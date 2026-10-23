# Mono Lifecycle and Output Redirection

## Overview

The Mono runtime runs the application code. HCOM does not implement Mono; it decides **whether** Mono
should start, starts it, keeps the host informed, forwards its console output, and handles the host
requests that change Mono's state. This document covers those jobs. The debugger bridge is in
[MonoRemoteDebug.md](MonoRemoteDebug.md).

```
   hcom_main (last start-up step)                           Mono task (own task group)
          |
          |  hcom_mono_ctrl_start_mono_main()
          |    checks: version, enabled, files, last run
          |    set "last run did not finish" flag
          |    task_create("Mono", priority 80) -------------> mono_main_proxy / meadow_mono_main
          |    start "MonoMonitor" thread                          |  monovm_initialize()
          |                                                        |  hcom_mono_ctrl_mono_appears_to_be_running()
          |<------------------ redirect stdout/stderr to FIFOs ----+     (redirect, clear the flag)
   "MonoStdxxx" thread: reads the FIFOs ---> host (TEXT_MONO_STDOUT / _STDERR)
          :                                                        |  ... the application runs ...
   "MonoMonitor": waitpid(Mono)  <------------ the task exits -----+
          |
          +--> meadow_os_reset_board(0)      the device restarts
```

Mono is built and enabled in the default `meadow_os` configuration (`CONFIG_SYSTEM_MONO=y`,
`CONFIG_HCOM_MONO_STDERR_STDOUT=y`). All of this code is in `apps/system/hcom/mono/`.

## Where Mono Comes From

The Runtime is a 3 MB binary, `Meadow.OS.Runtime.bin`, kept in its own partition at the start of the
QSPI flash (`HCOM_NX_FS_MONO_RAW_PARTITION_SIZE`, 0x300000; the partition table is in
[FlashPartitioning.md](FlashPartitioning.md)). At boot the kernel's configuration code
(`hcom_nx_config_init()`) validates it and copies it into SDRAM at `0xC0000000`
(`HCOM_NX_MONO_RUNTIME_SDRAM_ADDR`), from where it executes. The application assemblies are ordinary
files on the `/meadow0` file system.

The first flash block of the partition holds a signature structure (`mono_signature_t`). The kernel
reads it once at start-up (`hcom_nx_config_refresh_mono_version()`):

- If the signature value is not `0xDDCCBBAA`, no Runtime is considered present
  ("Mono Runtime was not found flashed in external flash") and the Mono version stays empty.
- Otherwise the major, minor, revision and build numbers, a date, a hash and the text version strings
  are taken from it. The strings (`short_string`, `long_string`) are only filled in for the current
  signature format; a Runtime in the older packed format yields numeric fields but no strings.

The version is therefore a boot-time snapshot. Flashing a new Runtime does not change it until the next
start.

## State Kept Across Restarts

Three bits in the battery-backed register BK31 (`hcom_bbreg_defn.h`) carry Mono's state. They survive a
reset; a full power loss clears them unless a backup battery is fitted.

| Bit | Value | Meaning |
|-----|-------|---------|
| `HCOM_BBREG_USER_RQST_MONO_ENABLE_BIT` | 0x0800 | **Set means Mono is disabled** (the name says the opposite). Set by `MONO_DISABLE` and by the version check; cleared by `MONO_ENABLE` |
| `HCOM_BBREG_MONO_LAST_RUN_LOCKUP_BIT` | 0x1000 | Set just before Mono is started and cleared by Mono once it reports it is running; if it is found set at the next start, the last attempt did not get that far |
| `HCOM_BBREG_MONO_DEBUGGING_START_BIT` | 0x2000 | A debug session was requested for the next start ([MonoRemoteDebug.md](MonoRemoteDebug.md)) |

`hcom_mono_ctrl_is_mono_enabled()` is simply "the disable bit is clear".

## Deciding Whether to Start

`hcom_mono_ctrl_start_mono_main()` is the last start-up step ([UserspaceStartup.md](UserspaceStartup.md)).
After configuring the blue LED pin it calls `hcom_mono_ctrl_should_mono_run()`, which tests these
conditions **in this order** and stops at the first that fails. Each failure sends the host a
`TEXT_INFORMATION` message and a syslog line.

