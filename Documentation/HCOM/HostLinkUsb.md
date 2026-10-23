# The USB Host Link

## Overview

All HCOM traffic travels over one USB cable. The STM32F7's on-chip USB OTG full-speed controller
presents a **CDC/ACM** (virtual serial port) device to the host, and the device exposes it to
user space as `/dev/ttyACM0`. The `hcom` application opens that node twice, once to read and once
to write, and everything in [WireProtocol.md](WireProtocol.md) is carried as bytes over it.

```
   Host PC / Mac                                   Meadow.OS
   +----------------------+                        +---------------------------------------+
   | Meadow.CLI           |                        |  user space: HCOM task                |
   | (serial port with    |  USB cable, CDC/ACM    |    HcomRecv   <-- fd 1: read          |
   |  vendor ID 0x2E6A)   |<---------------------->|    senders    --> fd 2: write         |
   +----------------------+                        |         both are /dev/ttyACM0         |
                                                   |  --------------------------------     |
                                                   |  kernel: NuttX CDC/ACM class driver   |
                                                   |          STM32F7 OTG FS controller    |
                                                   +---------------------------------------+
```

This document covers how the USB device is created at boot, how it identifies itself, how HCOM
opens and holds the port, and what happens to the link when the device restarts, sleeps or is put
into DFU mode. Byte-level framing is in [WireProtocol.md](WireProtocol.md); how the two descriptors
are used is in [ReceivePipeline.md](ReceivePipeline.md) and [SendPipeline.md](SendPipeline.md).

## How the USB Device Comes Up

The USB device is created by the board code in the kernel, well before the HCOM task exists.

| Step | Where | What happens |
|------|-------|--------------|
| 1 | `stm32_boardinitialize()` (`stm32_boot.c`), very early | `stm32_usbinitialize()` configures the OTG FS VBUS-sense GPIO. The controller has an internal soft pull-up, so nothing else is needed. |
| 2 | `board_late_initialize()` (`stm32_boot.c`), after the backup domain and PWM | `board_init_usbdev()` runs when `CONFIG_CDCACM` is set. |
| 3 | `board_init_usbdev()` | Calls `boardctl(BOARDIOC_USBDEV_CONTROL, ...)` to connect a CDC/ACM device, instance 0. NuttX registers it as `/dev/ttyACM0`. |
| 4 | Later in `board_late_initialize()` | The rest of the platform comes up (`/dev/upd`, QSPI flash, the file system, `hcom_nx_setup_mgr()`). |
| 5 | NuttX starts `hcom_main` | The HCOM task then opens `/dev/ttyACM0` (see below). |

So the host can see and enumerate the device before HCOM is running. If `board_init_usbdev()`
fails, the error is logged (`board_init_usbdev failed`) and the boot continues with no USB link.

