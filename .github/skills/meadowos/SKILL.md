---
name: meadowos
description: >-
  Expert context for the Meadow.OS firmware project: NuttX RTOS protected-mode kernel
  on STM32F777ZIT6, ESP32 coprocessor (WiFi/BT), hcom host-communication app, OTA
  bootloader, and Meadow Cloud. Use when working on any aspect of this codebase —
  architecture, debugging, build, flash, protocols, or code review.
---

# Meadow.OS Firmware — Expert Context

## Project Overview

Meadow.OS is embedded firmware for the **Wilderness Labs Meadow F7Micro** development
board. It combines a custom NuttX RTOS kernel, an ESP32 Wi-Fi/BT coprocessor, a host
communication application (`hcom`), and an OTA-capable bootloader into a single
flash image (`Meadow.OS.bin`).

**Primary MCU:** STM32F777ZIT6 (Cortex-M7, 216 MHz, 2 MB internal flash, 368 KB SRAM1, 128 KB DTCM, 16 KB ITCM, 4 KB BKPSRAM)  
**Coprocessor:** ESP32 (Wi-Fi 802.11 b/g/n, BT 4.2 / BLE, runs ESP-IDF + FreeRTOS)  
**Language:** C (NuttX kernel + Meadow.OS), C++ (ESP32 firmware), C (Bootloader via STM32 HAL)

---

## Repository Structure

```
Rebase/
├── nuttx/                  # NuttX RTOS source (upstream + Meadow patches)
│   └── boards/arm/stm32f7/stm32f777zit6-meadow/
│       ├── scripts/        # Linker scripts (memory.ld, kernel-space.ld, user-space.ld, flash.ld)
│       ├── src/            # Board-specific NuttX BSP (stm32_bringup.c, stm32_appinit.c, etc.)
│       └── configs/        # NuttX Kconfig defconfigs
├── apps/                   # NuttX user-space applications
├── Meadow.OS/              # Meadow-specific OS code (kernel + user)
│   ├── include/meadow/     # All public headers — single source of truth for APIs
│   ├── meadow_os/          # OS init, GPIO, SDMMC, ESP coprocessor bridge, battery-backed domain
│   ├── hcom_nx/            # hcom NuttX integration (config mgr, startup, update routing)
│   ├── espcp/              # ESP32 coprocessor protocol (SPI + UART, WiFi, usrsocket, events)
│   ├── ethernet/           # Ethernet connectivity (connect, DHCP, monitor, ping)
│   ├── pwrmgmt/            # STM32 power management (stop mode, LSI calibration, RTC wakeup)
│   ├── timers/             # Timer management (PWM, CPU measure, frequency, RC servo)
│   ├── meadow_cloud/       # Meadow Cloud TLS/MQTT provisioning
│   ├── kerneltests/        # Kernel test suite (espcp, ethernet, power, SPI DMA, etc.)
│   └── misc/               # Miscellaneous utilities
├── esp32/                  # ESP32 coprocessor firmware (ESP-IDF)
│   ├── main/               # AppMain.cpp — FreeRTOS app entry
│   ├── components/         # F7Services (STM32 interface), esp_littlefs
│   └── sdkconfig           # ESP-IDF menuconfig snapshot
├── bootloader/             # STM32CubeIDE / HAL bootloader (Meadow.BL)
│   └── Core/Src/main.c     # OTA check, CRC verify, QSPI R/W, jump to NuttX
├── build.sh                # Master build script
├── flash.sh                # OpenOCD flash script
├── debug.sh                # OpenOCD server + GDB launcher
└── gdb/                    # GDB init files and SVD for register viewing
```

---

## Architecture

### Protected / Kernel Build

NuttX is configured as a **protected build** (`CONFIG_BUILD_PROTECTED=y`):

- **Kernel space** — NuttX kernel, drivers, Meadow.OS board code. Runs privileged.  
  Linker script: `kernel-space.ld`. Output: `nuttx.elf` / `nuttx.bin`.
- **User space** — `hcom` application and NuttX libs. Runs unprivileged.  
  Linker script: `user-space.ld`. Output: `nuttx_user.elf` / `nuttx_user.bin`.
- **MPU** enforces the boundary at runtime. User code crosses to kernel via syscalls
  defined in `meadow_syscall.h` and `meadow_syscall_support.h`.
- **Pass1 build**: kernel is configured first, then user-space libs are built against
  the exported kernel headers (`nuttx/staging/`).

### Memory Map (STM32F777ZIT6)

