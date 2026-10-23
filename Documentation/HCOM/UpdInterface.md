# The User-to-Kernel Interface

## Overview

Meadow.OS is built as a NuttX **protected** build: the kernel and the user-space applications are separate
images, and user code cannot call kernel functions directly. HCOM, which runs in user space, still needs
the kernel for things only the kernel can do: the battery-backed registers, the RTC, restarting the MCU,
flash partitions, power management callbacks. Two mechanisms carry those calls across:

| Mechanism | Used for | Defined in |
|-----------|----------|-----------|
| **The `/dev/upd` driver** and its `ioctl()` commands | HCOM's requests, and the Mono runtime's hardware access | `F7MeadowOS/meadow_os/meadow-upd.c`, `F7MeadowOS/include/meadow/hcom_upd_shared.h` |
| **Direct syscalls** | Newer peripherals (for example the DAC and the interrupt service) | `F7MeadowOS/include/meadow/meadow_syscall_support.h`, `nuttx/syscall/syscall.csv` |

This document is mostly about the first. It describes the driver, its two command sets, the user-side
wrappers HCOM uses, and the path by which a host request is forwarded into the kernel and text comes back.

```
     user space (HCOM task group)                     kernel
   +------------------------------+          +----------------------------+
   | hcom_via_nx_*() wrappers     |  ioctl   | upd_ioctl() switch         |
   |   hcom_bbreg_*() helpers     |--------->|   HCOM_NX_UPD_*  (0x7000+) |
   |   (one fd per task)          | /dev/upd |   MUPD_*         (0x0000+) |
   +------------------------------+          |        |                   |
                                             |        v                   |
   Mono runtime ---- MUPD_* ---------------->|  hcom_nx_* / pwrmgmt_* /   |
                                             |  stm32_* functions         |
                                             +----------------------------+
```

## The `/dev/upd` Driver

`meadow_upd_initialize()` registers the driver with
`register_driver("/dev/upd", &g_driver_operations, 0666, NULL)`. The file operations are `open`, `close`,
`read` and `ioctl`; only `ioctl` does real work. The mode is 0666, so any task can open it.

**One driver, two command sets.** The `ioctl` switch handles both:

| Set | Prefix | Values | Used by | Notes |
|-----|--------|--------|---------|-------|
| Mono-facing | `MUPD_*` | 0x0000 block | The Mono runtime (managed code through the runtime's native layer) | The numbers are part of the .NET-side ABI and must not change. Covers ESP32, I2C, SPI, directory enumeration, register access, power reset and sleep, and configuration |
| HCOM | `HCOM_NX_UPD_*` | `HCOM_NX_UPD_BASE` (0x7000) and up | HCOM | Documented below. The code under `CONFIG_MEADOW_HCOM` |

The HCOM set was once a separate driver, `/dev/nxupd`, and was merged into `/dev/upd`. The base of 0x7000 was
chosen to stay clear of the ioctl number blocks NuttX reserves (0x0100 to 0x4600, 0x8b00 and 0xff00). The
header says to re-check this when rebasing onto a new NuttX version.

**Calling convention.** The comment in the driver notes that the ioctl "puts any returned value into errno
and the returned int is set to -1" on failure. In practice that is the usual NuttX behaviour: a handler
returns a negative value, the `ioctl()` library call returns -1, and `errno` holds the error number. The
wrappers therefore return `-errno` on failure. A handler that returns `OK` gives 0.

**Unknown commands.** Anything not handled reaches `default:`, which logs `unknown upd command:%d` and
returns `ERROR`.

## The HCOM Command Set

All values are `HCOM_NX_UPD_BASE` plus the offset shown. "Wrapper" is the user-side function in
`apps/system/hcom/nx_rqsts/hcom_via_nx_access.c` (or `hcom_bbreg_access.c`).

| Offset | Command | Argument | Kernel action | Wrapper |
|--------|---------|----------|---------------|---------|
| 0x00 | `SET_BBR_VALUE` | `bbr_value` | Writes the battery-backed register `RTC_BK31R` | `hcom_via_nx_set_bbr()` |
| 0x01 | `GET_BBR_VALUE` | `bbr_value` | Reads it | `hcom_via_nx_get_bbr()` |
| 0x02 | `UPDATE_BBR_VALUE` | `bbr_update` (clear mask, set mask) | Clears then sets bits with `modifyreg32()` | `hcom_via_nx_update_bbr()`, `_alt()` |
| 0x10 | `GET_MCU_ID` | 12-byte buffer | `stm32_get_uniqueid()`, the 96-bit unique ID | `hcom_via_nx_get_mcu_id()` |
| 0x11 | `GET_MCU_SER_NUMB` | `mcu_ser_numb` | The 6-byte chip identifier as text | `hcom_via_nx_get_mcu_ser_numb()` |
| 0x12 | `GET_HW_VERSION` | `get_hw_ver` | `meadow_hw_version_get()` | `hcom_via_nx_get_hw_version()`, `_alt()` |
| 0x20 | `IS_PART_MOUNTED` | `is_part_mounted` | `hcom_nx_fs_is_mounted()` | `hcom_via_nx_is_mounted()` |
| 0x30 | `CLI_COMMAND` | `hcom_nx_cmd_data` | Routes a host request to a kernel handler | `hcom_via_nx_forward_cli_cmd_to_nx()` |
| 0x40 | `HOST_RESTART` | none | Restart, flagged as host-initiated | `hcom_via_nx_host_restart_meadow()` |
| 0x41 | `ONLY_RESTART` | none | Restart, no flag | `hcom_via_nx_only_restart_meadow()` |
| 0x42 | `ENTER_INTO_DFU_MODE` | none | Writes the DFU magic value (0x1c0ffee2) to SRAM address 0x2004FFF0 | `hcom_via_nx_put_meadow_into_dfu_mode()` |
| 0x43 | `REG_PWR_MGMT_CB` | `pwr_mgmt_cb` | Subscribes a user function to low-power notifications | `hcom_via_nx_register_pwr_mgmt_callback()` |
| 0x50 | `RTC_SET_TIME` | `rtc_set_time` | Sets the clock (power-management builds only) | `hcom_via_nx_execute_rtc_set_clock()` |
| 0x51 | `RTC_WAKEUP_TIME` | `rtc_wakeup_time` | **Compiled out**, see below | `hcom_via_nx_execute_rtc_set_wakeup_time()` |
| 0x60, 0x61 | `UPDATE_OS1`, `UPDATE_OS2` | | OS update steps (out of scope here) | |
| 0x62, 0x63 | `GET_UPDATE_FLAG`, `SET_UPDATE_FLAG` | `update_flag` | **No kernel handler** | `hcom_via_nx_get_update_state()`, `_set_update_state()` |
| 0x64 | `COPY_RUNTIME_TO_RAM` | none | Copies the Mono Runtime from QSPI flash to SDRAM | `hcom_via_nx_copy_mono_runtime_to_ram()` |
| 0x70 | `MONO_HAS_STARTED` | none | `hcom_nx_trace_msg_mono_started()`, which switches off the RAMLOG copy of the trace | `hcom_via_nx_mono_has_started_alt()` |
| 0x71 | `RESTORE_UART_CONFIG` | UART id | `upd_hcom_restore_uart_reconfig()`, puts a UART's pins back after Mono reconfigured them | `hcom_via_nx_restore_uart_reconfig()` |
| 0x80 | `EXECUTE_ESPCP_TESTS` | user value | **No kernel handler** | `hcom_via_nx_execute_espcp_tests()` |
| 0x90 | `SET_SYSLOG_MASK` | `syslog_mask` | `hcom_nx_utils_set_syslog_mask_now()` | `hcom_via_nx_set_syslog_mask()` |

Offset 0x31 was a command that has been removed; the header says not to reuse it.

The argument structures are all in `hcom_upd_shared.h`. Buffer sizes shared with the user side are
`HCOM_NX_CMD_HOST_MSG_SIZE` and `HCOM_NX_CMD_LOG_MSG_SIZE`, both 128.

### Commands with no effect

- **`RTC_WAKEUP_TIME`**: the kernel case and `pwrmgmt_mono_cmd_time_wakeup_period()` are compiled out by
  `HCOM_INCLUDE_ISO8601_SUPPORT` (0). The driver logs "unknown upd command"
  ([DeviceControl.md](DeviceControl.md#wake-up-time)).
- **`GET_UPDATE_FLAG`, `SET_UPDATE_FLAG`, `EXECUTE_ESPCP_TESTS`**: the numbers and the user-side wrappers
  exist, but there is no `case` for them in `meadow-upd.c`, so a call would fail with "unknown upd command".
  Nothing in the repository calls the three wrappers.
- **`ENTER_INTO_DFU_MODE`** does what it says, but nothing reads the value afterwards (Issue #818,
  [HostLinkUsb.md](HostLinkUsb.md#dfu-entry)).

## Forwarding a Host Request: `CLI_COMMAND`

Most host requests are handled in user space. Those that need the kernel (flash, the clock read, the trace
routing, ...) are forwarded with one generic command rather than one ioctl each
([RequestRouting.md](RequestRouting.md#handler-conventions)).

```
   HcomProc                                           kernel (ioctl, same thread)
   hcom_via_nx_forward_cli_cmd_to_nx(cmd, userData)
        |  build struct hcom_nx_cmd_data
        |    hcomCmd, userData, logLevel = LOG_NONE, logLen = 0
        |    send_host_msg = hcom_host_send_simple_string_msg
        |-- ioctl(CLI_COMMAND) ----------------------> hcom_nx_route_in_bound_cli_command()
        |                                                switch (hcomCmd) -> a kernel handler
        |                                                  handler may call (*send_host_msg)(...)
        |                                                  --> text goes straight to the host
        |<-- return ------------------------------------ handler fills logMsg/logLen on error
        |
        |  ret < 0 ? log text present -> syslog(logLevel, "<logMsg> [cli cmd:0x....]")
        |            else             -> syslog "Error detected, errno ..."
```

**Messages to the host.** The structure carries a function pointer, `send_host_msg`, set by the user side to
the simple-string sender. A kernel handler calls it to send `TEXT_INFORMATION` and similar messages to the
host while it runs. The kernel therefore calls a function in user space from inside the ioctl. This works
because the call is made synchronously on the user thread's own context, but it relies on the pointer being
valid and on the sender being safe to call there.

**Errors.** A kernel handler cannot send an error through the host link on its own. On failure it formats a
message into `logMsg` (up to 128 bytes), sets `logLevel` and `logLen`, and returns a negative value. The
user-side wrapper only **logs** it. The host is not told. The wrapper checks for a log level outside
`LOG_EMERG..LOG_DEBUG`, and a `logLen` of 128 or more (which means the kernel's `snprintf()` truncated the
message) and adds a warning for each.

Whether the host sees any outcome therefore depends on the handler: those that send text with
`send_host_msg` do; those that only return an error code do not. The framing `TEXT_ACCEPTED` and
`TEXT_CONCLUDED` messages are sent by the user-side router regardless.

**Kernel handlers.** `hcom_nx_route_in_bound_cli_command()` in
`F7MeadowOS/hcom_nx/hcom_nx_upd_route_cmd.c` switches on the host request number:

| Request | Handler area |
|---------|--------------|
| `MONO_FLASH` | Writes the Mono Runtime from `/meadow0/Meadow.OS.Runtime.bin` to QSPI flash ([FlashPartitioning.md](FlashPartitioning.md)) |
| `BULK_FLASH_ERASE`, `VERIFY_ERASED_FLASH`, `PART_RENEW_FILE_SYS` | Flash partition work ([FlashPartitioning.md](FlashPartitioning.md)) |
| Trace and profiler routing requests | Set the trace destination bits ([TraceSyslog.md](TraceSyslog.md)) |
| `RTC_READ_TIME_CMD` | Power-management builds only ([DeviceControl.md](DeviceControl.md#reading-the-clock)) |
| anything else | Fills `logMsg` with an "unknown command" text and returns -1 |

## The User-Side Wrappers

`hcom_via_nx_access.c` holds the wrappers and one static descriptor, `_nx_access_fd`, opened by
`hcom_via_nx_upd_setup()` during start-up ([UserspaceStartup.md](UserspaceStartup.md)).

**The descriptor belongs to a task.** In the protected build with `CONFIG_FDCLONE_STDIO`, a descriptor
number means something only in the fd table of the task that opened it, and a new task does not inherit
descriptors. `_nx_access_fd` is valid only for the task that ran the setup, which is the HCOM task
(`HcomProc`). Code that runs in another task must open `/dev/upd` itself with `hcom_via_nx_upd_driver_open()`
and use the variants that take the descriptor, named `..._alt`:

| `_alt` function | Used by |
|-----------------|---------|
| `hcom_via_nx_update_bbr_alt(fd, clear, set)`, through `hcom_bbreg_clear_bbr_bits_alt()` | The Mono entry code, to clear the lock-up bit |
| `hcom_via_nx_mono_has_started_alt(fd)` | The Mono entry code |
| `hcom_via_nx_get_hw_version_alt(fd)` | The Mono entry code |

Using the plain wrapper from a task that did not call the setup would pass -1 to `ioctl()` and fail.

**Battery-backed register helpers.** `hcom_bbreg_access.c` builds on `hcom_via_nx_get_bbr()` and
`hcom_via_nx_update_bbr()` to provide per-bit operations on the register (`hcom_bbreg_is_bbr_bit_set()`,
`hcom_bbreg_set_bbr_bits()`, `hcom_bbreg_clear_bbr_bits()`, `hcom_bbreg_clear_then_set_bbr_bits()` and so on). The bit assignments are in `hcom_bbreg_defn.h`
([PlatformState.md](PlatformState.md)). The helper that tests one set of bits and clears another
(`hcom_bbreg_is_bbr_bits_set_n_clear()`) does a read and then a write as two separate ioctls, so it is not
atomic against another task changing the register in between.

## Direct Syscalls

The second mechanism is the NuttX syscall table. A function is declared in `nuttx/syscall/syscall.csv`;
the build generates a user-side stub and a kernel-side dispatch entry, and the function is then called from
user space like an ordinary function. Meadow uses this for the battery-backed register and backup SRAM helpers
([PlatformState.md](PlatformState.md)), the DAC, the GPIO interrupt service
([GpioInterrupts.md](GpioInterrupts.md)), kernel tests (`meadow_kt_dispatcher()`,
[DeveloperCommandsNsh.md](DeveloperCommandsNsh.md)) and similar newer interfaces. Their prototypes and
shared constants are in `meadow_syscall_support.h`.

The two mechanisms are not interchangeable. The ioctl route needs no change to the NuttX syscall table
and keeps the Mono-side numbering stable; a syscall gives typed arguments and no driver, but adding one
means regenerating the syscall files. After editing `syscall.csv`, force a regeneration (see the project's
`nuttx-add-syscall` skill); `build.sh --wlclean` does not.

## Things to Know

- **Pointers from user space are trusted.** The driver does not validate the pointers passed in `arg`, and
  it does not check that a structure is the size it expects. NuttX's protected build does not do this for
  ioctl arguments, and every caller here is Meadow's own code.
- **`MUPD_SET_REGISTER` is an arbitrary register write.** The Mono-facing set includes a command to write a
  32-bit value to any address; anything that can open `/dev/upd` (mode 0666) can use it.
- **The kernel calls into user code.** `send_host_msg` (CLI forwarding) and the power-management callback
  pointer are user-space addresses the kernel calls directly.
- **The power-management callbacks run on the sleeper's context.** A callback registered with
  `REG_PWR_MGMT_CB` is called on the thread that asks for Stop mode, which is the Mono task, not an HCOM
  thread. It is also limited to 6 subscribers (`PWR_MGMT_MAX_CALLBACKS_AVAILABLE`). A full table returns
  `-EBADSLT`.
- **User-side errors from kernel handlers are only logged.** See the forwarding section above.
- **`hcom_via_nx_is_mounted()` returns `bool`** but passes through a negative errno on failure, so a
  failure reads as "mounted" (Issue #766).
- **Three commands are placeholders**, as listed above.
- **A new command needs both sides.** Add the number to `hcom_upd_shared.h` (never reuse 0x31), a `case`
  in `meadow-upd.c`, and a wrapper; a missing case fails silently apart from a syslog line.

## Source Map

| Concern | File |
|---------|------|
| Driver registration and the ioctl switch | `F7MeadowOS/meadow_os/meadow-upd.c` |
| Command numbers and argument structures | `F7MeadowOS/include/meadow/hcom_upd_shared.h` |
| Kernel routing of forwarded requests | `F7MeadowOS/hcom_nx/hcom_nx_upd_route_cmd.c` |
| User-side wrappers | `apps/system/hcom/nx_rqsts/hcom_via_nx_access.c` |
| Battery-backed register helpers | `apps/system/hcom/nx_rqsts/hcom_bbreg_access.c`, `F7MeadowOS/include/meadow/hcom_bbreg_defn.h` |
| Power-management subscription | `F7MeadowOS/pwrmgmt/pwrmgmt_control.c` |
| Direct syscalls | `F7MeadowOS/include/meadow/meadow_syscall_support.h`, `nuttx/syscall/syscall.csv` |

## Related Documents

[RequestRouting.md](RequestRouting.md), [DeviceControl.md](DeviceControl.md),
[UserspaceStartup.md](UserspaceStartup.md), [KernelStartup.md](KernelStartup.md),
[PlatformState.md](PlatformState.md), [TraceSyslog.md](TraceSyslog.md).