`board_init_usbdev()` has three branches. The shipping configuration uses the stand-alone CDC/ACM
branch. With `CONFIG_USBDEV_COMPOSITE` (the developer-only dual-port build, see
[below](#the-dual-port-developer-build)) it initialises and connects a composite device instead.
A third, PL2303 branch exists but cannot be reached, because the function is only called when
`CONFIG_CDCACM` is set.

### Relevant configuration

The shipping `meadow_os` defconfig sets:

| Option | Value |
|--------|-------|
| `CONFIG_STM32F7_OTGFS` | `y` (the USB OTG full-speed controller) |
| `CONFIG_USBDEV` | `y` |
| `CONFIG_CDCACM` | `y` |
| `CONFIG_BOARDCTL`, `CONFIG_BOARDCTL_USBDEVCTRL` | `y` (lets board code connect the device through `boardctl()`) |
| `CONFIG_BOARD_USBDEV_SERIALSTR` | `y` (the board supplies the USB serial number string) |
| `CONFIG_CDCACM_VENDORID` / `_PRODUCTID` | `0x2E6A` / `0x0001` |
| `CONFIG_CDCACM_VENDORSTR` / `_PRODUCTSTR` | "Wilderness Labs" / "Meadow F7 Micro" |

`CONFIG_CDCACM_CONSOLE` is not set: the USB port carries HCOM, not the NuttX console.

## Device Identity

| Item | Value |
|------|-------|
| USB vendor ID | `0x2E6A` |
| Product ID | `0x0001` (single-port build); `0x0002` in the dual-port developer build |
| Vendor string | "Wilderness Labs" |
| Product string | "Meadow F7 Micro" |
| Serial number | 12 hexadecimal characters calculated from the MCU's 96-bit unique ID (`board_usbdev_serialstr()` in `hcom_nx_common_utils.c`, which calls `hcom_nx_common_utils_calc_f7_sn()`) |

Meadow.CLI treats every serial port with the Wilderness Labs vendor ID as a Meadow
(see [DualCdcAcm.md](../DualCdcAcm.md)). The baud rate and line settings a host sets on the port
are ignored; a CDC/ACM link has no real baud rate.

## How HCOM Opens the Port

The device node name is `CONFIG_HCOM_COMMS_DEVICE_NAME`, default and effectively fixed at
`/dev/ttyACM0`. (Changing the string would not create a different node; the node is named by the
CDC/ACM driver for minor number 0. The receive code's own comment says it "must be
`/dev/ttyACM0`".) `hcom_host_recv_get_device_name()` supplies the name to both directions.

HCOM keeps **two** descriptors open on the node, deliberately different:

| | Read descriptor | Write descriptor |
|---|-----------------|------------------|
| Opened by | the `HcomRecv` thread | whichever thread first sends |
| When | at thread start, and again after any failure | lazily, at the first send |
| Mode | `O_RDONLY` (blocking) | `O_WRONLY \| O_NONBLOCK` |
| Why | the thread has nothing else to do, so it blocks in `read()` | a blocking write would stall the caller whenever the host is not draining the port |
| Closed on | read error, and before low power | write error, and before low power |

All the descriptors belong to the HCOM task group. The send code refuses to send from any other
task group (`EMEADOW_TASK_GRP_MISMATCH`), because a descriptor number is only meaningful in the
task group that opened it.

### Connecting and reconnecting

Opening the node succeeds whether or not a host program is attached, so "connected" here means
"the node could be opened", not "a CLI is listening". The receive thread's open loop
(`hcom_host_recv_open_connection()`) retries as follows:

| Constant | Value | Meaning |
|----------|-------|---------|
| `HCOM_CONNECTION_TIMEOUT_STARTUP` | 250 ms | Delay between attempts at first |
| `HCOM_CONNECTION_STARTUP_ATTEMPTS` | 20 | Fast attempts (5 seconds' worth) per connection cycle |
| `HCOM_CONNECTION_TIMEOUT_RUNNING` | 5 s | Delay between attempts after the fast ones |

Each reconnection cycle starts with fast attempts again.

When a read fails, the thread closes the descriptor and reopens it. `ENOTCONN`, `ENOTSOCK` and
`ENETDOWN` ("the host connection dropped") add a one-second pause first, so a persistent error does
not burn CPU; other errors reopen at once. `EINTR` is ignored, and a zero-length read (end of file)
is logged and the read is simply repeated.

The first time the thread connects after boot it also checks whether this start followed a
host-requested restart, and if so sends the missing `TEXT_CONCLUDED`
(see [WireProtocol.md](WireProtocol.md#restart-requests)).

### When no host is listening

This is the normal state of a deployed device. The write descriptor still opens, and what is sent
accumulates in NuttX's buffers until they fill; after that `write()` reports `EAGAIN`. The send code
then marks the link "blocked", discards messages instead of waiting, and before each later send
writes a single `0x00` to probe whether the host has started draining the port (the byte is
harmless to a host that picks it up late). The retry budgets and the full logic are in
[SendPipeline.md](SendPipeline.md).

## Restarts

When the host asks for a restart, the device must disappear from USB and come back, and the host
has to be told to reconnect:

1. HCOM sends `TEXT_ACCEPTED` and `TEXT_RECONNECT` (echoing the request's `userData`).
2. The kernel sets the "restart initiated by host" flag in the battery-backed register
   (`HCOM_BBREG_RESTART_INITIATED_BY_HOST_CMD_BIT`), **waits 500 ms** so the reconnect message
   can get out and be processed, then calls `up_systemreset()`
   (`hcom_nx_common_utils_host_restart_meadow()`).
3. After the reset the USB controller restarts, so the host sees the port vanish and reappear.
   (On Windows both ports of a dual-port build are removed and re-added with the same numbers;
   many terminal programs stop when this happens, see [DualCdcAcm.md](../DualCdcAcm.md).)
4. When the new HCOM instance's receive thread first opens the port, it sends `TEXT_CONCLUDED`.

A restart that is not tied to a host request, such as a crash or a watchdog reset, sets no flag and
sends nothing. `hcom_nx_common_utils_only_restart_meadow()` (used for DFU entry) performs the same
500 ms wait and reset without setting the flag.

## Low-Power (Stop) Mode

The Mono runtime asks the kernel to enter Stop mode with a `/dev/upd` sleep command. In Stop mode
the USB link cannot be kept up, so the kernel first tells the HCOM modules that use it. The sequence,
from `pwrmgmt_enter_stm32f7_stop_mode()` in `F7MeadowOS/pwrmgmt/pwrmgmt_control.c`:

```
   Mono task        kernel (pwrmgmt)                HCOM                       Host
      |-- sleep ioctl -->|                           |                          |
      |                  |-- TEXT_NEXT_LOW_PWR ------(via kernel-to-user FIFO)-->|
      |                  |-- callback(start) ------->| receive: set flag,       |
      |                  |                           |   close read descriptor  |
      |                  |                           | send: wait for any send  |
      |                  |                           |   to finish, hold the    |
      |                  |                           |   send lock, close the   |
      |                  |                           |   write descriptor       |
      |                  |-- idle task: OTG transceiver powered down, Stop --->  port disappears
      |                  |                  ... sleeping ...                    |
      |                  |-- transceiver powered up, clocks restored --------->  port reappears
      |                  |-- callback(end) --------->| receive: release the     |
      |                  |                           |   thread; send: release  |
      |                  |                           |   the send lock          |
      |<-- ioctl returns-|                           |                          |
```

Details:

- The sleep time must be 2 seconds to 2,419,199 seconds (28 days less one second). A shorter request
  is skipped (`PWRMGMT_SLEEP_SKIPPED_TOO_SHORT`) and a longer one is refused with `-ETIME`.
- Three modules register for notification through `hcom_via_nx_register_pwr_mgmt_callback()`:
  the host receive code, the host send code and the Mono stdout/stderr redirect. A callback that
  returns an error aborts the sleep with `-EBUSY`.
- The kernel powers the USB OTG transceiver down on entry and up again on exit
  (`OTG_GCCFG_PWRDWN` in `pwrmgmt_enter_stop_mode.c`). This makes the serial port disappear from the
  host. Its comment says this is intentional: without it the USB serial data is corrupted across
  Stop mode.
- After wake-up, the first attempts to reopen the port often fail with `ENOTCONN`. The receive code's
  comment records that this is seen for about 250 ms (one retry delay) and then succeeds.
- The send callback holds the send semaphore for the whole sleep, so any thread that tries to send
  waits until the end notification.

## DFU Entry

`HCOM_MDOW_REQUEST_ENTER_DFU_MODE` is meant to restart the device into the STM32 ROM DFU bootloader,
so the firmware can be written with `dfu-util` (see [Debugging.md](../Debugging.md)). As implemented:

1. HCOM sends `TEXT_ACCEPTED`, then asks the kernel to enter DFU mode.
2. The kernel writes the value `0x1c0ffee2` to SRAM address `0x2004FFF0`
   (`MEADOW_ENTER_DFU_MODE_MAGIC_NUMB` and `_MEMORY_ADDR`, `hcom_dfu_mode_entry.h`).
3. HCOM asks for a restart, and the kernel waits 500 ms and resets.

No `TEXT_RECONNECT` and no `TEXT_CONCLUDED` follow; the device leaves HCOM.

**Nothing in this repository reads the magic value.** In the Legacy Meadow code, a check at the start
of NuttX's reset handler jumped to the ROM bootloader when it found the value. That check is not in
the rebased `nuttx/arch/arm/src/stm32f7/stm32_start.c`, and the in-tree bootloader does not look for
it either, so today the request restarts the device normally. It is tracked as Issue #818. Putting
the board into DFU mode with the BOOT0 button is not affected.

## The Dual-Port Developer Build

`build.sh --dualcdc` replaces the stand-alone CDC/ACM device with a composite USB device that has two
CDC/ACM interfaces. [DualCdcAcm.md](../DualCdcAcm.md) covers it fully. What matters here:

- `/dev/ttyACM0` is still HCOM. `/dev/ttyACM1` carries syslog, or NSH with `--nsh`.
- The macro `HCOM_USB_PORT2_USAGE` in `hcom_shared_common.h` (none, syslog or NSH) is derived from the
  build configuration and is used by the NSH launcher and the trace-over-USB thread.
- The product ID changes to `0x0002`, and `board_init_usbdev()` initialises and connects the composite
  device, using configuration 0 built by `board_composite0_connect()` in `stm32_composite.c`.
- The host can then see two serial ports with the same vendor ID, so Meadow.CLI must be pinned to the
  HCOM port.

## Things to Know

- **The "connected" state means only that the node opened.** A successful `open()` does not mean a CLI
  is listening, and the send side learns whether anyone is reading only by writing and seeing whether
  it blocks.
- **A zero-length read is retried immediately.** The receive loop logs it at `LOG_INFO` (which the
  default syslog mask hides) and reads again with no delay. If a driver ever returned zero repeatedly,
  the thread would loop without sleeping.
- **The power-management callbacks close descriptors from the sleep requester's context, which is
  not obviously HCOM's.** The sleep request arrives as a `/dev/upd` ioctl made by the Mono task, and
  the kernel runs the registered callbacks on that same task. Descriptor numbers are resolved against
  the *calling* task's table, and this build sets `CONFIG_FDCLONE_STDIO=y`, so a new task inherits only
  descriptors 0 to 2. The callbacks then call `close()` on HCOM's descriptor numbers and set them to
  `-1` regardless. The code comments acknowledge the constraint ("the fd must still be closed by a
  member of this Task Group... otherwise the fd will be orphaned"). Whether each Stop cycle in
  practice leaves one read and one write descriptor open has not been verified on hardware; this is
  the same class of problem as the earlier `meadow_interrupt.c` and traverse-FIFO fixes.
- **DFU entry has no effect** (Issue #818).
- **Reset timing is fixed.** Both restart helpers sleep a fixed 500 ms to let the last message leave
  the device; nothing confirms that it did.
- **USB is brought up before the file system and HCOM.** A host can enumerate and open the port while
  the device is still booting, before HCOM is reading from it.
- **A PL2303 branch in `board_init_usbdev()` is dead code**, as noted above.

## Source Map

| Concern | File |
|---------|------|
| USB device creation, boot order | `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_boot.c` |
| VBUS GPIO | `.../src/stm32_usb.c` |
| Composite (dual-port) device | `.../src/stm32_composite.c` |
| USB identity strings | `.../configs/meadow_os/defconfig`; serial number in `F7MeadowOS/hcom_nx/hcom_nx_common_utils.c` |
| Opening, reading and reconnecting | `apps/system/hcom/comms/hcom_host_receive.c` |
| Writing, blocked-host handling | `apps/system/hcom/comms/hcom_host_send.c` |
| Connection timing constants | `apps/system/hcom/hcom_common.h` |
| Restart and DFU helpers | `F7MeadowOS/hcom_nx/hcom_nx_common_utils.c`, `F7MeadowOS/meadow_os/meadow-upd.c` |
| Low-power notification and USB power-down | `F7MeadowOS/pwrmgmt/pwrmgmt_control.c`, `pwrmgmt_enter_stop_mode.c` |

## Related Documents

[WireProtocol.md](WireProtocol.md), [ReceivePipeline.md](ReceivePipeline.md),
[SendPipeline.md](SendPipeline.md), [KernelStartup.md](KernelStartup.md) (the boot order around USB),
[DualCdcAcm.md](../DualCdcAcm.md), [Memory.md](../Memory.md).
