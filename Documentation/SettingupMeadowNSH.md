# Setting up Meadow NSH

## Overview

NSH (NuttShell) is NuttX's built-in command-line shell. On Meadow F7 boards it runs
over a dedicated UART (e.g. USART6) rather than the normal debug console, so it can be
enabled independently without disturbing the main console/HCOM traffic. This guide
covers the two things needed to get it working: the build option that enables it, and
how to set up a terminal program to talk to it.

This assumes the codebase already contains NSH's console-setup fixes (the serial
line-discipline configuration in `apps/system/hcom/diag/hcom_diag_nsh_support.c` and
the `/proc` mount in `F7MeadowOS/hcom_nx/hcom_nx_startup_mgr.c`). This document only
covers configuration, not those source changes.

## Step 1: Build with NSH enabled

NSH is used only for rare diagnostic sessions, so **none of its configuration
lives in the tracked `defconfig`** -- the board's normal build carries zero NSH
content. Everything needed is applied on top, at build time, by passing `--nsh`
to `build.sh`:

```bash
# Full clean build with NSH
./build.sh --clean --force --nsh

# Subsequent incremental builds keep NSH - .config already has it
./build.sh
```

`--nsh` calls the `enable_nsh` function in `scripts/configuration_tweaks.sh`
(sourced by `build.sh`, not defined in it directly), which applies every
setting below to `nuttx/.config` with `kconfig-tweak`. Because `kconfig-tweak`
edits `.config` as text and never runs `olddefconfig`, that function has to
write out every value Kconfig would otherwise have defaulted (the
`NSH_PROMPT_*`, `NSH_MAXARGUMENTS`, `USART6_BAUD` sort of thing) in addition to
the values that matter functionally. `enable_nsh()` is the **only** place any
of this is set -- there is deliberately no equivalent block anywhere in
`defconfig` to keep in sync. If you add a setting here, add it to that function
only.

Three exceptions live in the base `defconfig` permanently, not in
`enable_nsh()`, because they aren't actually NSH-specific even though NSH is
what surfaced the need for them:

- `CONFIG_BOARD_LOOPSPERMSEC` -- a global `up_udelay()`/`up_mdelay()`
  timing-calibration constant used system-wide, measured using NSH's
  `calib_udelay` tool but not meaningful only to NSH.
- `CONFIG_DEFAULT_TASK_STACKSIZE` -- the generic fallback stack size for any
  task that doesn't set its own, raised for `calib_udelay`'s benefit but not an
  NSH-only knob.
- `CONFIG_FDCLONE_STDIO` -- controls file-descriptor inheritance for *every*
  `task_create()`/`task_spawn()` in the system, not just NSH's builtin
  commands. Most of Meadow's own kernel-space work uses `kthread_create()`
  (exempt from this setting entirely), but `apps/system/hcom/mono/hcom_mono_control.c`
  launches the Mono/.NET runtime itself with `task_create()`, and its own
  comment documents relying on exactly this behavior: *"A task will only share
  stdin, stderr and stdout with the threads running hcom."* That claim is only
  actually true when `CONFIG_FDCLONE_STDIO=y` is set -- without it (Kconfig's
  real default), a spawned task clones *every* file descriptor its parent has
  open, not just stdio.

  `CONFIG_FDCLONE_DISABLE` must never be set alongside it (or at all).
  `group_setuptaskfiles()` (`nuttx/sched/group/`) wraps its entire
  file-descriptor-inheritance logic in `#ifndef CONFIG_FDCLONE_DISABLE`, so if
  that's set, no spawned task -- Mono, `nsh`, `calib_udelay`, a nested `nsh`,
  nothing -- inherits stdin/stdout/stderr at all. The task still runs to
  completion, but every `printf()` silently goes nowhere, while typed input to
  an interactive one (like a nested `nsh`) still appears to echo (that's the
  serial driver's own termios-level echo, unrelated to whether the task has a
  valid stdin fd). This exact combination (`FDCLONE_DISABLE=y` left on
  alongside `FDCLONE_STDIO=y`, which silently makes the latter dead
  configuration) was accidentally introduced once already, in an unrelated
  "turn on some debug features" commit -- watch for it if NSH commands, or
  Mono's console output, ever stop appearing again.

