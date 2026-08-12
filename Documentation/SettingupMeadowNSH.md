# Setting up Meadow NSH

## Overview

NSH (NuttShell) is NuttX's built-in command-line shell. On Meadow F7 boards it runs
over a dedicated UART (e.g. USART6) rather than the normal debug console, so it can be
enabled independently without disturbing the main console/HCOM traffic. This guide
covers the two things needed to get it working: the `defconfig` entries, and how to
set up a terminal program to talk to it.

This assumes the codebase already contains NSH's console-setup fixes (the serial
line-discipline configuration in `apps/system/hcom/diag/hcom_diag_nsh_support.c` and
the `/proc` mount in `F7MeadowOS/hcom_nx/hcom_nx_startup_mgr.c`). This document only
covers configuration, not those source changes.

## Step 1: Add the defconfig entries

Add the block below to your board's `defconfig` file (e.g.
`nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/configs/meadow_os/defconfig`). It's
safe to paste as-is — every line is either required or an intentional choice noted
below.

```
# ---- NSH support (see Documentation/SettingupMeadowNSH.md) ----
CONFIG_FS_PROCFS=y
CONFIG_FS_PROCFS_REGISTER=y
# CONFIG_FS_PROCFS_EXCLUDE_PROCESS is not set
CONFIG_SCHED_CPULOAD_SYSCLK=y
# CONFIG_FS_PROCFS_EXCLUDE_CPULOAD is not set
CONFIG_TTY_SIGINT=y
CONFIG_NSH_ALTCONDEV=y
CONFIG_NSH_ALTSTDERR="/dev/ttyS3"
CONFIG_NSH_ALTSTDIN="/dev/ttyS3"
CONFIG_NSH_ALTSTDOUT="/dev/ttyS3"
CONFIG_NSH_DISABLE_IFCONFIG=y
CONFIG_NSH_DISABLE_IFUPDOWN=y
CONFIG_NSH_DISABLE_TIMEDATECTL=y
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
CONFIG_READLINE_CMD_HISTORY=y
CONFIG_STM32F7_USART6=y
CONFIG_USART6_RXBUFSIZE=512
CONFIG_USART6_TXBUFSIZE=512
CONFIG_SYSTEM_NSH=y
CONFIG_SYSTEM_NSH_PRIORITY=180
CONFIG_SYSTEM_NSH_STACKSIZE=8192
# ---- End NSH support ----
```

Notes:

- `ifconfig`/`ifupdown`/`timedatectl` are deliberately left disabled — the first two
  to avoid NSH fighting Meadow's own network/cloud management, the third because it
  needs kernel-internal symbols not exposed to userspace in this build. Leave them
  disabled unless someone has specifically addressed that.
- `CONFIG_FS_PROCFS_EXCLUDE_PROCESS` must be off for `ps`/`wait` to work — without
  it, those two commands silently disappear regardless of their own `NSH_DISABLE_*`
  setting.
- `top` needs `CONFIG_SCHED_CPULOAD_SYSCLK` (CPU load tracking, off by default)
  and `CONFIG_FS_PROCFS_EXCLUDE_CPULOAD` cleared, on top of its own
  `NSH_DISABLE_TOP` setting.
- `CONFIG_TTY_SIGINT` is required for Ctrl+C to work at all in NSH (e.g. to exit
  `top`) — it's only auto-enabled for console devices, and USART6 here is not
  the console.
- `/dev/ttyS3` is what USART6 currently resolves to in this board's serial
  configuration. If you change which UARTs are enabled elsewhere in the defconfig,
  re-check this — the `/dev/ttySx` numbering is assigned in UART-enable order, not
  fixed per peripheral.
- TX/RX pin assignment depends on the Meadow hardware platform and which UART is
  selected — it isn't fixed. For example, on a Meadow F7FeatherV2 using USART6,
  the board's TX is pin D09 and its RX is pin D10. Wire these crossed to your
  USB-to-serial adapter (board TX → adapter RX, board RX → adapter TX). Only
  TX, RX, and ground are needed — no flow control lines.

After editing, regenerate `.config` from the defconfig (don't hand-edit `.config`
directly) and rebuild:

```bash
rm -f nuttx/.config
nuttx/tools/configure.sh stm32f777zit6-meadow:meadow_os
make -C nuttx MEADOW_OS=1 olddefconfig
./build.sh
```

Flash the resulting build as usual.

## Step 2: Set up a terminal emulator

Any serial terminal program works — PuTTY, Tera Term, or the Windows Terminal serial
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
| Line ending translation | Not needed — leave at default |

**Local echo must be off.** The device echoes typed characters itself; if the
terminal also echoes locally, every character will appear doubled.

Pick the serial port that corresponds to your USB-to-serial adapter — on Windows
this is a `COMn` port (check Device Manager), on Mac a `/dev/tty.usbserial-*` (or
similar) device, and on Linux typically `/dev/ttyUSB0` or `/dev/ttyACM0`.

## Step 3: Enable and use NSH

NSH doesn't start automatically — it's enabled at runtime from the Meadow CLI (the
`nsh enable` diagnostic command). Once enabled, switch to your terminal session and
press Enter — you should see the `nsh>` prompt. Try `help` for the full command
list.
