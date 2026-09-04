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

Two things not obvious from a directory listing:
- `F7MeadowOS/libyaml/` and `F7MeadowOS/libcyaml/` are vendored third-party code - do not modify.
- `F7MeadowOS/` is symlinked into the board tree as `.../src/F7MeadowOSLink`.

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

**Available `--unittests=` values:** `esp`, `sqlite`, `snprintf`, `gpio`, `overload`, `bbr`,
`chat`, `ethernet`, `bg77`, `iso8601`, `power`, `sdcard`, `misc`, `dirmgmt`, `adc`, `dac`,
`spidma`, `pwm`, `rotenc`, `i2c`, `os`, `usrwork`, `sotest`, `all`

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

See `Documentation/Debugging.md` for flashing and hardware-debug workflows (`flash.sh`,
`debug.sh`, GDB helper scripts); see `esp32/CLAUDE.md` for ESP32 coprocessor build commands.

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
