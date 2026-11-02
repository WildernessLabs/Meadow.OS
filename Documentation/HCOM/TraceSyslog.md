# Trace and Syslog

## Overview

Meadow.OS has no serial console. Everything the kernel and the user-space code report through `syslog()` is
written to a **RAMLOG**, a 32 KB circular buffer in RAM (`/dev/kmsg`). Nothing leaves the buffer unless a reader
copies it somewhere. This document describes the readers: the thread that forwards the log to a UART and to the
host, the channel that carries log text from the kernel to HCOM, and the controls (a syslog mask and three
routing switches) that the host can change at run time.

Other text reaches the host by the same channel, for example the test output sent by `syslogToHost()`
([DeveloperCommandsNsh.md](DeveloperCommandsNsh.md)). Mono's console output is a separate path
([MonoLifecycle.md](MonoLifecycle.md#output-redirection)).

```
   kernel syslog()   user syslog() (syscall)
         \                /
          v              v
        +----------------------------+                     CONFIG_RAMLOG_SYSLOG
        |  RAMLOG  /dev/kmsg  32 KB  |   (overwrites oldest data when full)
        +----------------------------+
              |                |
              | reader 1       | reader 2 (--dualcdc builds only)
              v                v
     "RamlogRead" kthread   "RamlogUsb" kthread ----> /dev/ttyACM1 (second USB port)
        |            |
        | UART       | host
        v            v
   USART1 (/dev/ttyS0)    /dev/hcom_traverse_ku  (named FIFO, kernel -> user)
                              |
                         "Traverse" pthread (HCOM) ----> TEXT_TRACE_MSG (0x030b) to the host
```

Without `CONFIG_RAMLOG_SYSLOG` (a `build.sh --esd` build, below) there is no RAMLOG and none of the readers exist;
the routing requests then reply "Trace logging not available".

## The Syslog Mask

NuttX filters `syslog()` calls with a mask of priority bits. In a protected build the kernel and user space
each keep **their own mask**, and Meadow keeps the two in step.

| Level (priority) | Value | In the default mask |
|------------------|-------|---------------------|
| `LOG_EMERG` to `LOG_WARNING` | 0 to 4 | yes |
| `LOG_NOTICE` | 5 | at power-on yes; "Default" via the CLI, no |
| `LOG_INFO` | 6 | no |
| `LOG_DEBUG` | 7 | no |

`LOG_MDIAG` (`hcom_shared_common.h`) is `LOG_CRIT`, so diagnostic lines written with it are always visible.

The four masks the code names, and their CLI levels:

| CLI level (`userData`) | Mask | Meaning |
|------------------------|------|---------|
| 0, Default | 0x1f | Emergency to warning |
| 1, Notice | 0x3f | and notice |
| 2, Notice and Information | 0x7f | and info |
| 3, Notice, Information and Debug | 0xff | everything |

### Where the mask is kept

The low 8 bits of battery-backed register 31 hold the mask, so it survives a restart
([PlatformState.md](PlatformState.md)). At each start:

1. **Kernel**, startup-manager step 4 (`hcom_nx_utils_startup_handling_of_trace_level()`): if the whole register
   is 0 (a power-on, since the registers clear when power is lost), the mask is set to **0x3f**
   (emergency to notice) and saved. Otherwise the saved mask is used and `HCOM_BBREG_LAST_START_WAS_REBOOT_BIT`
   is set. The mask is applied with `setlogmask()`.
2. **Kernel**, step 13: the configured `InternalDebug.TraceLevel` is merged in
   (`hcom_nx_utils_apply_config_trace_level()`): 1 adds notice, 2 adds notice and info, 3 adds notice, info
   and debug. This is **not** saved to the register.
3. **User space**, HCOM start-up (`hcom_diag_trace_priority_level_init()`): reads the same register, applies the
   same configured level to its own mask, and logs a start-up banner at notice level: the OS version, whether
   this was a power-on or a reboot, the hardware, whether Mono is enabled, the trace level, and the
   destination.

The configured level is applied only up to 3. A `TraceLevel` of 4 is accepted by the configuration parser but both
sides log "Unsupported config trace level of 4 ignored" and leave the mask as it was
([ConfigManager.md](ConfigManager.md)). Because the power-on mask already includes notice, a configured level
of 1 changes nothing after a power-on; it matters only when the saved mask was lowered to 0x1f.

### Changing it at run time

The host's `CHANGE_TRACE_LEVEL` request (0x0102, `userData` 0 to 3) is handled in user space
(`hcom_diag_trace_priority_change_trace_level()`):

1. Build the new mask (any other `userData` gives the default, 0x1f).
2. Save it in the low 8 bits of the register.
3. `setlogmask()` for user space.
4. Ask the kernel to apply it (`HCOM_NX_UPD_SET_SYSLOG_MASK`, [UpdInterface.md](UpdInterface.md)), so kernel
   messages follow at once rather than at the next restart.
5. Reply `TEXT_INFORMATION` "Trace level changed from 'X' (0x..) to 'Y' (0x..)" (or "remained at"), and log the
   same text at notice level.

The level named for the old mask is chosen only from the four masks in the table; any other value is reported
as "Normal".

## The RAMLOG Reader: `RamlogRead`

A kernel thread, `RamlogRead` (priority 120, 2048-byte stack), reads `/dev/kmsg` and forwards each line. It is
**started lazily**: if neither the UART nor the host is a destination, it does not exist. It is created at
start-up if a battery-backed routing bit says so, and by a host request otherwise.

### The routing switches

Three bits in the battery-backed register (the full map is in [PlatformState.md](PlatformState.md)) record the
destinations, so they too survive a restart:

| Bit | Value | Set by request | Cleared by request | Effect |
|-----|-------|----------------|--------------------|--------|
| `HCOM_BBREG_ROUTE_TRACE_MSG_TO_HOST_BIT` | 0x200 | `SEND_TRACE_TO_HOST` (0x0115) | `NO_TRACE_TO_HOST` (0x0114) | Send log lines to the host |
| `HCOM_BBREG_ROUTE_TRACE_MSG_TO_UART_BIT` | 0x400 | `SEND_TRACE_TO_UART` (0x011a) | `NO_TRACE_TO_UART` (0x011b) | Write log lines to the UART |
| `HCOM_BBREG_ROUTE_PROFILER_BINS_TO_UART_BIT` | 0x4000 | `SEND_PROFILER_TO_UART` (0x0123) | `NO_PROFILER_TO_UART` (0x0124) | Reserve the UART for profiling data |

These are forwarded to the kernel ([UpdInterface.md](UpdInterface.md#forwarding-a-host-request-cli_command))
and handled in `hcom_nx_trace_msg_proc.c`. Each replies with one `TEXT_INFORMATION` message, for example
"Trace logs now sent to CLI and UART", "No change. Trace logs already sent to UART", or "Trace logging failed to
initialize". The configuration file also sets these at start-up: `InternalDebug.Uart1Use` of `trace` turns the
UART bit on, and `profiler` turns the profiler bit on (`hcom_nx_trace_insure_correct_config()`,
[ConfigManager.md](ConfigManager.md)).

Rules between the switches:

- **Profiler wins.** While the profiler flag is set, the UART belongs to the profiler: the thread neither opens
  nor writes it for trace. Setting the profiler from the configuration file also clears the UART-trace flag; the
  request does not.
- **No destination, no thread.** When both trace destinations are cleared, the thread is asked to exit
  (`hcom_nx_trace_kthread_exit_initiate()`: set a flag, signal the thread with `SIGALRM`, wait up to 5 s for it
  to finish). The thread otherwise runs until the device restarts.
- **Idempotent.** A request that changes nothing says so.

### What the thread does

```
   loop until told to stop:
        open /dev/kmsg                   -- on failure: wait 1 s, retry
        open the UART (if wanted)        -- on failure: wait 1 s, retry
        loop:
            fgets() one line (up to 511 bytes)       -- blocks
            skip it if no destination remains
            UART: write the line (retrying short writes)
            host: hcom_traverse_ku_syslog_to_host()  -- see below
            any failure: leave the loop (close, wait 1 s, reopen)
```

Points about it:

- **Lines, not bytes.** The thread reads whole lines, with one buffer of 512 bytes. A longer line is reported as
  truncated on the UART. A line sent to the host is cut to the channel's 256-byte payload limit by
  `hcom_traverse_ku_syslog_to_host()`.
- **UART choice.** The syslog UART is chosen at build time (`HCOM_DIAG_SYSLOG_UART_NUMBER`, default 1, which is
  `/dev/ttyS0` on pins PB14 and PB15; 4 and 6 are the alternatives). The "COM1" on the board is this port.
- **An early marker.** When the UART is a destination, one line "Meadow.OS initialization began at <time> Meadow
  UTC time." is written straight to the UART, not through syslog, so a crash soon afterwards still shows that the
  start was reached.
- **Errors go to the UART directly.** The thread's own errors cannot be logged with `syslog()` (that would
  re-enter the buffer it is reading), so `hcom_nx_uart_direct()` writes them straight to the UART. That function
  may be called **only from a kernel thread**; the file warns that calling it from a user thread crashes the
  system.
- **Mono reconfigures the pins.** When Mono starts it reconfigures every GPIO, including the UART's. After
  `MONO_HAS_STARTED` ([UpdInterface.md](UpdInterface.md)) the thread re-applies the UART pin configuration before
  each write for the next **30 seconds**.
- **A line that cannot be delivered is lost.** The code says so: "If this function cannot send it, the message is
  lost".
- **Reading restarts from the oldest data.** NuttX's RAMLOG starts each new reader at the oldest data still in the
  buffer (the newest 32 KB), not at the end, so every time the thread reopens the log, everything still in the
  buffer is sent again.

## The Kernel-to-User Channel

Text for the host cannot be sent from the kernel directly: the host link belongs to HCOM in user space. The kernel
side hands the text to a **named FIFO**, `/dev/hcom_traverse_ku`, and an HCOM thread reads it and sends it on.

### Messages

Each item is a fixed-size envelope (`struct hcom_traverse_ku_msg_t`, 264 bytes), written whole:

| Field | Size | Meaning |
|-------|------|---------|
| `traverseType` | 4 | How the receiver should send it (below) |
| `rqstType` | 2 | The HCOM host request type to send it as |
| `length` | 2 | Bytes used in the payload |
| `payload` | 256 | The text |

| Type | Sent to the host as | Used by |
|------|---------------------|---------|
| `SYSLOG_TEXT` | `rqstType` (`TEXT_TRACE_MSG`) with the length given | The RAMLOG reader |
| `GENERIC_TEXT` | `rqstType`, length given | `syslogToHost()` (as `TEXT_INFORMATION`), the power-management code (`TEXT_NEXT_LOW_PWR`), the channel's tests |
| `GENERIC_STRING` | `rqstType`, NUL-terminated | The channel's tests; no other user in this repository |

The 264-byte envelope is smaller than `PIPE_BUF` (512 on NuttX), so one `write()` is atomic and no framing
is needed. A payload larger than 256 bytes, or a zero-length one, is rejected with `-EINVAL`.

### Kernel side (`hcom_traverse_ku_kernel.c`)

- `hcom_traverse_ku_kernel_setup()` creates the FIFO with `mkfifo()` during start-up (step 3b,
  [KernelStartup.md](KernelStartup.md)).
- The write end is opened **lazily** on the first send, non-blocking, using `file_open()` and `file_write()` on a
  `struct file` and not a file descriptor. This matters in a protected build: two unrelated contexts can be first
  to send (the RAMLOG kthread, and kernel tests that arrive through a syscall on an ordinary task), and a plain
  descriptor is valid only in the table of the task that opened it, so the other context would fail with `EBADF`.
  A mutex protects the shared `struct file`.
- If no reader is attached yet (`-ENXIO`), the open retries every 10 ms for up to **3 seconds**, then gives up on
  that message. The bound exists because a restart where a previous session left host tracing enabled may never
  gain a reader, and without it the RAMLOG thread, and the UART output with it, would wait for ever.
- The write end stays non-blocking. A blocking write could split an envelope across two ring-buffer writes when the
  pipe is almost full, and the reader would lose synchronisation. A full pipe gives `EAGAIN`, retried after 1 ms,
  so a slow host slows the sender (back-pressure) but does not corrupt it.
- On `EPIPE` (the reader went away) the write end is closed and reopened, and the same message is sent again.
- During shutdown of the trace thread, every retry loop gives up with `-ESHUTDOWN`.
- Not safe from an interrupt handler.

### User side (`hcom_traverse_ku_user.c`)

`hcom_traverse_ku_init_user_setup()` is early in HCOM start-up ([UserspaceStartup.md](UserspaceStartup.md)),
before any kernel code sends. It opens the FIFO read-only and non-blocking (so the open does not wait for the
kernel), then makes the descriptor blocking, and starts the `Traverse` thread (priority 120, 2048-byte stack).
The thread:

- reads until a whole envelope has arrived, and dispatches on `traverseType` to the matching send function of
  the host send pipeline ([SendPipeline.md](SendPipeline.md));
- on end-of-file (no writer attached) discards any partial envelope and waits 5 ms, since a reconnecting writer
  always begins a new envelope;
- logs other read errors and keeps going. The thread never exits on its own.

Because these sends use the normal host send functions, trace text competes with everything else for the host
link and is subject to the send budgets and blocked-host handling ([SendPipeline.md](SendPipeline.md)).

## The Second USB Port

A `build.sh --dualcdc` build also copies the log to the second CDC/ACM port, `/dev/ttyACM1`
([DualCdcAcm.md](../DualCdcAcm.md)). A second kernel thread, `RamlogUsb` (priority 110), has its own RAMLOG
reader and writes raw 256-byte chunks to the port, with the terminal set to add a carriage return to each
newline. It opens the RAMLOG once and keeps it, so its place in the log survives a USB disconnect; while no
program reads the port, only this thread waits, and the RAMLOG holds the backlog until it overflows. When the
port is disconnected the remainder of the chunk is dropped and the port is reopened. This copy has no routing
bits: it is on whenever the build includes it.

## Build-Time Switches

| Item | Effect |
|------|--------|
| `CONFIG_RAMLOG_SYSLOG` | Without it there is no RAMLOG, no reader threads and no trace routing |
| `build.sh --esd` | Replaces the RAMLOG with a real console on USART1 and enables stack colouring, so that a hard fault prints something. The routing requests then reply "Trace logging not available" |
| `HCOM_DIAG_SYSLOG_UART_NUMBER` | Which UART the trace uses: 1 (default), 4 or 6 |
| `HCOM_FORCE_SYSLOG_MASK_AND_OUTPUT_TO_UART` | Forces notice and info into the mask and UART tracing on from the start |
| `HCOM_DIAG_INCLUDE_NX_STARTUP_SYSLOG` | Writes "Kernel startup manager N" before each start-up step ([KernelStartup.md](KernelStartup.md)) |
| `HCOM_DIAG_OUTPUT_SYSLOG_PID_OF_NEW_THREADS` | Logs the thread and task id of each new Meadow thread |
| `HCOM_DIAG_INCLUDE_MESSAGE_DECODING_IN_BUILD` | Logs every HCOM message sent and received in readable form (`hcom_diag_decode_protocol.c`) |
| `CONFIG_SYSLOG_TIMESTAMP`, `CONFIG_SYSLOG_PROCESSID` | Both are set, so each line starts with a time stamp and the process id |

`MEADOW_TRACE_INFORMATION`, `MEADOW_TRACE_DEBUG` and the related macros in `meadow_debug_helpers.h` are thin
wrappers over `syslog()` that compile to nothing unless a file turns on `USE_MEADOW_DEBUG_HELPERS`.

## Things to Know

- **The settings are remembered across restarts.** The mask and the three routing bits live in the battery-backed
  register, so tracing that was left on keeps going after a restart, and it clears only on a power loss with no
  coin cell. (The register is cleared only at power-on.)
- **Long lines are cut short for the host.** Only the first 256 bytes of a line reach the host.
- **Reopening replays the log.** Any error that makes the thread reopen the RAMLOG re-sends the buffer.
- **Lost lines are normal.** A full RAMLOG overwrites its oldest data; a line that cannot be delivered is
  dropped; the 3-second wait for a reader drops a message. Treat the trace as best effort.
- **The default mask differs by route.** A power-on gives notice (0x3f); the CLI's "Default" gives 0x1f.
- **A stale comment** in `hcom_diag_trace_priority_level_init()` says the configured trace level affects only
  user space. The kernel also applies it (start-up step 13).
- **Routing requests need no host reader to succeed.** `SEND_TRACE_TO_HOST` only sets a flag and starts a thread; if
  the host does not read `TEXT_TRACE_MSG`, the FIFO fills and the thread slows.
- **Trace text shares the host link.** A busy transfer slows tracing, and tracing slows the transfer.

## Source Map

| Concern | File |
|---------|------|
| The RAMLOG reader, routing requests, UART output | `F7MeadowOS/hcom_nx/diag/hcom_nx_trace_msg_proc.c` |
| Kernel side of the FIFO | `F7MeadowOS/hcom_nx/diag/hcom_traverse_ku_kernel.c` |
| Second USB port copy | `F7MeadowOS/hcom_nx/diag/hcom_nx_trace_usb.c` |
| FIFO envelope and API | `F7MeadowOS/include/meadow/hcom_traverse_ku.h` |
| User side of the FIFO | `apps/system/hcom/diag/hcom_traverse_ku_user.c` |
| Kernel mask handling, restarts | `F7MeadowOS/hcom_nx/hcom_nx_common_utils.c` |
| User mask handling and the CLI level request | `apps/system/hcom/diag/hcom_diag_trace_priority.c` |
| Message decoding (diagnostic) | `apps/system/hcom/diag/hcom_diag_decode_protocol.c` |
| Thread names, priorities and sizes | `F7MeadowOS/include/meadow/meadow_thread_config.h` |
| Switches and `LOG_MDIAG` | `F7MeadowOS/include/meadow/hcom_shared_common.h` |
| Register bits | `F7MeadowOS/include/meadow/hcom_bbreg_defn.h` |
| `--esd` | `scripts/configuration_tweaks.sh` (`enable_stack_dump()`) |

## Related Documents

[KernelStartup.md](KernelStartup.md), [ConfigManager.md](ConfigManager.md), [UpdInterface.md](UpdInterface.md),
[PlatformState.md](PlatformState.md), [SendPipeline.md](SendPipeline.md), [MonoLifecycle.md](MonoLifecycle.md),
[DualCdcAcm.md](../DualCdcAcm.md), [Debugging.md](../Debugging.md).