The settings `enable_nsh()` applies, with the notes that explain them, are:

```
# ---- NSH support (see Documentation/SettingupMeadowNSH.md) ----
CONFIG_BUILTIN=y
CONFIG_NSH_BUILTIN_APPS=y
CONFIG_SYSTEM_READLINE=y
CONFIG_FS_PROCFS=y
CONFIG_FS_PROCFS_REGISTER=y
CONFIG_SCHED_CPULOAD_SYSCLK=y
CONFIG_TTY_SIGINT=y
CONFIG_NSH_ALTCONDEV=y
CONFIG_NSH_ALTSTDERR="/dev/ttyS3"
CONFIG_NSH_ALTSTDIN="/dev/ttyS3"
CONFIG_NSH_ALTSTDOUT="/dev/ttyS3"
CONFIG_NSH_DISABLE_IFCONFIG=y
CONFIG_NSH_DISABLE_IFUPDOWN=y
CONFIG_NSH_DISABLE_TIMEDATECTL=y
CONFIG_READLINE_CMD_HISTORY=y
CONFIG_READLINE_ECHO=y
CONFIG_STM32F7_USART6=y
CONFIG_USART6_RXBUFSIZE=512
CONFIG_USART6_TXBUFSIZE=512
CONFIG_SYSTEM_NSH=y
CONFIG_SYSTEM_NSH_PRIORITY=180
CONFIG_SYSTEM_NSH_STACKSIZE=8192
CONFIG_NSH_INITSCRIPT="init.d/rcS"
CONFIG_NSH_SYSINITSCRIPT="init.d/rc.sysinit"
CONFIG_NSH_SCRIPT_REDIRECT_PATH=""

# CONFIG_FS_PROCFS_EXCLUDE_BLOCKS is not set
# CONFIG_FS_PROCFS_EXCLUDE_CPUINFO is not set
# CONFIG_FS_PROCFS_EXCLUDE_CPULOAD is not set
# CONFIG_FS_PROCFS_EXCLUDE_ENVIRON is not set
# CONFIG_FS_PROCFS_EXCLUDE_IOBINFO is not set
# CONFIG_FS_PROCFS_EXCLUDE_MEMDUMP is not set
# CONFIG_FS_PROCFS_EXCLUDE_MEMINFO is not set
# CONFIG_FS_PROCFS_EXCLUDE_MOUNT is not set
# CONFIG_FS_PROCFS_EXCLUDE_MOUNTS is not set
# CONFIG_FS_PROCFS_EXCLUDE_NET is not set
# CONFIG_FS_PROCFS_EXCLUDE_PARTITIONS is not set
# CONFIG_FS_PROCFS_EXCLUDE_PROCESS is not set
# CONFIG_FS_PROCFS_EXCLUDE_ROUTE is not set
# CONFIG_FS_PROCFS_EXCLUDE_TCBINFO is not set
# CONFIG_FS_PROCFS_EXCLUDE_UPTIME is not set
# CONFIG_FS_PROCFS_EXCLUDE_USAGE is not set
# CONFIG_FS_PROCFS_EXCLUDE_VERSION is not set
# CONFIG_FS_PROCFS_INCLUDE_PROGMEM is not set

# CONFIG_NETUTILS_NETINIT is not set
# CONFIG_NSH_NETINIT is not set
# CONFIG_NSH_DISABLEBG is not set
# CONFIG_NSH_DISABLESCRIPT is not set
# CONFIG_NSH_DISABLE_ITEF is not set
# CONFIG_NSH_DISABLE_LOOPS is not set

# CONFIG_NSH_DISABLE_ADDROUTE is not set
# CONFIG_NSH_DISABLE_ARP is not set
# CONFIG_NSH_DISABLE_BASENAME is not set
# CONFIG_NSH_DISABLE_CAT is not set
# CONFIG_NSH_DISABLE_CD is not set
# CONFIG_NSH_DISABLE_CMP is not set
# CONFIG_NSH_DISABLE_CP is not set
# CONFIG_NSH_DISABLE_DATE is not set
# CONFIG_NSH_DISABLE_DELROUTE is not set
# CONFIG_NSH_DISABLE_DF is not set
# CONFIG_NSH_DISABLE_DIRNAME is not set
# CONFIG_NSH_DISABLE_DMESG is not set
# CONFIG_NSH_DISABLE_ECHO is not set
# CONFIG_NSH_DISABLE_ENV is not set
# CONFIG_NSH_DISABLE_ERROR_PRINT is not set
# CONFIG_NSH_DISABLE_EXEC is not set
# CONFIG_NSH_DISABLE_EXIT is not set
# CONFIG_NSH_DISABLE_EXPORT is not set
# CONFIG_NSH_DISABLE_EXPR is not set
# CONFIG_NSH_DISABLE_FDINFO is not set
# CONFIG_NSH_DISABLE_FREE is not set
# CONFIG_NSH_DISABLE_GET is not set
# CONFIG_NSH_DISABLE_HEXDUMP is not set
# CONFIG_NSH_DISABLE_KILL is not set
# CONFIG_NSH_DISABLE_LOMTD is not set
# CONFIG_NSH_DISABLE_LOSETUP is not set
# CONFIG_NSH_DISABLE_LOSMART is not set
# CONFIG_NSH_DISABLE_LS is not set
# CONFIG_NSH_DISABLE_MB is not set
# CONFIG_NSH_DISABLE_MH is not set
# CONFIG_NSH_DISABLE_MKDIR is not set
# CONFIG_NSH_DISABLE_MKFATFS is not set
# CONFIG_NSH_DISABLE_MKFIFO is not set
# CONFIG_NSH_DISABLE_MKRD is not set
# CONFIG_NSH_DISABLE_MOUNT is not set
# CONFIG_NSH_DISABLE_MV is not set
# CONFIG_NSH_DISABLE_MW is not set
# CONFIG_NSH_DISABLE_NSLOOKUP is not set
# CONFIG_NSH_DISABLE_PIDOF is not set
# CONFIG_NSH_DISABLE_PKILL is not set
# CONFIG_NSH_DISABLE_PRINTF is not set
# CONFIG_NSH_DISABLE_PS is not set
# CONFIG_NSH_DISABLE_PSSIGMASK is not set
# CONFIG_NSH_DISABLE_PUT is not set
# CONFIG_NSH_DISABLE_PWD is not set
# CONFIG_NSH_DISABLE_REBOOT is not set
# CONFIG_NSH_DISABLE_RM is not set
# CONFIG_NSH_DISABLE_RMDIR is not set
# CONFIG_NSH_DISABLE_ROUTE is not set
# CONFIG_NSH_DISABLE_SEMICOLON is not set
# CONFIG_NSH_DISABLE_SET is not set
# CONFIG_NSH_DISABLE_SHUTDOWN is not set
# CONFIG_NSH_DISABLE_SLEEP is not set
# CONFIG_NSH_DISABLE_SOURCE is not set
# CONFIG_NSH_DISABLE_TEST is not set
# CONFIG_NSH_DISABLE_TIME is not set
# CONFIG_NSH_DISABLE_TOP is not set
# CONFIG_NSH_DISABLE_TRUNCATE is not set
# CONFIG_NSH_DISABLE_UMOUNT is not set
# CONFIG_NSH_DISABLE_UNAME is not set
# CONFIG_NSH_DISABLE_UNSET is not set
# CONFIG_NSH_DISABLE_UPTIME is not set
# CONFIG_NSH_DISABLE_USLEEP is not set
# CONFIG_NSH_DISABLE_WAIT is not set
# CONFIG_NSH_DISABLE_WATCH is not set
# CONFIG_NSH_DISABLE_WGET is not set
# CONFIG_NSH_DISABLE_XD is not set
# ---- End NSH support ----
```

