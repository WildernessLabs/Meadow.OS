# Platform State: Battery-Backed Registers, Hardware Version and Fault Reports

## Overview

Three small mechanisms give Meadow.OS knowledge that outlives a single run, or that is a fact about the board
it is running on:

| Mechanism | Holds | Lifetime |
|-----------|-------|----------|
| **Battery-backed registers (BBR)** | Flags and a syslog mask used by HCOM, the reset reason, a fault word | Survive a reset; cleared on power loss (unless a coin cell holds the backup domain) |
| **Backup SRAM** | One 4 KB fault message | Same |
| **Hardware version** | Which board this is | Re-detected at every start |

Reset and power-cycle **counters** live in a file, `/meadow0/system/os_data.bin`.

This document describes each, the helpers used to read and write them, and how a fault is reported at the next
start. It is the reference for the bit numbers used throughout the other documents.

## The Backup Domain

The STM32F7 has a block of memory that keeps its contents through a reset and, with a battery on the VBAT pin,
through power loss: 32 backup registers of 32 bits (`RTC_BK0R` to `RTC_BK31R`, in the RTC) and 4 KB of backup SRAM
(`CONFIG_STM32F7_BKPSRAM`, at 0x40024000). Board start-up enables them with `stm32_pwr_initbkp(true)`
([KernelStartup.md](KernelStartup.md)). Without a battery or coin cell the contents are lost at power-on and the
registers read 0.

| Register | Owner | Use |
|----------|-------|-----|
| `BK0` | NuttX | The RTC "initialised" magic value. **Do not use it** (`hcom_bbreg_defn.h` warns) |
| `BK29` | Fault handling | The fault status word, below |
| `BK30` | Bootloader to OS | The reset reason, below |
| `BK31` | HCOM | Flags and the syslog mask, below |

Other registers are not used by the code in this repository (the OTA process, outside this series, uses its own
state). The Mono runtime reaches any register by number through the helpers below.

### `BK31`: the HCOM flags

`HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER` is `BK31`. Its bits (`hcom_bbreg_defn.h`):

