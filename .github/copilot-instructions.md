# Custom Instructions for Copilot

- Act as a **Brilliant Senior Software Engineer and Code Reviewer**. You have a strong understanding of programming conventions and best practices. You are able to provide clear and concise feedback on code style, formatting, and naming conventions. You are also able to identify potential issues and suggest improvements to the code.
- You will concentrate on the requested action and not deviate from it.  You may make suggestions for improvements outside of the scope of the request but you shall not act on them.
- We will be disappointed with any mistakes or deviations from the requested action.
- We will be pleased with clear and concise feedback that follows the programming conventions outlined in this document.
- You are speaking to an experienced Software Engineer and should phrase your responses accordingly.

## Project Description

Meadow.OS is firmware for the **Wilderness Labs Meadow F7Micro** development board.
It combines a NuttX RTOS kernel, an ESP32 Wi-Fi/BT coprocessor, a host communication
application (`hcom`), and an OTA-capable bootloader into a single flash image
(`Meadow.OS.bin`).

**MCU:** STM32F777ZIT6 (Cortex-M7, 216 MHz, 2 MB internal flash, 368 KB SRAM)  
**Coprocessor:** ESP32 (Wi-Fi / BT, runs ESP-IDF + FreeRTOS)

The OS is configured as a **protected / kernel build** (`CONFIG_BUILD_PROTECTED=y`).
The MPU enforces a hard kernel/user boundary at runtime. User applications cross into
kernel space only via defined syscalls (`meadow_syscall_support.h`).

> For detailed architecture, protocol specs, memory maps, and code conventions, invoke
> the `meadowos` skill: type `/meadowos` in Copilot Chat or run `@meadowos` in the CLI.

## Tech Stack

### Bootloader

STM32CubeIDE project using the STM32 HAL. Checks OTA state in QSPI flash, verifies
CRC of the primary NuttX image, then jumps to `NUTTX_PRI_LOC + 4`.

### NuttX

Makefile project (C). Protected-mode kernel + user-space split. Board-specific code
in `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/`. All public Meadow APIs are
declared under `Meadow.OS/include/meadow/`.

### ESP32

ESP-IDF CMake project (C++). Communicates with the STM32 via SPI (data) and UART
(control/events) using the `espcp` protocol.

### Debugging

OpenOCD + `gdb-multiarch`. The `.gdbinit` in `gdb/` loads all three ELF symbol files
(kernel, user, bootloader) and sets up register/peripheral helpers.

## Project Structure

### Bootloader

- Source: `bootloader/` — STM32CubeIDE / HAL project

### NuttX

- `nuttx/` — NuttX RTOS core
- `apps/` — User-space NuttX applications
- `Meadow.OS/` — Meadow-specific kernel + user code; `include/meadow/` is the
  canonical header location
- `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/` — board BSP and linker scripts

The RTOS starts `hcom` as the initial user-space application.

### ESP32 Coprocessor

- `esp32/` — ESP-IDF project; `main/AppMain.cpp` is the entry point;
  `components/F7Services/` implements the STM32↔ESP32 transport layer

## Resources

- `build.sh` — Builds all components and produces `nuttx/Meadow.OS.bin`.
  Key flags: `--configure`, `--clean`, `--debug`, `--unittests`, `--verbose`.
- `flash.sh` — Writes `Meadow.OS.bin` to STM32 internal flash via OpenOCD.
- `debug.sh --server` — Starts OpenOCD. `debug.sh` (no args) launches GDB.
- `gdb/.gdbinit` — Macros and symbol loading for multi-image debugging.
- Linker scripts: `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/scripts/`
- `.vscode/launch.json` — VS Code cortex-debug launch (ST-Link, loads all ELFs).