Notes:

- `ifconfig`/`ifupdown`/`timedatectl` are deliberately left disabled -- the first two
  to avoid NSH fighting Meadow's own network/cloud management, the third because it
  needs kernel-internal symbols not exposed to userspace in this build. Leave them
  disabled unless someone has specifically addressed that.
- This board sets `CONFIG_DEFAULT_SMALL=y`, so every `NSH_DISABLE_*` and
  `FS_PROCFS_EXCLUDE_*` option defaults to `y`. That is why the block above is
  mostly "is not set" lines -- each one buys back a command or a `/proc` entry.
- `CONFIG_FS_PROCFS_EXCLUDE_PROCESS` must be off for `ps`/`wait` to work -- without
  it, those two commands silently disappear regardless of their own `NSH_DISABLE_*`
  setting. The remaining `EXCLUDE_*` options are cleared for the same reason:
  `uptime`, `df`, `mount`, `free` and `env` each read a `/proc` entry that its
  matching `EXCLUDE_*` option would remove.
- `CONFIG_FS_PROCFS_INCLUDE_PROGMEM` is the exception to the point above and must
  stay off. Kconfig offers it because `ARCH_HAVE_PROGMEM` is selected arch-wide,
  but the STM32F7 implementation (`arch/arm/src/stm32f7/stm32_flash.c`) is only
  compiled when `CONFIG_STM32F7_PROGMEM=y`, which this board does not set. Turning
  it on fails the link with undefined references to `up_progmem_*`.
