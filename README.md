# Meadow.OS

[![Build System](https://github.com/WildernessLabs/Meadow.OS/actions/workflows/build-system.yml/badge.svg?branch=main)](https://github.com/WildernessLabs/Meadow.OS/actions/workflows/build-system.yml)

An open-source, high-security, high-reliability POSIX-based embedded micro-RTOS built on the [NuttX](https://nuttx.apache.org/) kernel. Developed by [Wilderness Labs](https://www.wildernesslabs.co/) and targeting the custom **STM32F777ZIT6 Meadow F7** microcontroller board.

## High-Level Architecture

The system is divided into three major firmware components that run together on the target hardware:

```
+---------------------------------------------------------+
|                    User Space (uflash)                  |
|              hcom application (hcom_main)               |
|   file transfers | host comms | OTA | networking        |
+---------------------------------------------------------+
|               NuttX Kernel (kflash)                     |
|  hcom_nx | meadow_os | espcp | pwrmgmt | ethernet       |
|               MPU-enforced protected mode               |
+---------------------------------------------------------+
|              Bootloader (Meadow.BL)                     |
|        OTA update | CRC verify | rollback               |
|              STM32 HAL, 0x08000000                      |
+---------------------------------------------------------+
```

### Bootloader (Meadow.BL)

Located in the `bootloader` directory and built with a standard STM32 makefile. Resides in the first 256 KB of internal flash (`0x08000000`). Responsible for:

- Hardware initialisation (SDRAM via FMC, QSPI flash)
- Board hardware version detection (v1: Spansion QSPI, v2: Winbond QSPI)
- CRC32 verification of the primary OS image
- Performing OTA firmware updates, rollbacks, and backups using images stored on the QSPI flash
- Jumping to the NuttX OS image at `0x08040000` once checks pass

### NuttX Kernel

Located in the `nuttx` directory with board-specific support in `F7MeadowOS` and `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow`. The kernel runs in **protected mode** (`CONFIG_BUILD_PROTECTED=y`) and uses the ARM MPU to isolate kernel code from user-space code. It is built in two passes:

- **Pass 2 (kernel pass)**: builds the kernel ELF (`nuttx.bin`)
- **Pass 1 (user pass)**: builds the user-space ELF (`nuttx_user.bin`)

Key kernel subsystems added by Wilderness Labs live in `F7MeadowOS` (see [Kernelspace.md](Documentation/Kernelspace.md) for the full list):

| Directory      | Purpose                                                            |
|----------------|--------------------------------------------------------------------|
| `hcom_nx`     | Kernel-side host communication, config management, OTA routing     |
| `meadow_os`   | Core OS services, board reset, fault handling, battery-backed registers |
| `espcp`       | ESP32 WiFi coprocessor driver and `usrsock` networking stack       |
| `ethernet`    | Ethernet support via LAN8742A PHY                                  |
| `pwrmgmt`     | Custom low-power stop-mode power management                        |
| `timers`      | Timer capture, PWM, and frequency-measurement support               |
| `specialized` | Rotary encoder and frequency-measurement drivers                   |
| `ntpclient`   | Wilderness Labs fork of the NuttX ntpclient app                    |
| `meadow_cloud`| Meadow Cloud key provisioning                                      |
| `kerneltests` | In-kernel test suite                                               |
| `misc`        | Shared logging, interrupt support, utilities                       |
| `libyaml`, `libcyaml` | Vendored third-party YAML parsing libraries - do not modify |

### User Space (hcom)

Located in `apps/system/hcom`. This is the primary user-space application; its entry point is `hcom_main()`. It handles all communication with the host computer over USB, manages file transfers, and interfaces with the ESP32 WiFi coprocessor. OTA updates, cellular (PPP), and the .NET Mono runtime are present in the source tree but are **not currently started at boot** - see the "Currently Disabled Subsystems" note in [Userspace.md](Documentation/Userspace.md).

## Repository Layout

```
Rebase/
+-- bootloader/         STM32 HAL bootloader (Meadow.BL)
+-- nuttx/              NuttX kernel source tree
+-- F7MeadowOS/         Wilderness Labs kernel extensions and board support
+-- apps/               NuttX applications (hcom lives in apps/system/hcom/)
+-- esp32/              ESP32 coprocessor firmware
+-- mbedtls/            mbedTLS library for TLS support
+-- gdb/                GDB init scripts and Python helpers
+-- scripts/            Build helper scripts (common_methods.sh, etc.)
+-- Documentation/      Detailed documentation
+-- build.sh            Top-level build script
+-- flash.sh            Flash tool (OpenOCD or DFU)
+-- debug.sh            Debug launcher (OpenOCD server + GDB client)
+-- debug.cfg           OpenOCD configuration for debugging
+-- flash.cfg           OpenOCD configuration for flashing
```

## Build System

The full system is built by running `build.sh`. The script:

1. Generates build info and sets up the ARM cross-toolchain
2. Builds the bootloader via `build-bootloader.sh`
3. Builds NuttX kernel pass (pass2)
4. Builds mbedTLS via `build-mbedtls.sh`
5. Builds NuttX user pass (pass1)
6. Links the final `nuttx` ELF
7. Packages the combined binary:
   - `nuttx/Meadow.OS.Update.bin` - 1792 KB OS image (kernel + user, with CRC appended)
   - `nuttx/Meadow.OS.bin` - Bootloader prepended at offset 0 + OS at offset 0x40000
   - `nuttx/Meadow.OS.Runtime.bin` - Mono runtime slice extracted from the user binary

These `Meadow.OS*` names are build-artifact filenames only; the source directory they are built
from is `F7MeadowOS/`, not `Meadow.OS/`.

### Key Build Options

| Flag | Effect |
|------|--------|
| `--clean` / `-c` | Full clean before build |
| `--wlclean` | Clean only Wilderness Labs object files (faster iteration) |
| `--debug` | Build with debug symbols |
| `--force` / `-f` | Force NuttX reconfiguration |
| `--esd` | Route stack dumps to USART1 (disables RAMLOG) |
| `-u=<tests>` | Enable specific unit test suites |

## Hardware

- **MCU**: STM32F777ZIT6 (ARM Cortex-M7, 216 MHz)
- **Internal Flash**: 2 MB (split 1 MB kernel / 1 MB user)
- **Internal SRAM**: 512 KB (DTCM 128 KB + SRAM1 368 KB + SRAM2 16 KB)
- **External SDRAM**: 32 MB at `0xC0000000` (3 MB exposed as heap2 to NuttX)
- **External QSPI Flash**: 32 MB (secondary OS image + runtime + filesystem)
- **WiFi coprocessor**: ESP32 (connected via custom ESPCP serial protocol)
- **Ethernet**: LAN8742A PHY
- **USB**: USB OTG FS (used for host communication)
- **Debug interface**: ST-Link (SWD)

## Documentation

Detailed documentation is in the [`Documentation`](Documentation/) directory:

| Document | Contents |
|----------|---------|
| [Architecture.md](Documentation/Architecture.md) | Protected build model, boot sequence, OTA state machine, kernel configuration |
| [Memory.md](Documentation/Memory.md) | Full memory map, flash partitions, MPU regions, battery-backed registers |
| [Kernelspace.md](Documentation/Kernelspace.md) | Kernel startup flow, all kernel modules and their responsibilities |
| [Userspace.md](Documentation/Userspace.md) | hcom application structure, all user-space modules |
| [Debugging.md](Documentation/Debugging.md) | OpenOCD/GDB setup, flashing, QEMU, Python GDB extensions |
