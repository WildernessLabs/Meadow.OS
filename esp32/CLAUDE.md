# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Meadow-ESP32** is ESP32 firmware (C++20/C11, ESP-IDF 5.3+, FreeRTOS) that provides WiFi, Bluetooth, POSIX networking, and file storage services to an external STM32 microcontroller via SPI and UART.

- **Target**: ESP32-PICO-D4
- **STM32->ESP32**: UART for control/state, SPI for request/response
- **Default branch**: `develop`

## Build Commands

```bash
./build.sh --build                      # Release build
./build.sh --develop                    # Build with debug symbols (DDEBUG=1)
./build.sh --build --flash --monitor    # Build, flash, and monitor
./build.sh --fullclean                  # Full rebuild (use when incremental fails)
./build.sh --help                       # All options

# Debug subsystems: WIFI, SPI, BT, SYSTEM
./build.sh --develop --debug=WIFI --build

# Direct idf.py
idf.py build
idf.py menuconfig
idf.py monitor
```

### Diagnostic Build Flags

| Flag | Effect |
|------|--------|
| `--develop` | `DEBUG=1` + debug symbols |
| `--coredump` | Enable core dump to flash |
| `--heaptracing` | Heap memory tracing |
| `--tracemessages` | SPI message protocol tracing |
| `--halt` | Halt on hard fault |

## Testing

Tests live in `components/F7Services/test/` and use the Unity framework. Run from a separate build in `test/`:

```bash
cd test
cp secrets.template secrets.h   # Add WiFi credentials once
./build.sh --build
./build.sh --flash
idf.py monitor                  # Interactive menu selects test
```

## Architecture

### Message Flow

Communication protocol details: `Documentation/CommunicationsOverview.md`, `Documentation/SPIProtocolOverview.md`, `Documentation/MessageFormat.md`.

All source is under `components/F7Services/` (see that tree, and `partitions.csv` for flash layout).

## Code Conventions

**Formatting**: Allman style, 4-space indentation, rules in `.clang-format`.

**Character encoding**: ASCII only (code points 0x00-0x7F) in every file you create or edit,
including code, comments, and Markdown docs. No Unicode box-drawing characters, arrows, em/en
dashes, or smart quotes. Use `->` for arrows, `--` for dashes, and `+`/`-`/`|` for diagrams.

**Spelling**: UK English throughout (`ise` not `ize`).

**Docstrings**: Required on all namespaces, classes, functions, and variables.

**Naming**:
| Construct | Style |
|---|---|
| Classes, Functions, Namespaces | `PascalCase` |
| Constants | `UPPER_SNAKE_CASE` |
| Private member variables | `_camelCase` |
| Function parameters | `camelCase` |
| File names | `PascalCase.ext` |

**C++ specifics**:
- `nullptr` not `NULL`
- `return()` style for return statements
- `const` wherever appropriate
- Use whole words, not abbreviations
- Spaces around operators, after commas, after casts

## Key Build Notes

- `build.sh` injects git commit hash, branch, and build timestamp into `main/build_info.h` from `build_info.template`, then restores the template after build.
- SPI transaction hard limit: **4092 bytes** (hardware constraint).
- SDK config files: `sdkconfig.defaults` (release), `sdkconfig.develop` (merged on `--develop`), `sdkconfig` (generated, not committed).
- Managed dependencies: `joltwallet/littlefs` v1.5.5, `espressif/cjson` ^1.7.19 (via `idf_component.yml`).

## Documentation

`Documentation/` contains detailed guides:
- `Building.md` - build options
- `Testing.md` - test setup
- `CommunicationsOverview.md` - STM32<->ESP32 protocol
- `SPIProtocolOverview.md` - SPI low-level details
- `MessageFormat.md` - message structure
- `Diagnostics.md` - debugging techniques
- `Tools.md` - VS Code, Docker, OpenOCD, GDB setup