- `CONFIG_NSH_NETINIT` defaults to `y` whenever networking is enabled and would
  have NSH bring the network up itself. It is left off (along with the
  `NETUTILS_NETINIT` helper it selects) for the same reason `ifconfig` is
  disabled -- Meadow owns network and cloud management.
- `top` needs `CONFIG_SCHED_CPULOAD_SYSCLK` (CPU load tracking, off by default)
  and `CONFIG_FS_PROCFS_EXCLUDE_CPULOAD` cleared, on top of its own
  `NSH_DISABLE_TOP` setting.
- `CONFIG_TTY_SIGINT` is required for Ctrl+C to work at all in NSH (e.g. to exit
  `top`) -- it's only auto-enabled for console devices, and USART6 here is not
  the console.
- `CONFIG_SYSTEM_READLINE` is a `menuconfig` that `nshlib`/`nsh` Kconfig normally
  pulls in via `select` -- but `kconfig-tweak` edits `.config` as text and never
  resolves `select` dependencies (same reason the `NSH_PROMPT_*`-style defaults
  have to be spelled out explicitly), so it must be requested directly here too.
- `CONFIG_READLINE_ECHO` controls only one thing: whether `readline()` actively
  sends a backspace + VT100 clear-to-EOL to erase a character on the terminal
  when Backspace/Delete is pressed. Normal typed characters are echoed by the
  serial driver's own termios-level echo regardless of this setting -- it is
  *not* the same thing as a terminal's "local echo" setting (which must stay off,
  per Step 2 below). With `CONFIG_READLINE_ECHO` off, `readline()` still correctly
  drops the character from its internal line buffer, but sends nothing to the
  terminal, so backspace looks like it does nothing at all: the cursor doesn't
  move and the character stays on screen. Defaults to `y` upstream, but has been
  accidentally turned off before, so it's set explicitly here.
- `CONFIG_BUILTIN` and `CONFIG_NSH_BUILTIN_APPS` are both required for
  `exec_builtin()`/`nsh_builtin()` to find *any* registered builtin app by
  name at the `nsh>` prompt -- anything that isn't one of NSH's own internal
  commands (`ls`, `cat`, `help`, etc.). Miss either one and every such
  command (`pppd`, `renew`, `hcom`, `sh`, a nested `nsh`, or the
  `calib_udelay` tool described below) reports "command not found" even
  though it's compiled in and registered, while NSH's own internal commands
  and its own top-level launch (started via a direct `task_create()` call,
  not through this lookup) work fine -- easy to misdiagnose as a deeper
  problem when it's really just these two missing flags.
- `/dev/ttyS3` is what USART6 currently resolves to in this board's serial
  configuration. If you change which UARTs are enabled elsewhere in the defconfig,
  re-check this -- the `/dev/ttySx` numbering is assigned in UART-enable order, not
  fixed per peripheral.
