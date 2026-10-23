# Kernel Start-up

## Overview

This document describes what the Meadow kernel does between reset and the moment the user-space `hcom_main`
task is started ([UserspaceStartup.md](UserspaceStartup.md)). The generic NuttX boot is unchanged; Meadow
adds work in two board hooks and in one long function that those hooks call.

| Stage | Where | What it does |
|-------|-------|--------------|
| Bootloader | `bootloader/` (STM32 HAL, not NuttX) | Runs first, handles OTA, leaves the reset reason in a battery-backed register ([Architecture.md](../Architecture.md)) |
| `stm32_boardinitialize()` | `nuttx/boards/.../stm32_boot.c` | Very early: SPI chip selects, SDRAM, USB pins |
| NuttX `up_initialize()` | vendored NuttX | Drivers, serial console |
| `board_late_initialize()` | `stm32_boot.c` | Backup domain, PWM, USB device, MPU regions, `/dev/upd`, QSPI flash, FTL |
| `hcom_nx_setup_mgr()` | `F7MeadowOS/hcom_nx/hcom_nx_startup_mgr.c` | Everything else Meadow needs in the kernel (25 steps) |
| `nx_start()` then init task | vendored NuttX | Starts `hcom_main` as the init task |

```
   reset
     |
   bootloader (BBR30 = reset reason) ---> NuttX image
     |
   stm32_boardinitialize()      SPI CS, SDRAM, USB GPIO       (no syslog allowed)
     |
   nx_start() ... up_initialize()
     |
   board_late_initialize()      PWR/BBR, PWM, USB CDC, MPU, /dev/upd, QSPI, hw version, FTL
     |
     +--> hcom_nx_setup_mgr(mtd) flash, FS, trace, config, interrupts, power, SD, ...
     |
   init task: hcom_main()       user space
```

`CONFIG_BOARD_LATE_INITIALIZE=y` is set in the Meadow configurations, and NuttX calls
`board_late_initialize()` after `up_initialize()` and just before the initial application starts.
`stm32_bringup()` exists in the board directory but is **not** called by this path; all of the board's
initialisation is in `board_late_initialize()` (its own comment says the function was copied from the
original Meadow with few changes).

## Early Board Initialisation

`stm32_boardinitialize()` runs before the scheduler's data structures exist, so it must not call `syslog()`.
The RAMLOG back end takes the scheduler lock, which dereferences a null task pointer at that point, and the
board hangs silently. The comment in the function explains that enabling `CONFIG_DEBUG_FEATURES` used to
hang the board for this reason.

It does three things, each conditional on the configuration: set up the SPI chip-select pins, initialise the
SDRAM through the FMC (`stm32_sdram_initialize()`; the Mono Runtime is later copied to its start,
[MonoLifecycle.md](MonoLifecycle.md)), and configure the USB pins (`stm32_usbinitialize()`).

## `board_late_initialize()`

The steps, in order:

