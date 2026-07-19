# CLAUDE.md - Workspace Root

This file provides workspace-level guidance. For NuttX-specific content (architecture, Kconfig,
boot sequence, rebase state), see `Documentation/Rebasing.md` and the other files under
`Documentation/` (there is no `nuttx/CLAUDE.md` - NuttX-specific guidance lives in `Documentation/`
instead).

## What This Workspace Is

Meadow.OS is the open-source POSIX RTOS for the Wilderness Labs **Meadow F7** board
(STM32F777ZIT6, Cortex-M7, 216 MHz). It is a fork of Apache NuttX 12, extended with
Wilderness Labs board support, a host-communication protocol (HCOM), an ESP32 WiFi
coprocessor driver, and a Mono/.NET runtime integration.

## Repository Layout

```
Rebase/
+-- nuttx/              NuttX kernel source tree
+-- F7MeadowOS/         Wilderness Labs kernel extensions, board support, and public APIs
|   |                   (symlinked into the board tree as .../src/F7MeadowOSLink)
|   +-- include/meadow/ Public Meadow API headers (~35 headers, shared with apps/system/hcom)
|   +-- hcom_nx/        Host comm kernel side, OTA routing, QSPI/FS init
|   +-- espcp/          ESP32 coprocessor driver (SPI data + UART control); see espcp/readme.md
|   +-- meadow_os/      Core OS init, fault handling, GPIO, battery-backed registers
|   +-- pwrmgmt/        Stop-mode power management, LSI clock calibration
|   +-- ethernet/       LAN8742A ethernet bring-up and monitoring
|   +-- timers/         Timer capture, PWM, frequency measurement
|   +-- specialized/    Rotary encoder and frequency-measurement drivers
|   +-- ntpclient/      Wilderness Labs fork of the NuttX ntpclient app
|   +-- meadow_cloud/   Meadow Cloud key provisioning
|   +-- kerneltests/    On-target test suite
|   +-- misc/           Shared logging, ADC, watchdog, ISO8601, circular buffer
|   +-- libyaml/        Vendored third-party YAML parser - do not modify
|   +-- libcyaml/       Vendored third-party schema-driven YAML wrapper (used by hcom_nx_config_manager) - do not modify
+-- apps/               NuttX applications (hcom lives in apps/system/hcom/)
+-- bootloader/         STM32 HAL bootloader (Meadow.BL) - CRC verify, OTA, rollback
+-- esp32/              ESP32 coprocessor firmware (ESP-IDF project)
+-- mbedtls/            mbedTLS - TLS support for the coprocessor protocol
+-- gdb/                GDB init scripts and NuttX-aware Python extensions
+-- scripts/            Shared shell helpers (common_methods.sh, debug_methods.sh, etc.)
+-- Documentation/      Detailed design docs (see below)
+-- build.sh            Top-level STM32 build script
+-- build-esp32.sh      ESP32 coprocessor build script
+-- build-bootloader.sh Bootloader build (called by build.sh automatically)
+-- build-mbedtls.sh    mbedTLS build (called by build.sh automatically)
+-- flash.sh            Flash STM32 via OpenOCD or DFU
+-- debug.sh            Launch OpenOCD GDB server and/or arm-none-eabi-gdb
+-- debug.cfg           OpenOCD configuration for interactive debugging
+-- flash.cfg           OpenOCD configuration for flashing
```

> **Symlink note:** `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/F7MeadowOSLink` is a
> symlink to `../../../../../../F7MeadowOS` (i.e. `F7MeadowOS/` at workspace root). Edit
> `F7MeadowOS/` directly - never via the symlink path.

## Environment Setup

Two env vars are required for flash and debug operations:

```bash
# Path to a Meadow-patched OpenOCD build (NuttX RTOS awareness)
export MEADOW_OPENOCD_DIR=/path/to/openocd

# For STM32CubeProgrammer-based board reset (optional)
export CUBE_APP=/Applications/STMicroelectronics/STM32CubeProgrammer.app/Contents/MacOs/bin/STM32_Programmer_CLI
```

The ARM cross-toolchain is installed under `toolchain/macos/` (or `toolchain/linux/`).
`build.sh` adds it to `PATH` automatically; no manual setup needed for builds.

## Build Workflow

All commands run from the **workspace root**, not from `nuttx/`:

```bash
# Standard build (auto-configures on first run)
./build.sh

# Iterative rebuild - cleans only Wilderness Labs and apps objects (fast)
./build.sh --wlclean

# Full clean rebuild
./build.sh --clean

# Configure NuttX without building
./build.sh --configure

# Build with unit tests enabled (comma-separated)
./build.sh --unittests=esp,gpio,adc

# Build with stack dumps to USART1/COM1 (disables RAMLOG - hard fault debugging)
./build.sh --esd

# Build with debug symbols
./build.sh --debug
```

**Available `--unittests=` values:** `esp`, `sqllite`, `snprintf`, `gpio`, `overload`, `bbr`,
`chat`, `ethernet`, `bg77`, `iso8601`, `power`, `sdcard`, `misc`, `dirmgmt`, `adc`, `dac`,
`spidma`, `rotenc`, `os`, `usrwork`, `all`