| Bits | Name | Meaning | Set by | Cleared by |
|------|------|---------|--------|-----------|
| 0x0000ff | syslog mask | The priority mask, bits 0 to 7 = `LOG_EMERG` to `LOG_DEBUG` | Kernel at power-on; the CLI trace-level request | Rewritten, never cleared ([TraceSyslog.md](TraceSyslog.md)) |
| 0x000100 | `RESTART_INITIATED_BY_HOST_CMD` | The last restart was a host request, so the new run owes the host a `TEXT_CONCLUDED` | Kernel, just before the reset ([DeviceControl.md](DeviceControl.md#restart)) | HCOM, when it sends that message |
| 0x000200 | `ROUTE_TRACE_MSG_TO_HOST` | Send the log to the host | Host request | Host request |
| 0x000400 | `ROUTE_TRACE_MSG_TO_UART` | Send the log to the UART | Host request | Host request |
| 0x000800 | `USER_RQST_MONO_ENABLE` | **Mono is disabled** when this bit is **set**. The name is misleading | `MONO_DISABLE`; a version mismatch | `MONO_ENABLE` |
| 0x001000 | `MONO_LAST_RUN_LOCKUP` | Mono was started and has not yet reported that it is running | HCOM, before it starts Mono | The Mono entry code once the runtime is up; a deliberate restart; HCOM at the next start, which then does not start Mono |
| 0x002000 | `MONO_DEBUGGING_START` | Start the debugger bridge on this run | `MONO_START_DBG_SESSION` | HCOM, as the bridge starts ([MonoRemoteDebug.md](MonoRemoteDebug.md)) |
| 0x004000 | `ROUTE_PROFILER_BINS_TO_UART` | The UART is reserved for profiler data | Host request | Host request |
| 0x008000 | `LAST_START_WAS_REBOOT` | This start followed a reset with the register intact, not a power-on | Kernel, at start-up | Only a power-on |

Remaining bits (0x10000 to 0x80000000) are unused. The register's initial state matters: **the whole register
reads 0 only at a true power-on.** The kernel writes the syslog mask on every start, so after that the register is
never 0 again; `LAST_START_WAS_REBOOT` exists so that user space, which runs after the kernel, can still tell
a power-on from a warm reboot.

How the bits are used is described where they matter: the trace bits in [TraceSyslog.md](TraceSyslog.md), the Mono
bits in [MonoLifecycle.md](MonoLifecycle.md) and [MonoRemoteDebug.md](MonoRemoteDebug.md).

### Access helpers

Three layers read and write the registers:

| Layer | Functions | Notes |
|-------|-----------|-------|
| Kernel | `hcom_nx_bbreg_*()` in `hcom_nx_bbreg_access.c` | Directly on `BK31`. Set, clear and clear-then-set use `modifyreg32()`, so they are safe against other tasks. The read-then-clear helper is two accesses |
| User space (HCOM) | `hcom_bbreg_*()` in `hcom_bbreg_access.c` | The same operations through `/dev/upd` ([UpdInterface.md](UpdInterface.md#the-user-side-wrappers)). The read-then-clear helper is not atomic |
| Managed code | `meadow_os_bbd_register_*()` in `meadow_os_battery_backed_domain.c`, called as direct syscalls ([UpdInterface.md](UpdInterface.md#direct-syscalls)) | Any register by number, 0 to 31; a number above 31 returns `ERROR`. Set and clear bits are a read followed by a write (not atomic) |

The same file holds the backup-SRAM helpers: `meadow_os_bbd_strdup_to_sram()` (clears the whole 4 KB, then
copies a string, at most 4095 bytes), `meadow_os_bbd_strdup_from_sram()`, and `meadow_os_bbd_clear_sram()`.

## Reset Reason and Counters

### The reason

The bootloader runs before NuttX. At start it copies the STM32's `RCC_CSR` register, which records why the chip
reset, into a variable, clears the hardware flags (so they do not linger), and later stores the saved value in
`BK30` (at address 0x400028c8). The OS reads only bits 24 to 31, shifted down by 24
(`meadow_os_reset_reason()`):

| Value | Name | Cause |
|-------|------|-------|
| 0x02 | `MEADOW_OS_RESET_BROWNOUT` | Brown-out or POR/PDR |
| 0x04 | `MEADOW_OS_RESET_RESET_PIN` | The NRST pin |
| 0x08 | `MEADOW_OS_RESET_POWER_CYCLE` | Power-on/power-down reset |
| 0x10 | `MEADOW_OS_RESET_SOFTWARE` | A software reset (`up_systemreset()`, so every host restart and assertion reset) |
| 0x20 | `MEADOW_OS_RESET_INDEPENDENT_WATCHDOG` | Independent watchdog |
| 0x40 | `MEADOW_OS_RESET_WINDOW_WATCHDOG` | Window watchdog |
| 0x80 | `MEADOW_OS_RESET_LOW_POWER` | Low-power management reset |

Several bits can be set at once (a power-on usually sets brown-out, pin and power-cycle together). The kernel logs
the code at start-up (step 9, [KernelStartup.md](KernelStartup.md)).

### The counters

At step 8 of the start-up manager, `meadow_os_reset_update_counters()` reads `/meadow0/system/os_data.bin`
(a small structure with a version, the reset count and the power-cycle count), updates it and writes it back:

- the **reset count** is incremented at every start;
- the **power-cycle count** is incremented when the reason has any of the brown-out, power-cycle or low-power
  bits, when the reason is 0, or when the count is still 0.

A missing file is created. The values are logged ("Reset cycle count: N, Power cycle count: M"), and available
through `meadow_os_reset_cycle_count()` and `meadow_os_power_cycle_count()`. If the file system cannot be opened,
the function fails and both counts stay 0. The 4 KB "OS persisted" flash partition described in
[FlashPartitioning.md](FlashPartitioning.md) is not used for this.

## The Hardware Version

`meadow_hw_version_find_device_ver()` (`F7MeadowOS/meadow_os/meadow_hw_version.c`) runs once, from the board's
late initialisation, before the flash driver, because the flash driver depends on the answer
([KernelStartup.md](KernelStartup.md)).

```
   read the 4 version pins (PC15, PC14, PG3, PC13, each with a pull-up)
        |
        +-- 0x01  ------------------------------------------> F7 Core-Compute V2
        |
        +-- 0x0f (all high: not connected) --> read the flash chip ID (command 0x9f)
        |        +-- Spansion  0x016019 ----------------------> F7 Feather V1 (unsupported)
        |        +-- Winbond   0xEF4020 (Q) or 0xEF7020 (M) --> F7 Feather V2
        |        +-- anything else ---------------------------> unknown
        |
        +-- any other pattern ------------------------------> error
```

The pins were added with the Core-Compute module; boards before it have them unconnected, so the pull-ups read
high and the chip identifies the board. After the read the pins are returned to floating inputs to save power.

| Number | Name | Flash | Ethernet | SD card |
|--------|------|-------|----------|---------|
| 0 | Unknown | | | |
| 1 | `F7FeatherV1` | 32 MB Spansion, **not supported** (Issue #453) | | |
| 2 | `F7FeatherV2` | 64 MB Winbond | no | no |
| 3 | `F7CoreComputeV2` | 64 MB Winbond (same chip) | yes, in an Ethernet build | yes, in a build with SDMMC2 |

`-1` is the error value. The version is available with `meadow_hw_version_get()`, its name with
`meadow_hw_version_string_return()`, and to user space through `/dev/upd` (`HCOM_NX_UPD_GET_HW_VERSION`). It appears
as `Hardware` in the device information ([DeviceControl.md](DeviceControl.md#device-information)).

Things the code decides from it: whether a board can run at all (the V1 and an unknown or error result stop the
boot, [KernelStartup.md](KernelStartup.md)); the size of the QSPI MPU region and which flash driver to use;
whether the Ethernet and SD card drivers start; and whether the SD setting in `meadow.config.yaml` is honoured
([ConfigManager.md](ConfigManager.md)).

## Fault Reports

### The status word

`BK29` holds a fault status word built from two identical byte-wide groups, one for the OS (bits 0 to 7) and one
for the Mono runtime (bits 8 to 15; `FAULT_LOGGING_RT_BIT_SHIFT` is 8), defined in `hcom_shared_common.h`:

| Bit in the group | Meaning |
|------------------|---------|
| 0 | This component faulted |
| 1, 2 | Phase 1 (writing the message to backup SRAM) started, completed |
| 3, 4 | Phase 2 (writing it to persistent storage) started, completed |
| 5 | An error writing the crash file (runtime group only) |

The two-phase design copes with a fault that interrupts the report: the message first goes to backup SRAM
quickly, and the slower write to a file is done at the next start.

### At the next start

`meadow_os_fault_handler_check_fault_code()` runs at step 13 of the start-up manager
([KernelStartup.md](KernelStartup.md)). If `BK29` is non-zero it:

1. logs the status word;
2. for an **OS** fault: logs which phases completed, and writes the lines of the saved message that begin with
   `up_` or `arm_` (the NuttX assertion and register dump) to syslog and to `/meadow0/crash/oscrash_report.txt`;
3. for a **runtime** fault: logs the phases, notes a crash-file error if flagged, and logs the saved message;
4. clears the backup SRAM and sets `BK29` to 0.

### What writes the status

- **Runtime faults** are written by the Mono runtime, which is outside this repository.
- **OS faults.** Nothing in this tree writes them. The writer that Legacy had
  (`meadow_os_fault_handler_save_os_state()`) was never called after the rebase and was removed (Issue #801);
  recording OS faults is tracked in Issue #800. The reader above still handles the OS bits. An OS assertion resets the board (`CONFIG_BOARD_RESET_ON_ASSERT`, see
  [KernelStartup.md](KernelStartup.md#restarts)); no OS crash report is recorded.

## Things to Know

- **Registers are not zero after the first start.** Test the dedicated bit, not the whole register, to tell a
  power-on from a reboot.
- **Bit 0x800 means "disabled".** Its name says "ENABLE".
- **Hard resets keep the registers; power loss does not.** A board without a coin cell forgets the trace
  settings, the Mono-disabled flag and the lock-up flag at every power-off.
- **The user-side helpers are not atomic.** The "test then clear" helper reads and then writes with a gap, and the
  managed-code helpers do the same.
- **A failed chip-ID read gives an odd version.** `meadow_hw_version_from_flash_chip()` returns the negative errno
  from a failed read as the version, which is neither "unknown" nor the error value; the boot then ends at the
  flash-driver choice with an "Unknown MTD" message.
- **Detection does not check the board.** The Core-Compute is recognised by its pins, and Ethernet or SD
  support is assumed from that, not tested (a comment in the code says so).
- **The OS fault recorder is dormant.** Treat the crash report as a runtime feature only.

## Source Map

| Concern | File |
|---------|------|
| Register assignments and `BK31` bits | `F7MeadowOS/include/meadow/hcom_bbreg_defn.h` |
| Kernel BBR helpers | `F7MeadowOS/hcom_nx/hcom_nx_bbreg_access.c` |
| User-space BBR helpers | `apps/system/hcom/nx_rqsts/hcom_bbreg_access.c` |
| Managed-code register and SRAM access | `F7MeadowOS/meadow_os/meadow_os_battery_backed_domain.c` |
| Reset reason and counters | `F7MeadowOS/meadow_os/meadow_os_info.c`, `F7MeadowOS/include/meadow/meadow_os.h` |
| Bootloader's reset reason | `bootloader/Core/Src/main.c` |
| Hardware version | `F7MeadowOS/meadow_os/meadow_hw_version.c`, `F7MeadowOS/include/meadow/meadow_hw_version.h` |
| Fault handling | `F7MeadowOS/meadow_os/meadow_os_fault_handling.c`, `F7MeadowOS/include/meadow/hcom_shared_common.h` (`FAULT_LOGGING_*`) |

## Related Documents

[KernelStartup.md](KernelStartup.md), [UpdInterface.md](UpdInterface.md), [TraceSyslog.md](TraceSyslog.md),
[MonoLifecycle.md](MonoLifecycle.md), [FlashPartitioning.md](FlashPartitioning.md),
[Memory.md](../Memory.md), [Architecture.md](../Architecture.md).
