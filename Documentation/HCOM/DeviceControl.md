# Device-Level Requests

## Overview

A small group of host requests deal with the device itself and not with files, Mono or tracing: asking
what the device is, restarting it, putting it into DFU mode, and setting or reading its clock. They share
a simple shape: each is a `TEXT_ACCEPTED`, a little work, and a `TEXT_CONCLUDED`, apart from the restarts.
This document describes what each does and what it returns. The protocol background is in
[WireProtocol.md](WireProtocol.md), and the dispatching is in [RequestRouting.md](RequestRouting.md).

| Request | Value | Handler | Section |
|---------|-------|---------|---------|
| `GET_DEVICE_INFORMATION` | `0x0112` | `hcom_misc_rqst_get_device_info()` | [Device information](#device-information) |
| `GET_DEVICE_NAME` | `0x011f` | `hcom_misc_rqst_get_device_name()` | [Device name](#device-name) |
| `RESTART_PRIMARY_MCU` | `0x0105` | `hcom_via_nx_host_restart_meadow()` | [Restart](#restart) |
| `ENTER_DFU_MODE` | `0x010b` | `hcom_misc_rqst_enter_dfu_mode()` | [DFU mode](#dfu-mode) |
| `RTC_SET_TIME_CMD` | `0x0303` | `hcom_via_nx_execute_rtc_set_clock()` | [Setting the clock](#setting-the-clock) |
| `RTC_READ_TIME_CMD` | `0x0304` | forwarded to the kernel | [Reading the clock](#reading-the-clock) |
| `RTC_WAKEUP_TIME_CMD` | `0x0305` | `hcom_via_nx_execute_rtc_set_wakeup_time()` | [Wake-up time](#wake-up-time) |

The device-information and name handlers are in `apps/system/hcom/nx_rqsts/hcom_misc_requests.c`. The
clock handlers run in the kernel, in `F7MeadowOS/pwrmgmt/pwrmgmt_time_support.c`, and are reached through
`/dev/upd` ([UpdInterface.md](UpdInterface.md)).

## Device Information

`GET_DEVICE_INFORMATION` replies with a single `TEXT_DEVICE_INFO` message holding a list of `Key|Value~`
pairs. The reply is built in a buffer from the device configuration (a deep copy taken for the call,
[ConfigManager.md](ConfigManager.md)) and from build constants in `hcom_nuttx_shared.h`:

| Key | Value |
|-----|-------|
| `Product` | `HCOM_DEVICE_INFO_PRODUCT`, "Meadow" |
| `Model` | `HCOM_DEVICE_INFO_MODEL`, "F7Micro" |
| `ProcessorType` | the MCU part, a build constant |
| `CoprocessorType` | the ESP32 type, a build constant |
| `OSVersion` | the Meadow.OS version (long form at protocol version 8, short form at version 7) |
| `CoprocessorVersion` | the ESP32 firmware version, only when it is known |
| `MonoVersion` | the Mono Runtime version, only when the Runtime is present (any version field non-zero) |
| `ProcessorId` | the 96-bit MCU unique ID as 12 hexadecimal bytes separated by `-` |
| `Hardware` | the hardware-revision text (for example the F7 Feather V2 or Core-Compute V2) |
| `DeviceName` | the configured device name |
| `SerialNo` | the 6-byte chip identifier as 12 hexadecimal characters |
| `WiFiMAC`, `SoftAPMac` | the ESP32's station and soft-AP MAC addresses |

A trailing newline is added to the string but removed by the send function, which stops at the first CR or
LF ([WireProtocol.md](WireProtocol.md#text-bodies)), so the message ends after the last `~`. If the two
working buffers cannot be allocated the reply is `TEXT_ERROR` "Memory allocation error. No results can be
sent". The request's `userData` is not used.

## Device Name

`GET_DEVICE_NAME` replies with `TEXT_DEVICE_INFO` containing only the configured device name, taken from
the configuration (`Device.Name` in `meadow.config.yaml`, default "MeadowF7"). If the configuration or the
name cannot be obtained the handler logs and returns without sending anything but the framing `TEXT_ACCEPTED`
and `TEXT_CONCLUDED`.

## Restart

`RESTART_PRIMARY_MCU` is the host's way to reset the device. The sequence (also described in
[HostLinkUsb.md](HostLinkUsb.md#restarts)) is:

1. `TEXT_ACCEPTED`, then `TEXT_RECONNECT` with the request's `userData` echoed back.
2. `hcom_via_nx_host_restart_meadow()` first clears the Mono "last run did not finish" flag (a deliberate
   restart is not a crash), then asks the kernel to restart.
3. The kernel sets the "restart initiated by host" flag, waits 500 ms for the reconnect message to leave,
   and resets the MCU.
4. After the restart the new HCOM instance sends `TEXT_CONCLUDED` when it first opens the USB port.

The same mechanism, with different preliminary work, serves `BULK_FLASH_ERASE`, `PART_RENEW_FILE_SYS`,
`MONO_ENABLE`, `MONO_DISABLE` and `MONO_START_DBG_SESSION`.

## DFU Mode

`ENTER_DFU_MODE` is meant to restart the device into the STM32 ROM DFU bootloader so that new firmware can
be written with `dfu-util`. As implemented, HCOM replies `TEXT_ACCEPTED`, asks the kernel to write a "DFU
requested" magic value to a fixed SRAM address, and then restarts the device **without** the host-restart
flag (`hcom_via_nx_only_restart_meadow()`): there is no `TEXT_RECONNECT` and no `TEXT_CONCLUDED`.

**Nothing in this repository reads the magic value**, so the device currently restarts normally instead of
entering DFU mode (Issue #818); the details are in [HostLinkUsb.md](HostLinkUsb.md#dfu-entry). The
request's `userData` is not used.

## The Real-Time Clock Requests

The three clock requests all reach the kernel through `/dev/upd` and need the power-management support
(`CONFIG_MEADOW_PWR_MGMT_SUPPORT`) to be built in.

### Setting the clock

`RTC_SET_TIME_CMD` carries the date and time as text in the message body (no terminator), in ISO 8601
form. `pwrmgmt_mono_cmd_time_set_clock()` accepts three forms, recognised from the characters at fixed
positions (`F7MeadowOS/misc/parse_iso8601_time.c`):

| Form | Example |
|------|---------|
| Standard | `2022-03-31T17:34:25`, optionally followed by a fraction and a UTC offset such as `+00:00` |
| Standard with a space | `2022-03-31 17:34:25` |
| Condensed | `20220331T173425Z` |

An empty body gives `-EINVAL` (logged only; the host still receives `TEXT_ACCEPTED` and `TEXT_CONCLUDED`).
On success the time is passed to NuttX with `clock_settime(CLOCK_REALTIME, ...)`, which updates the RTC
hardware, inside the same write-protect unlock that the power-management code uses so the two do not
collide on the RTC.

Limits of the current code:

- **The UTC offset and the fraction are parsed but not used.** A time given as `...+05:00` is set as
  written, as if it were UTC; the conversion is commented out as "NOT SUPPORTED AT THIS TIME". Fractions of
  a second are dropped.
- **The fields are not range-checked.** `sscanf()` only checks that six numbers were read, so for example a
  month of 13 is accepted and normalised by `mktime()`.
- **The result of `clock_settime()` is not checked**, so the handler returns success regardless.
- The request is acknowledged with `TEXT_ACCEPTED` and `TEXT_CONCLUDED` only: the host receives no message
  saying whether the clock was changed or what it was changed to.

### Reading the clock

`RTC_READ_TIME_CMD` has no body. It is forwarded to the kernel (the generic path in
[RequestRouting.md](RequestRouting.md#handler-conventions)), where `pwrmgmt_mono_cmd_time_read_clock()`
reads the RTC hardware with `up_rtc_getdatetime()` and sends one `TEXT_INFORMATION` message through the
send function the user side supplied:

```
   UTC time:2022-03-31T17:34:25+00:00
```

The offset in the reply is always zero (`+00:00`).

### Wake-up time

`RTC_WAKEUP_TIME_CMD` was designed to set the RTC alarm that wakes the device from Stop mode, from either
an ISO 8601 duration (`PT30S`, `P1DT2H`) or an absolute time. **It does nothing in the current build.**
The kernel code (`pwrmgmt_mono_cmd_time_wakeup_period()`, which is also commented "This function is
incomplete") and its case in the `/dev/upd` driver are compiled out by `HCOM_INCLUDE_ISO8601_SUPPORT`, set
to 0 in `hcom_nx_common.h`. The user-side wrapper still makes the ioctl, the driver logs "unknown upd
command", and the wrapper logs the failure; the host sees only `TEXT_ACCEPTED` and `TEXT_CONCLUDED`.

The application's own sleep and wake-up do not use this request: Mono asks for Stop mode directly with a
duration ([HostLinkUsb.md](HostLinkUsb.md#low-power-stop-mode)).

## Things to Know

- **Clock requests give little feedback.** Setting the clock and the wake-up time reply with nothing but
  the framing messages, whatever happens. Only reading the clock returns data.
- **Setting the clock ignores the time zone and fractional seconds.**
- **The wake-up request is dead.** It looks supported (it is acknowledged) but changes nothing.
- **DFU entry has no effect** (Issue #818).
- **A restart is not confirmed until the host reconnects.** The `TEXT_CONCLUDED` for a restart comes from
  the new instance, so a host that does not reconnect never sees one.
- **Device information is only as complete as the configuration.** If the configuration copy fails, the
  reply carries only the build constants (product, model, processor type, coprocessor type).

## Source Map

| Concern | File |
|---------|------|
| Device information, name and DFU handlers | `apps/system/hcom/nx_rqsts/hcom_misc_requests.c` |
| Build constants for the reply | `F7MeadowOS/include/meadow/hcom_nuttx_shared.h` |
| User-to-kernel wrappers (restart, clock) | `apps/system/hcom/nx_rqsts/hcom_via_nx_access.c` |
| Kernel clock handlers | `F7MeadowOS/pwrmgmt/pwrmgmt_time_support.c` |
| ISO 8601 parsing | `F7MeadowOS/misc/parse_iso8601_time.c` |
| Kernel restart and DFU handlers | `F7MeadowOS/hcom_nx/hcom_nx_common_utils.c`, `F7MeadowOS/meadow_os/meadow-upd.c` |
| The ISO 8601 switch | `F7MeadowOS/hcom_nx/hcom_nx_common.h` |

## Related Documents

[WireProtocol.md](WireProtocol.md), [HostLinkUsb.md](HostLinkUsb.md), [RequestRouting.md](RequestRouting.md),
[UpdInterface.md](UpdInterface.md), [ConfigManager.md](ConfigManager.md).