| Region  | Origin       | Size    | Notes                            |
|---------|-------------|---------|----------------------------------|
| FLASH   | 0x08000000  | 2048 KB | Internal — kernel + user image   |
| SRAM1   | 0x20010000  | 368 KB  | Main data RAM                    |
| DTCM    | 0x20000000  | 128 KB  | Fast tightly coupled data RAM    |
| ITCM    | 0x00000000  | 16 KB   | Instruction TCM                  |
| EXTRAM  | 0xC0000000  | varies  | External SDRAM                   |

Bootloader occupies the **first sector(s)** of internal flash. NuttX is loaded at
a fixed offset (`NUTTX_PRI_LOC`) in internal flash; the bootloader jumps to
`NUTTX_PRI_LOC + 4` (ARM vector table entry point).

### External QSPI Flash Partitions

Hardware-detected flash size: F7V1 = 32 MB, F7V2 / CCMv2 = 64 MB.

| Offset Index                          | Contents              |
|---------------------------------------|-----------------------|
| `MEADOW_FLASH_PART_OFFSET_RUNTIME`    | Mono/.NET runtime     |
| `MEADOW_FLASH_PART_OFFSET_NUTTX_UPDATE` | NuttX OS update image |
| `MEADOW_FLASH_PART_OFFSET_OTA_SPACE`  | OTA state / manifest  |
| `MEADOW_FLASH_PART_OFFSET_OS_PERSIST` | Persistent OS data    |
| `MEADOW_FLASH_PART_OFFSET_ASSERT_DATA`| Fault / assert data   |
| `MEADOW_FLASH_PART_OFFSET_UNUSED_SPACE` | Reserved            |
| `MEADOW_FLASH_PART_OFFSET_FILE_SYS`   | User filesystem (LittleFS) |

Partition definitions: `Meadow.OS/include/meadow/meadow_partition_share.h`

### Hardware Version Detection

```c
// From meadow_hw_version.h
#define MEADOW_F7_HW_VERSION_NUMB_F7V1  (1)  // 32 MB QSPI, GPIO 0x0f, Ethernet
#define MEADOW_F7_HW_VERSION_NUMB_F7V2  (2)  // 64 MB QSPI, GPIO 0x0f, Ethernet
#define MEADOW_F7_HW_VERSION_NUMB_CCMV2 (3)  // 64 MB QSPI, GPIO 0x01, no Ethernet
```

Version is detected at runtime from QSPI flash device ID and GPIO strapping pins.

---

## Key Subsystems

### hcom — Host Communication

`hcom` runs in **user space**, communicates with a host computer over USB CDC.  
Protocol: **COBS-framed binary packets** over virtual serial port.

Message types (from `hcom_protocol.h`):

| Type constant                           | Description                   |
|-----------------------------------------|-------------------------------|
| `HCOM_PROTOCOL_HEADER_ONLY_TYPE`        | Command with no payload       |
| `HCOM_PROTOCOL_HEADER_SIMPLE_TEXT_TYPE` | UTF-8 text response           |
| `HCOM_PROTOCOL_HEADER_SIMPLE_BINARY_TYPE` | Raw binary payload          |
| `HCOM_PROTOCOL_HEADER_FILE_START_TYPE`  | Begin file transfer           |

Version string format: `%d.%d.%d.%d, built DD Mon YYYY HH:MM:SS UTC (hash/ref)`.  
`HCOM_DEVICE_INFO_PROCESSOR_TYPE = "STM32F777IIK6"`, coprocessor `"ESP32"`.

NuttX-side hcom integration lives in `Meadow.OS/hcom_nx/`:
- `hcom_nx_startup_mgr.c` — application startup sequence
- `hcom_nx_config_manager.c` — YAML-based configuration (via libyaml/libcyaml)
- `hcom_nx_upd.c` / `hcom_nx_upd_route_cmd.c` — firmware update command routing
- `hcom_nx_bbreg_access.c` — battery-backed register access

### espcp — ESP32 Coprocessor Protocol

Communication between STM32 and ESP32 uses two channels:

- **SPI** — primary data channel; framed `espcp_message_t` structs  
  Max frame: `ESPCP_MAXIMUM_SPI_FRAME_SIZE`; header overhead: `ESPCP_SPI_MESSAGE_OVERHEAD`
- **UART monitor line** — control/event notifications, handled by `espcp_uart_monitor.c`

Key source files in `Meadow.OS/espcp/`:

| File                         | Responsibility                         |
|------------------------------|----------------------------------------|
| `espcp_thread.c`             | Main coprocessor communication thread  |
| `espcp_message_dispatcher.c` | Route incoming messages by type        |
| `espcp_wifi.c`               | WiFi connect/disconnect/scan           |
| `espcp_usrsock_sockif.c`     | NuttX usrsock → ESP32 TCP/IP stack     |
| `espcp_event_handlers.c`     | Async event callbacks                  |
| `espcp_core_dump.c`          | Retrieve ESP32 core dump               |
| `espcp_system.c`             | ESP32 reset, deep sleep, programming   |
| `espcp_file_system.c`        | File I/O routed through ESP32          |