### What `build.sh` produces

| Output file | Description |
|-------------|-------------|
| `nuttx/nuttx.bin` | Kernel image (load at `0x08040000`) |
| `nuttx/nuttx_user.bin` | User-space image |
| `nuttx/Meadow.OS.Update.bin` | 1792 KB kernel+user image with CRC (used for OTA) |
| `nuttx/Meadow.OS.bin` | Bootloader + OS combined binary (flash from `0x08000000`) |
| `nuttx/Meadow.OS.Runtime.bin` | Mono runtime slice, written separately via Meadow CLI |

> These `Meadow.OS*` filenames are build-artifact names only - the source directory they are
> built from is `F7MeadowOS/`, not `Meadow.OS/`.

### NuttX make targets (run from `nuttx/`)

```bash
make menuconfig      # Interactive Kconfig editor
make savedefconfig   # Write .config changes back to defconfig
make distclean       # Full clean including .config
make pass2           # Kernel pass only
make pass1           # User-space pass only
make nuttx.elf       # Final link
```

### ESP32 coprocessor build

```bash
# Build ESP32 firmware
./build-esp32.sh

# Build and flash to the ESP32 (requires board connected)
./build-esp32.sh --flash

# Build and open serial monitor
./build-esp32.sh --monitor

# Full clean build
./build-esp32.sh --fullclean

# Enter ESP-IDF menuconfig
./build-esp32.sh --menuconfig

# Build with debug logging for specific subsystems (comma-separated: WIFI,SPI,BT,SYSTEM)
./build-esp32.sh --debug=WIFI,SPI
```

## Flash Workflow

```bash
# Flash via OpenOCD (requires MEADOW_OPENOCD_DIR set and ST-Link connected)
./flash.sh

# Flash via DFU (no ST-Link required, board in DFU mode)
./flash.sh --dfu

# Flash OS + Mono runtime (runtime written via Meadow CLI)
./flash.sh --includeruntime

# Reset board after flashing (requires CUBE_APP set)
./flash.sh --reset
```

`flash.sh` looks for `nuttx/Meadow.OS.bin` first; falls back to `nuttx/nuttx.bin` +
`nuttx/nuttx_user.bin`. `flash.cfg` loads both kernel and user images at the correct offsets
and verifies after write.

## Debug Workflow

```bash
# Start OpenOCD GDB server (port 4242) - leave running in a terminal
./debug.sh --server

# In a second terminal: attach GDB to the running target
./debug.sh

# Combined one-shot: launch server and connect GDB
./debug.sh --openocd

# Machine-interface mode (for IDE integration, e.g. cortex-debug)
./debug.sh --mi
```

`debug.cfg` configures OpenOCD with NuttX RTOS awareness (`-rtos nuttx`), so GDB can
inspect NuttX tasks, stacks, and registers with correct context switching. The GDB server
listens on port 4242.

### GDB helper scripts (`gdb/`)

| File | Purpose |
|------|---------|
| `Nuttx.py` | NuttX-aware GDB extensions (task listing, stack inspection) |
| `NuttxHeap.py` | Heap allocation tracing |
| `NVIC.py` | ARM NVIC register inspection |
| `Tracing.py` | Execution tracing helpers |
| `STM32F777.svd` | SVD file for peripheral register display |
| `gdb-dashboard.src` | GDB dashboard layout |
| `meadow_cortex-debug.cfg` | VS Code cortex-debug launch config template |

Load all Python extensions in one shot from GDB:

```
(gdb) source gdb/Nuttx.py
```

## Linting

NuttX enforces a strict coding style. Check all files you touch before committing:

```bash
# Check recent commits (run from nuttx/)
./tools/checkpatch.sh -g HEAD~...HEAD

# Check a single file
./tools/checkpatch.sh -f path/to/file.c
```

Lint failures are caught by CI (`lint/*` step). Fix pre-existing issues in every file you touch.

## Character Encoding

All files in this repository - source code, scripts, and documentation alike - must use ASCII
characters only (code points 0x00-0x7F). Do not introduce Unicode box-drawing characters, arrows,
em/en dashes, smart quotes, or any other non-ASCII symbol. Use plain ASCII equivalents instead:
`->` for arrows, `--` for dashes, and `+`/`-`/`|` for diagram borders and tree structures. This
applies to every file you create or edit, including ASCII-art diagrams in Markdown code blocks.

## Documentation

Detailed design documents live in `Documentation/`:

| Document | Contents |
|----------|---------|
| `Architecture.md` | Protected build model, boot sequence, OTA state machine, kernel configuration |
| `Memory.md` | Full memory map, flash partitions, MPU regions, battery-backed registers |
| `Kernelspace.md` | Kernel startup flow, all kernel modules and their responsibilities |
| `Userspace.md` | hcom application structure, all user-space modules |
| `Debugging.md` | OpenOCD/GDB setup, flashing, QEMU, Python GDB extensions |
| `Rebasing.md` | How to rebase Meadow.OS onto a new upstream NuttX version |
| `RunningDeveloperTests.md` | How to enable and run on-target unit tests |