| # | Condition | If it fails |
|---|-----------|-------------|
| 1 | **Versions match.** The OS version (major, minor, revision and build) equals Mono's | "Mono will not start - version mismatch: Meadow.OS version X, Mono version Y, Mono disabled". **Sets the disable bit**, so Mono stays off until the host re-enables it. If the Mono version strings are missing, the message is "mono version unavailable, Mono disabled", and the disable bit is set as well |
| 2 | (debug build only) not prevented by `HCOM_DIAG_PREVENT_MONO_FROM_RUNNING` | "Mono prevented from running by #define" |
| 3 | **Enabled.** The disable bit is clear | "Mono is disabled" |
| 4 | **Needed files exist.** `/meadow0/System.Private.CoreLib.dll` and `/meadow0/Meadow.dll` can be opened | "Mono will not start - the following file(s) is/are missing: ..." |
| 5 | **The last attempt finished.** The "last run" flag is clear | "Runtime will not start, it did not run correctly last time". The flag is **cleared** by this check, so the next start will try again |

Because the OS and Mono must share a build number, every rebuilt OS needs a matching Runtime flashed.

## Starting Mono

If all conditions hold:

1. Set the "last run" flag.
2. Read the configuration's `MonoControl.Options` string and filter it (below) into the task's arguments.
3. `task_create("Mono", 80, CONFIG_PTHREAD_STACK_DEFAULT, entry, argv)`. The entry is
   `mono_main_proxy` when the debugger option (`CONFIG_HCOM_MONO_REMOTE_DEBUGGING`) is built, and
   `meadow_mono_main` otherwise. The default stack is 65,536 bytes.
4. On success, send `TEXT_INFORMATION` "Meadow successfully started MONO" and start the monitor thread. On
   failure, "Meadow could not start Mono task".

Mono is a separate **task**, not a pthread, so it has its own descriptor table. The task would otherwise
share only standard input and output with HCOM, which is why the output redirection is set up from
inside the Mono task (below).

### The option filter

`hcom_mono_ctrl_extract_mono_options()` accepts only these options from the configuration string
(space-separated), matched without regard to case; anything else is silently dropped:

| Option | Match |
|--------|-------|
| `--optimize=...`, `--gc-params=...`, `--trace=...`, `--profile=...` | prefix |
| `-v`, `--llvmonly`, `--llvmonly-interp`, `--debug`, `--soft-breakpoints` | exact |
| `--jit`, `--aot`, `--interp` | exact (the run method, below) |

The **run method** is JIT by default. If exactly one of `--jit`, `--aot`, `--interp` is given, that one is
used (for AOT or interpreter mode the flag is appended to the arguments); if none or more than one is
given, JIT is used. `--jit` itself is never passed on, since it is the default.

### The monitor thread

`MonoMonitor` (priority 100, 2048-byte stack, detached) does one thing: `waitpid()` for the Mono task,
and when it exits, for any reason, call `meadow_os_reset_board(0)`. **Whenever Mono ends, the device
restarts.**

## "Mono Is Running"

The Mono task's main thread calls `hcom_mono_ctrl_mono_appears_to_be_running()` after
`monovm_initialize()` succeeds and before managed code runs (`apps/system/mono/mono_main.c`). Running it
inside the Mono task matters, because it changes that task's own standard streams. It:

1. **Redirects the task's stdout and stderr** to the output FIFOs ([below](#output-redirection)).
2. Opens `/dev/upd` for its own use (its descriptor table is not HCOM's) and reads the hardware version.
3. Tells the kernel that Mono has started (with a RAM log), so it restores UART1's configuration, which
   Mono's initialisation changed to a digital output.
4. Reconfigures the blue LED pin and drives it low.
5. **Clears the "last run" flag.**
6. Closes `/dev/upd`.

A failure at any step is logged and returns an error, and Mono does not continue. After this point a
crash is no longer treated as "did not run correctly" at the next start.

## Output Redirection

Mono's `Console` output is carried to the host over two named pipes (FIFOs) created by HCOM:

```
   Mono task                     HCOM task group                              host
   stdout (fd 1) --> /dev/monostdout --+
                                       +--> "MonoStdxxx" thread:  poll(), read, send
   stderr (fd 2) --> /dev/monostderr --+          |
                                                  +--> TEXT_MONO_STDOUT / TEXT_MONO_STDERR
                                                  +--> syslog copy (optional)
```

**Set-up** (`hcom_mono_stdxxx_read_setup()`, start-up step 14): allocate a 384-byte read buffer (385
with room for a terminator), register for low-power notices, create the two FIFOs with `mkfifo()`, and
start the thread (priority 120, 2048-byte stack). The thread signals the start-up manager once it is
running, then loops: open both FIFOs, then read.

**The reader thread** opens each FIFO `O_RDWR | O_NONBLOCK`. Opening it for writing as well as reading
is deliberate: a FIFO with no writer reports `POLLHUP` on every poll, which made the loop spin while
Mono was not running; as long as HCOM itself counts as a writer, it does not. The loop `poll()`s both
FIFOs with no timeout. When one is readable it makes one `read()` of up to 384 bytes and sends what it
got.

**The writer side** is the hook above: `hcom_mono_stdxxx_redirect()` opens each FIFO `O_WRONLY` (blocking)
and `dup2()`s it onto the Mono task's descriptor 1 or 2. Because the open is blocking, a Mono console
write waits whenever the FIFO is full.

**Forwarding to the host:** each chunk is sent as one `TEXT_MONO_STDOUT` (0x0309) or `TEXT_MONO_STDERR`
(0x030d) message with `hcom_host_send_raw_string_msg()`, which sends the bytes exactly as read.
Consequences:

- Chunks are **not line-aligned**: a message may start or end in the middle of a line, and control
  characters such as `\r` and `\n` are included. The host must reassemble lines.
- The text is **not NUL-terminated**.
- A chunk is at most 383 or 384 bytes (see Things to Know).
- When the host is not listening, the send reports success or `-EAGAIN` and the text is **discarded**;
  `-EAGAIN` is deliberately ignored. There is no buffering for later.

**The syslog copy:** when `g_copy_application_output_to_uart` is set, each chunk is also passed to
`hcom_mono_stdxxx_to_syslog()`, which writes it at `LOG_INFO`. The flag is set at start-up from two
configuration items: copy-application-output must be on **and** UART1 must be in use for trace.
Because the copy is written at `LOG_INFO`, it only appears when the syslog mask includes information
messages, which the default mask does not.

**Low power:** the stdout redirect's low-power callback only sets a flag (`_lowPowerActive`) used to
suppress one expected `ENOTCONN` error just after a sleep. It does not close any descriptor.

## Host Requests That Control Mono

| Request | Effect |
|---------|--------|
| `MONO_DISABLE` | Sets the disable bit, replies "Runtime disabled - restarting Meadow", `TEXT_RECONNECT`, restarts |
| `MONO_ENABLE` | Clears the disable bit, replies "Runtime enabled - restarting Meadow", `TEXT_RECONNECT`, restarts |
| `MONO_RUN_STATE` | Replies `TEXT_INFORMATION` "Mono is enabled" or "Mono is disabled" (the setting, not whether Mono is running) |
| `MONO_UPDATE_RUNTIME`, `MONO_UPDATE_FILE_END`, `MONO_FLASH` | Replace the Runtime image (next section) |
| `MONO_START_DBG_SESSION` | Arms the debugger and restarts ([MonoRemoteDebug.md](MonoRemoteDebug.md)) |

There is no request to stop or start Mono without restarting: enabling and disabling take effect through
a restart, and Mono never stops by itself without causing one.

File downloads and deletes are refused while Mono is enabled
([FileDownload.md](FileDownload.md#before-the-file-is-opened)). That includes the download of a new
Runtime, so a Runtime update begins with `MONO_DISABLE`.

## Updating the Runtime

The sequence the host follows (it matches the Mono part of `flash.sh`) is: disable Mono, upload the new
Runtime, then delete the file. In protocol terms:

1. `MONO_DISABLE` (restart).
2. `MONO_UPDATE_RUNTIME`: begins a file download like `START_FILE_TRANSFER`
   ([FileDownload.md](FileDownload.md)). The file must end up as `/meadow0/Meadow.OS.Runtime.bin`,
   because the kernel flashes that fixed name.
3. Data packets.
4. `MONO_UPDATE_FILE_END`: ends the download, then forwards a `MONO_FLASH` command to the kernel. The route
   code's comment notes this takes several seconds because the flash is erased first.
5. The host deletes `Meadow.OS.Runtime.bin` (as `flash.sh` does with `meadow file delete`). Mono
   stays disabled until a `MONO_ENABLE` is sent; whether the CLI's update command sends it itself was
   not checked.

`MONO_FLASH` is also a request in its own right, which flashes whatever file is already at that path.

The kernel handler, `hcom_nx_exec_ex_flash_mono_flash()`, does this: open the file; require its size to be
exactly 3 MB; send `TEXT_INFORMATION` "Erasing mono flash memory"; erase the Runtime partition; write it
block by block, reporting "Flashing N% complete" about every 10 %; read every block back and compare,
reporting "Verifying N% complete"; and finally "Runtime flashed successfully". A failure while writing or
verifying sends `TEXT_ERROR`.

The version that the next start compares is read from the new image only after a restart.

## Configuration

| Item | Where | Effect |
|------|-------|--------|
| `CONFIG_SYSTEM_MONO` | defconfig | Builds Mono, and makes HCOM start it |
| `CONFIG_HCOM_MONO_STDERR_STDOUT` | board Kconfig (default y), defconfig (y) | Builds the output redirection |
| `CONFIG_HCOM_MONO_REMOTE_DEBUGGING` | board Kconfig (default y) | Builds the debugger bridge and uses `mono_main_proxy` as Mono's entry point |
| `MonoControl.Options` | `meadow.config.yaml` | Mono command-line options (filtered, above) |
| `InternalDebug.CopyApplicationOutputToUart`, `InternalDebug.Uart1Use` | `meadow.config.yaml` | Both must be on to enable the syslog copy of Mono's output |
| `MONO_TASK_PRIORITY` (80), `HCOM_THREAD_PRIORITY_MONO_MONITOR` (100), `HCOM_THREAD_PRIORITY_STDXXX_REDIRECT` (120) | `meadow_thread_config.h` | Priorities |

## Things to Know

- **Mono ending always restarts the device**, even after a clean exit. A Mono that exits as soon as it
  has started would cause the device to restart repeatedly, because the "last run" flag has already been
  cleared by then.
- **A failure before the "running" hook costs one skipped start.** The flag is found set at the next
  boot, that boot does not start Mono (and clears the flag), and the boot after that tries again. A
  crash after the hook is treated as an ordinary run.
- **A version mismatch disables Mono persistently.** The disable bit is set, and stays set across
  resets until the host enables Mono again. The same happens when the Runtime signature is missing or in
  the old format.
- **The version is a boot-time snapshot.** A freshly flashed Runtime is not seen until the device has
  restarted.
- **Start failures are quiet.** `hcom_mono_ctrl_start_mono_main()` returns an error for every "will not
  start" case, but the start-up manager discards the result, so HCOM carries on and the host only sees
  the `TEXT_INFORMATION` reason.
- **Mono output can be lost in two ways in the forwarding code:** the last byte of every full 384-byte
  read is dropped (Issue #824), and the optional syslog copy discards unterminated lines and prints stale
  characters for non-printable ones (Issue #825).
- **Runtime-flash failures are only partly visible to the host** (Issue #826): a missing or wrong-size
  Runtime file is reported only to syslog, and a failed erase reports success.
- **Output is blocking on Mono's side.** The write end is opened without `O_NONBLOCK`, so Mono's console
  writes wait when the FIFO is full; a send stalled on the host can therefore slow the application.
- **The run-state reply reports the setting, not the process.** "Mono is enabled" says nothing about
  whether the Mono task is alive.
- **The LED comments disagree.** At start the code writes `false` to the blue LED pin with a comment
  saying the LED stays on until Mono calls back; the "running" hook writes `false` again with a comment
  saying it turns the LED off. The same write cannot do both, so what is actually seen on a board should
  be checked on hardware.
- **Stale comments:** `RebaseNotes.txt` and `hcom_shared_common.h` still say the redirect is disabled;
  `mono_main.c` says HCOM's FIFO opener always redirects stdout regardless of its parameter (it does not);
  and the header of `hcom_remote_vs_debug.c` says there is no Mono runtime.

## Source Map

| Concern | File |
|---------|------|
| Start decision, option filter, task creation, monitor, hook | `apps/system/hcom/mono/hcom_mono_control.c` |
| Output FIFOs and forwarding | `apps/system/hcom/mono/hcom_stdxxx_redirect.c` |
| Mono task entry and the hook call | `apps/system/mono/mono_main.c` |
| Request handling | `apps/system/hcom/comms/hcom_host_route.c` |
| Mono version from the image, copy to SDRAM | `F7MeadowOS/hcom_nx/hcom_nx_config_manager.c` |
| Flashing the Runtime | `F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_flash.c` |
| State bits | `F7MeadowOS/include/meadow/hcom_bbreg_defn.h` |
| Priorities, names, stack sizes | `F7MeadowOS/include/meadow/meadow_thread_config.h` |

## Related Documents

[MonoRemoteDebug.md](MonoRemoteDebug.md), [FileDownload.md](FileDownload.md),
[UserspaceStartup.md](UserspaceStartup.md), [ConfigManager.md](ConfigManager.md),
[FlashPartitioning.md](FlashPartitioning.md), [WireProtocol.md](WireProtocol.md).