Thread configuration (from `meadow_thread_config.h`):

| Thread                      | Priority | Stack     |
|-----------------------------|----------|-----------|
| `EspcpUARTMonitor`          | 190      | 8192 B    |
| `CellPPPD`                  | 200      | 4096 B    |

### Power Management

Implemented in `Meadow.OS/pwrmgmt/`:

- `pwrmgmt_enter_stop_mode.c` — STM32 STOP mode entry
- `pwrmgmt_calib_lsi_clock.c` — LSI RC oscillator calibration
- `pwrmgmt_switch_rtc_clock.c` — swap RTC to calibrated LSI
- `pwrmgmt_config_wakeup_alarm.c` / `_timer.c` — RTC-based wakeup sources
- `pwrmgmt_subscribe_for_low_pwr_notifications()` — callback API for subsystem notification before entering low-power

### Reset Reason Codes

From `meadow_os.h` (derived from STM32 `RCC_CSR` bits 31:24, shifted right by 24):

| Constant                              | Value | Cause                       |
|---------------------------------------|-------|-----------------------------|
| `MEADOW_OS_RESET_BROWNOUT`            | 0x02  | Brownout / POR              |
| `MEADOW_OS_RESET_RESET_PIN`           | 0x04  | NRST pin                    |
| `MEADOW_OS_RESET_POWER_CYCLE`         | 0x08  | POR/PDR                     |
| `MEADOW_OS_RESET_SOFTWARE`            | 0x10  | Software reset               |
| `MEADOW_OS_RESET_INDEPENDENT_WATCHDOG`| 0x20  | IWDG                        |
| `MEADOW_OS_RESET_WINDOW_WATCHDOG`     | 0x40  | WWDG                        |
| `MEADOW_OS_RESET_LOW_POWER`           | 0x80  | Low-power reset             |

---

## Build System

### `build.sh` — Key Options

| Flag              | Effect                                                        |
|-------------------|---------------------------------------------------------------|
| `--configure`     | Run `distclean` + `kconfig` for selected NuttX config        |
| `--clean`         | Clean all build artefacts before building                     |
| `--debug`         | Enable debug symbols and `CONFIG_DEBUG_*` in NuttX           |
| `--unittests`     | Enable `CONFIG_KERNEL_TESTS_SYSCALL` kernel test suite       |
| `--netcore`       | Network core build variant                                    |
| `--verbose`       | Pass `V=1` to make (full command echo)                       |
| `--wlclean`       | Wilderness Labs extended clean                               |
| `--force`         | Force rebuild even if up to date                             |

### Build Outputs

| File                   | Content                                                  |
|------------------------|----------------------------------------------------------|
| `nuttx/nuttx.elf`      | Kernel ELF with debug symbols                            |
| `nuttx/nuttx_user.elf` | User-space ELF with debug symbols                        |
| `nuttx/nuttx.bin`      | Raw kernel binary                                        |
| `nuttx/nuttx_user.bin` | Raw user-space binary                                    |
| `nuttx/Meadow.OS.bin`  | Final image: `Meadow.BL.bin` + kernel + user (srec_cat) |

CRC is computed and written to the **last 4 bytes** of `Meadow.OS.bin`.

### NuttX Configuration

NuttX is configured with `kconfig-tweak` scripts. The active defconfig is:
`nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/configs/`

After any `--configure` the `.config` file in `nuttx/` reflects the active build.

### Toolchain

The build expects `arm-none-eabi-gcc` on `PATH`. `build.sh` prepends
`toolchain/macos`, `toolchain/linux`, or `toolchain/windows` depending on `uname`.

---

## Bootloader (Meadow.BL)

Source: `bootloader/Core/Src/main.c` — STM32CubeIDE project, HAL-based.

Boot sequence:
1. STM32 HAL clock init (`SystemClock_Config`)
2. Read OTA state from QSPI flash
3. **Rollback check** — if previous update failed, restore backup image
4. **CRC verify** primary NuttX image in QSPI
5. If valid → copy to internal flash if needed; otherwise rollback
6. `BootMeadowOS()` — load MSP from vector table, jump to NuttX entry point  
   (`JumpOS = *(uint32_t*)(NUTTX_PRI_LOC + 4)`)

The bootloader is linked at `0x08000000` (start of internal flash).  
NuttX starts at the offset defined by `NUTTX_PRI_LOC`.

CDC and UART interfaces are compile-time options (`enable_bl_cdc`, `enable_bl_uart`).

---

## ESP32 Firmware

Source: `esp32/` — ESP-IDF CMake project.