- TX/RX pin assignment depends on the Meadow hardware platform and which UART is
  selected -- it isn't fixed. For example, on a Meadow F7FeatherV2 using USART6,
  the board's TX is pin D09 and its RX is pin D10. Wire these crossed to your
  USB-to-serial adapter (board TX -> adapter RX, board RX -> adapter TX). Only
  TX, RX, and ground are needed -- no flow control lines.

Flash the resulting build as usual.

### Optional: calibrating `CONFIG_BOARD_LOOPSPERMSEC` with `calib_udelay`

`calib_udelay` is a one-off diagnostic tool, not part of every NSH session, so
it's deliberately not included by `--nsh`. Enable it on top of `--nsh` only
when actually (re-)calibrating, by adding these `ktweak` calls (matching the
comment above `enable_nsh()` in `scripts/configuration_tweaks.sh`) before
building:

```
ktweak --enable EXAMPLES_CALIB_UDELAY
ktweak --set-val EXAMPLES_CALIB_UDELAY_NUM_MEASUREMENTS 3
ktweak --set-val EXAMPLES_CALIB_UDELAY_NUM_RESULTS 20
ktweak --set-val EXAMPLES_CALIB_UDELAY_DELAY_TEST_ITERATIONS 100000
```

The three sub-options are declared under `if EXAMPLES_CALIB_UDELAY` in
`apps/examples/calib_udelay/Kconfig`, so -- same `kconfig-tweak`-never-runs-
`olddefconfig` reason as everything in `enable_nsh()` -- they must be set
explicitly. `NUM_MEASUREMENTS`/`NUM_RESULTS` happen to have matching
`#ifndef` fallbacks in the source itself and would silently use the same
values even if left unset, but `DELAY_TEST_ITERATIONS` does not and fails the
build outright ("undeclared") if it's missing.

Run `calib_udelay` at the `nsh>` prompt and copy its recommended value into
the base `defconfig`'s `CONFIG_BOARD_LOOPSPERMSEC`. It calls `sched_lock()`
for its whole run (which can be tens of seconds), so it produces no visible
output at all until it finishes -- that's expected, not a hang.

## Step 2: Set up a terminal emulator

Any serial terminal program works -- PuTTY, Tera Term, or the Windows Terminal serial
add-on on Windows; `screen`, `minicom`, `picocom`, or CoolTerm on Mac/Linux. Connect
your USB-to-serial adapter to the board's USART6 pins, then configure the session
with:

| Setting | Value |
|---|---|
| Baud rate | 115200 |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Flow control | None |
| Local echo | **Off** |
| Line ending translation | Not needed -- leave at default |

**Local echo must be off.** The device echoes typed characters itself; if the
terminal also echoes locally, every character will appear doubled.

Pick the serial port that corresponds to your USB-to-serial adapter -- on Windows
this is a `COMn` port (check Device Manager), on Mac a `/dev/tty.usbserial-*` (or
similar) device, and on Linux typically `/dev/ttyUSB0` or `/dev/ttyACM0`.

## Step 3: Enable and use NSH

NSH is started automatically on USART6 when the `--nsh` flag is used in the build.
## Running the developer tests: `meadowtest`

`--nsh` also builds the `meadowtest` command (`apps/system/meadowtest`), which runs the
same developer tests Meadow.CLI can trigger, from the `nsh>` prompt:

```
nsh> meadowtest -p 0          # list the tests built into the system
nsh> meadowtest -p 12 -v 3    # run test 12 passing the value 3
```

- `-p <uint32_t>` selects the test and defaults to 0 (list the tests).  The test
  dispatcher takes a 16-bit ID, so values above 65535 are rejected.
- `-v <uint32_t>` is the value handed to the test and defaults to 0.
- Numbers may be decimal, octal (leading 0) or hex (leading 0x).

The command calls `hcom_developer_tests_developer()`.  With `--nsh`, that function
(`apps/system/hcom/tests/developer_tests.c`) also uses `printf` for the test list and
the test result, keyed off `CONFIG_SYSTEM_MEADOWTEST`.  Output written by an
individual test through syslog is unchanged.