| # | Step | Notes |
|---|------|-------|
| 1 | Backup SRAM and the 32 battery-backed registers | `stm32_pwr_initbkp(true)`, writable ([PlatformState.md](PlatformState.md)) |
| 2 | PWM | `stm32_pwm_setup()`; an error is logged and start-up continues |
| 3 | USB device | `board_init_usbdev()`: connects CDC/ACM, or the composite device in a `--dualcdc` build ([HostLinkUsb.md](HostLinkUsb.md), [DualCdcAcm.md](../DualCdcAcm.md)) |
| 4 | MPU: user access to the GPIO registers | `mpu_user_peripheral()` over the GPIO port range |
| 5 | MPU: Mono Runtime area in SDRAM | `mpu_configure_region()` at `HCOM_NX_MONO_RUNTIME_SDRAM_ADDR`, cacheable, user read/write |
| 6 | `/dev/upd` | `meadow_upd_initialize()`; failure is logged as critical and the function **returns**, so nothing after this runs ([UpdInterface.md](UpdInterface.md)) |
| 7 | QSPI controller | `stm32f7_qspi_initialize(0)`; no controller returns |
| 8 | Hardware version | `meadow_hw_version_find_device_ver()` reads the flash chip's identity; unknown or error returns ([PlatformState.md](PlatformState.md)) |
| 9 | Unsupported board check | The original F7 Feather V1 (a different flash chip) lights the red LED and returns (Issue #453) |
| 10 | MPU: QSPI flash region | `mpu_configure_region()` at `STM32_FMC_BANK4`, strongly ordered, user read/write |
| 11 | Flash driver | `w25qxxxjv_initialize()` for the F7 V2 and Core-Compute V2 |
| 12 | FTL | `ftl_initialize(0, mtd)` makes the flash a block device |
| 13 | `hcom_nx_setup_mgr(mtd)` | The Meadow setup, below. **A failure here is a `PANIC()`** |

Why the MPU regions are needed: the build is protected, so user code runs unprivileged and sees only the
address ranges the kernel opens. The GPIO range is opened so that managed code can toggle pins directly; the
SDRAM region holds the Mono Runtime, which executes in place; and the QSPI region is memory-mapped flash that
the runtime can read. The QSPI region is deliberately strongly ordered and not cacheable. The comment cites
ST application note AN4838 and a problem where speculative reads of the region left the QSPI busy flag set
after a transfer.

Most failures in steps 7 to 12 end the function with a log line and no panic. The device then keeps booting
without a flash file system; HCOM starts, but most requests cannot work. The log line is the only sign, and it
is in the RAMLOG, which the CLI cannot read in that state.

## `hcom_nx_setup_mgr()`

Called last from `board_late_initialize()`. Its comment markers number the steps; with
`HCOM_DIAG_INCLUDE_NX_STARTUP_SYSLOG` set, it writes "Kernel startup manager N" to syslog before each one,
which shows how far a failing start got. The steps, in the order the code runs:

| Step | What | Condition | On failure |
|------|------|-----------|-----------|
| - | Reconfigure PB4 (`MEADOW_DEBUG_NJTRST_NOT_USED_GPIO`) like the other pins | always | none |
| 1 | Refuse a null MTD | always | returns `ERROR` |
| 2 | Optionally dump the flash regions | `HCOM_NX_EX_FLASH_SHOW_FLASH_STATS` | none |
| 3 | `hcom_nx_exec_ex_flash_setup()`: keep a copy of the MTD for later flash commands | always | returns |
| 3b | `hcom_traverse_ku_kernel_setup()`: create the kernel-to-user FIFO ([TraceSyslog.md](TraceSyslog.md)) | `CONFIG_RAMLOG_SYSLOG` | returns |
| 4 | Syslog level from the battery-backed register | always | returns |
| 5 | Create, mount and check the flash file systems ([FlashPartitioning.md](FlashPartitioning.md)) | `CONFIG_HCOM_FILESYSTEM_INIT` (default y) | returns |
| 6 | Start the trace message pump (and the second-USB-port copy in a `--dualcdc` build) | `CONFIG_RAMLOG_SYSLOG` | logged only |
| 7 | Prepare the log file `meadow.log` | always | none |
| 8 | Read the reset reason from BBR30 and update the reset and power-cycle counters in `/meadow0/system/os_data.bin` | always | none |
| 9 | Log the reset code and counters | always | none |
| 10 | Early ESP32 set-up (message queues for error handling) | `CONFIG_MEADOW_ESPCP_MANAGER` | none |
| 11 | Read `meadow.config.yaml` ([ConfigManager.md](ConfigManager.md)) | always | no configuration object: `LOG_EMERG`, returns `ERROR` |
| 12 | Remember whether the ESP32 should be reset at start-up | ESPCP | none |
| 13 | Apply the configured trace level and trace routing to the kernel; release the config lock; check for a saved fault code; set the clock to the OS build time | always | none |
| 14 | Mount procfs | `CONFIG_FS_PROCFS` and `CONFIG_SYSTEM_NSH` | returns |
| 17, 18 | Prepare QSPI flash tests, SD card tests | test builds only | returns |
| 19 | `meadow_interrupt_setup()` ([GpioInterrupts.md](GpioInterrupts.md)) | always | returns |
| 20 | `meadow_power_mgmt_initialize()` | `CONFIG_MEADOW_PWR_MGMT_SUPPORT` | logged only |
| 21 | SD card driver | `CONFIG_STM32F7_SDMMC2`, hardware has an SD socket, and the configuration enables it | logged; SD marked unsupported |
| 22 | Ethernet driver | Ethernet build, hardware supports it, and the default interface is Ethernet | `-ENODEV` is logged and written to `meadow.log`; other errors return |
| 23 | Cell module | `CONFIG_NETUTILS_PPPD` and the default interface is Cell | none |
| 24 | Idle monitor | `CONFIG_ARCH_IDLE_CUSTOM` | returns |
| 25 | ESP32 late initialisation and entry to run mode | ESPCP, and the configuration asks for an ESP32 reset | `LOG_EMERG`, returns |

The step numbers 15 and 16 are placeholders: the code prints the progress message for them with nothing
between. The numbering in the table is the numbering in the syslog messages, so a log stopped at "Kernel
startup manager 21" means the SD card step did not complete.

### Ordering constraints

The order is not arbitrary; several comments in the code record why:

- **The flash set-up precedes everything that touches the flash.** `hcom_nx_exec_ex_flash_setup()` only keeps
  the MTD pointer, but it must be done before the file system code, because that code reaches the block driver
  to copy the Mono Runtime from flash to RAM.
- **The file system comes before the configuration**, "so config file can be read by others".
- **The trace pump comes after the file system** because the trace settings and the log file live there, and
  **before the configuration read** so that messages produced while the configuration is parsed can be seen.
- **The early ESP32 set-up comes before the configuration read**, in case the configuration file has errors
  that need to be reported to the coprocessor's message queues.
- **The syslog level from the battery-backed register is applied first**, before the YAML file has been read, so
  the first mask is from the register alone. The configured level is merged in at step 13
  ([TraceSyslog.md](TraceSyslog.md)). Kernel space and user space keep separate masks in a protected build, so
  each side must apply the configured level itself.
- **The system clock is raised to the build time** if it reads earlier (`hcom_nx_config_set_time_to_os_build_time()`),
  so that TLS certificate checks have a plausible date before any real time source exists. A later, more
  accurate time can replace it (NTP or `RTC_SET_TIME_CMD`).

### What is not done here

- **Starting hcom.** The kernel does not start HcomRecv or HcomProc; NuttX's own `nx_start()` runs the init task,
  `hcom_main`, after this function returns ([UserspaceStartup.md](UserspaceStartup.md)).
- **The Mono Runtime.** The Runtime is copied to SDRAM and started from user space
  ([MonoLifecycle.md](MonoLifecycle.md)).
- **Anything outside the flash-backed state.** The `hcom_nx_setup_mgr()` steps for the SD card, Ethernet and
  Cell are only the driver set-up; their own behaviour is outside this series.

## Restarts

Two kernel functions in `F7MeadowOS/hcom_nx/hcom_nx_common_utils.c` restart the device for HCOM:

| Function | Action |
|----------|--------|
| `hcom_nx_common_utils_host_restart_meadow()` | Sets `HCOM_BBREG_RESTART_INITIATED_BY_HOST_CMD_BIT`, sleeps 500 ms, calls `up_systemreset()` |
| `hcom_nx_common_utils_only_restart_meadow()` | Sleeps 500 ms, calls `up_systemreset()` |

The sleep gives the `TEXT_RECONNECT` message time to leave the USB port. The flag makes the next start's HCOM
send `TEXT_CONCLUDED` ([DeviceControl.md](DeviceControl.md#restart)). Neither function returns.

Separately, `CONFIG_BOARD_RESET_ON_ASSERT` is 2 in the main configuration, so an assertion in a user or kernel
thread resets the board ([PlatformState.md](PlatformState.md)). Setting it to 0 while debugging stops the board from rebooting after
a crash and so makes the crash visible.

## Things to Know

- **Kernel start-up has no host connection.** The USB device is created and connected in step 3, but nothing
  talks to the host until user-space HCOM starts. A failure in the kernel stages is visible only on the syslog
  UART or in the RAMLOG.
- **Failure handling is uneven.** A negative result from `hcom_nx_setup_mgr()` is a `PANIC()`, but failures in
  the earlier steps of `board_late_initialize()` just return and let the boot continue half-initialised.
- **One failure stops the rest.** `board_late_initialize()` returns on the first error among steps 6 to 12,
  and `hcom_nx_setup_mgr()` returns on the first failing step, so one early failure (for example a bad file
  system) means no interrupt service, power management or ESP32 start.
- **The F7 Feather V1 is not supported** (Issue #453). On it the kernel lights the red LED, sets up no flash or
  file system, and returns; `hcom_main` then runs without them and answers every request with a "not supported"
  error ([UserspaceStartup.md](UserspaceStartup.md)).
- **`stm32_bringup()` is not called** in this configuration, because `board_late_initialize()` does the work.

## Source Map

| Concern | File |
|---------|------|
| Board hooks, MPU regions, QSPI and FTL | `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_boot.c` |
| The setup manager | `F7MeadowOS/hcom_nx/hcom_nx_startup_mgr.c` |
| Restart and trace-level helpers | `F7MeadowOS/hcom_nx/hcom_nx_common_utils.c` |
| Reset counters and OS information | `F7MeadowOS/meadow_os/meadow_os_info.c` |
| Idle monitor | `F7MeadowOS/specialized/meadow_idle_monitor.c`, `nuttx/boards/.../src/stm32_idle.c` |
| Board options (`HCOM_FILESYSTEM_INIT`, ...) | `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/Kconfig` |
| Configurations | `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/configs/` |

## Related Documents

[Architecture.md](../Architecture.md), [Kernelspace.md](../Kernelspace.md), [Memory.md](../Memory.md),
[UserspaceStartup.md](UserspaceStartup.md), [UpdInterface.md](UpdInterface.md),
[FlashPartitioning.md](FlashPartitioning.md), [ConfigManager.md](ConfigManager.md),
[PlatformState.md](PlatformState.md), [TraceSyslog.md](TraceSyslog.md).