`AppMain.cpp` entry:
- Starts FreeRTOS tasks: `MessageDispatcher`, `WifiRequestHandler`, BLE services
- Communicates with STM32 via `SpiTransportProvider`
- Manages NVS (non-volatile storage) via `NvsManager`

Key components:
- `components/F7Services/` — STM32↔ESP32 SPI/UART transport
- `components/esp_littlefs/` — LittleFS port for ESP32
- Build variants: `sdkconfig` (release), `sdkconfig-debug`, `sdkconfig-coredump`, `sdkconfig-heaptracing`

Flash/debug ESP32 with `esp32/flash.sh` or via `esp32/flash-all.cfg` (OpenOCD).

---

## Debugging

### OpenOCD + GDB

```bash
./debug.sh --server    # Start OpenOCD (ST-Link, stm32f7x target)
./debug.sh             # Launch gdb-multiarch (loads .gdbinit)
```

`gdb/.gdbinit` loads all three ELF symbol files and defines macros for common
debug tasks (fault dump, thread inspection, etc.).

VS Code launch config (`.vscode/launch.json`):
- Extension: **cortex-debug**
- ELF: `nuttx/nuttx.elf` (primary)
- Additional symbols: `nuttx/nuttx_user.elf`, `bootloader/Debug/Meadow.BL.elf`
- SVD: `gdb/STM32F777.svd`
- Reset sequence: `monitor reset halt` → load → `add-symbol-file -readnow` (×2) → halt

### Key Debug Facts

- Without bootloader: PC at `0x08040238`, system runs normally.
- With bootloader: PC at `0x0804023c`; incorrect vector table / VTOR setup causes lock.
- When debugging across BL + NuttX, all three ELF files **must** be loaded for
  correct symbol resolution.
- The `nuttx/System.map` and `nuttx/User.map` provide flat address→symbol lookup.

---

## Code Conventions

### Naming

| Prefix       | Domain                                      |
|--------------|---------------------------------------------|
| `meadow_`    | Meadow OS core (GPIO, SDMMC, info, config)  |
| `meadow_os_` | OS-level APIs (reset, power cycle, ESP ctrl)|
| `espcp_`     | ESP32 coprocessor protocol                  |
| `hcom_`      | Host communication protocol / NuttX glue    |
| `hcom_nx_`   | hcom NuttX-specific layer                   |
| `pwrmgmt_`   | Power management                            |
| `stm32_`     | Board BSP (NuttX board/ directory)          |
| `MEADOW_`    | Public macros / constants (uppercase)       |
| `HCOM_`      | hcom protocol macros                        |
| `ESPCP_`     | ESPCP protocol macros                       |

### Header Conventions

- All public Meadow APIs are in `Meadow.OS/include/meadow/`.
- Kernel-build headers use `#include <meadow/header.h>` (via NuttX include path).
- Guarded with `#ifndef __<PATH>__H` / `#define __<PATH>__H` pattern.
- Version tokens (`###VERSION_MAJOR###` etc.) in `hcom_nuttx_shared.h` are
  substituted by the build script before compilation.

### NuttX Patterns

- Task stacks and priorities are centralised in `meadow_thread_config.h` — **always**
  add new threads here rather than hardcoding values.
- Use `FAR` qualifier for pointers passed across the kernel/user boundary.
- Kernel tests gated with `#if defined(CONFIG_KERNEL_TESTS_SYSCALL)`.
- Syscalls crossing the protected boundary: `meadow_syscall_support.h`.

---

## Common Tasks

### Add a New Kernel Syscall
1. Declare in `Meadow.OS/include/meadow/meadow_syscall_support.h`
2. Add to NuttX `syscall.csv` in `nuttx/syscall/`
3. Implement handler in kernel space
4. Rebuild with `./build.sh --configure` then `./build.sh`

### Add a New Thread
1. Add `#define MYTHREAD_PRIORITY`, `_NAME`, `_STACK_SIZE` in `meadow_thread_config.h`
2. Call `task_create()` / `pthread_create()` in the appropriate init function

### Extend ESPCP with a New Message Type
1. Add message type enum in `espcp_shared_enums.h`
2. Add encoder in `espcp_encoders.c`
3. Add handler in `espcp_event_handlers.c` or `espcp_message_dispatcher.c`
4. Add corresponding handler in `esp32/components/F7Services/`

### Flash a Build
```bash
./build.sh              # Build all
./flash.sh              # Flash Meadow.OS.bin via OpenOCD + ST-Link
```

### Run Kernel Tests
```bash
./build.sh --unittests  # Enable CONFIG_KERNEL_TESTS_SYSCALL and build
# Connect via hcom or serial, tests run automatically on boot
```
