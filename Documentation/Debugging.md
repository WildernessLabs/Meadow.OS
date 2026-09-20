# Debugging

## Overview

Meadow.OS is debugged using **OpenOCD** as the GDB server and **gdb** (with Python scripting) as the client. The ST-Link SWD interface connects to the target. QEMU is also supported for software-only testing.

## Prerequisites

- OpenOCD installed and accessible on `PATH` (or referenced via `MEADOW_OPENOCD_COMMAND`)
- ARM cross-toolchain (`arm-none-eabi-gdb` / `arm-none-eabi-gdb-py`)
- ST-Link v2/v3 debugger connected to the board's SWD port
- A completed build (symbol files `nuttx/nuttx.elf` and `nuttx/nuttx_user.elf` must exist)

## Quick Start

Open two terminals:

**Terminal 1 — Start the OpenOCD server:**
```bash
./debug.sh --server
```

**Terminal 2 — Launch GDB:**
```bash
./debug.sh
```

GDB will automatically connect to `localhost:4242`, load symbol files, and apply the `.gdbinit` configuration.

## `debug.sh` Options

| Flag | Effect |
|------|--------|
| `--server` | Start OpenOCD GDB server (blocks) |
| `--stlink` | Use `st-util` instead of OpenOCD |
| `--ocd` / `--openocd` | Use OpenOCD (default) |
| `--qemu` | Use QEMU instead of hardware |

## OpenOCD Configuration

### Debug session (`debug.cfg`)

```tcl
source [find interface/stlink.cfg]
source [find target/stm32f7x.cfg]
reset_config none separate

gdb port 4242
debug_level 1
tcl port 6666

# NuttX RTOS awareness
stm32f7x.cpu configure -rtos nuttx
```

NuttX RTOS awareness is enabled in OpenOCD. This allows OpenOCD to display NuttX tasks as threads in GDB.

### Flash session (`flash.cfg`)

`flash.cfg` writes whichever image is present, and the first branch is the one
that normally runs. If `nuttx/Meadow.OS.bin` exists — the combined bootloader and
OS image that `build.sh` produces — it is written to `0x08000000` and the script
stops there:

```tcl
set image nuttx/Meadow.OS.bin
if {[file exists $image]} {
  flash write_image erase $image 0x08000000
  verify_image $image 0x08000000
  shutdown
}
```

Only when that image is absent does it fall back to the separate binaries,
writing `nuttx.bin` at `0x08000000` and `nuttx_user.bin` at `0x08040000`:

```tcl
flash write_image erase nuttx/nuttx.bin 0x08000000
flash write_image erase nuttx/nuttx_user.bin 0x08040000
```

> **Note**: `flash.sh` uses `flash.cfg` by default. DFU mode (`--dfu`) is also supported using `dfu-util`.

## GDB Initialisation (`gdb/.gdbinit`)

The `.gdbinit` file in the `gdb` directory is loaded automatically when GDB is started from that directory. It:

1. Configures history, output radix (hex), and remote hardware limits:
   - 8 hardware breakpoints
   - 4 hardware watchpoints
2. Defines `load-nuttx-symbols` to load symbol files:
   ```
   file ../nuttx/nuttx.elf
   add-symbol-file -readnow ../nuttx/nuttx.elf
   add-symbol-file -readnow ../nuttx/nuttx_user.elf  (if present)
   ```
3. Loads Python helper scripts
4. Connects to the GDB server: `target extended-remote :4242`
5. Configures NuttX RTOS awareness for OpenOCD. The task control block offsets
   are **derived from the loaded symbols**, not hardcoded, so they cannot go
   stale when a NuttX change moves a field in `struct tcb_s`:
   ```
   monitor nuttx.g_tasklisttable_size 72

   eval "monitor nuttx.pid_offset %d", &((struct tcb_s *)(0))->pid
   eval "monitor nuttx.xcpreg_offset %d", &((struct tcb_s *)(0))->xcp.regs
   eval "monitor nuttx.state_offset %d", &((struct tcb_s *)(0))->task_state
   eval "monitor nuttx.name_offset %d", &((struct tcb_s *)(0))->name
   eval "monitor nuttx.name_size %d", sizeof(((struct tcb_s *)(0))->name)
   ```
   Literal `monitor nuttx.*_offset` lines are present in the file but commented
   out. Because the offsets come from the symbols, a wrong or missing symbol
   file produces a wrong task list rather than an error — load the symbols that
   match the firmware on the board.

### Useful GDB Macros

| Macro | Description |
|-------|-------------|
| `load-nuttx-symbols` | Reload all symbol files |
| `reset-qemu` | Load image and reset QEMU target |
| `enable_hardfault` | Enable hard fault on divide-by-zero and unaligned access (sets DEMCR bit) |

## Python GDB Extensions (`gdb`)

Custom Python scripts extend GDB with Meadow-specific commands:

| Script | Purpose |
|--------|---------|
| `Nuttx.py` | NuttX RTOS helper: task list, stack inspection |
| `Nuttx_Tasks.py` | Pretty-printer for NuttX task control blocks |
| `NuttxHeap.py` | Heap analysis: free/allocated block inspection |
| `Tracing.py` | Trace buffer display and analysis |

## Flashing the Board

### Using OpenOCD (default)

```bash
./flash.sh
```

Writes `nuttx/Meadow.OS.bin` to `0x08000000` via ST-Link when that combined image is present, falling back to `nuttx/nuttx.bin` (kernel) and `nuttx/nuttx_user.bin` (user) when it is not.

### Using DFU

```bash
./flash.sh --dfu
```

Writes `nuttx/Meadow.OS.bin` to `0x08000000` using `dfu-util`. Requires the board to be in DFU mode.

### Including the Mono Runtime

```bash
./flash.sh --includeruntime
```

Also writes `Meadow.OS.Runtime.bin` to the board using the Meadow CLI tool (`meadow mono update rt`).

### Resetting the Board

```bash
./flash.sh --reset
```

Uses STM32CubeProgrammer CLI (`CUBE_APP` env var must point to `STM32_Programmer_CLI`) to perform a hard reset after flashing.

## QEMU Debugging

QEMU support allows debugging without physical hardware.

**Start QEMU server (ARM):**
```bash
./debug.sh --server --qemu
```

**Start QEMU server (ESP32/Xtensa):**
```bash
./debug.sh --server --qemu --esp
```

QEMU ARM machine configuration:
- Machine: `meadow`
- Boot image: `nuttx/Meadow.OS.bin` (or `nuttx.bin` fallback)
- QSPI flash: `qemu/meadow_qspi_flash.raw` (32 MB raw file, created on first run)
- GDB port: `4242`
- hcom serial port: TCP `1234`

## Debugging the Bootloader

Bootloader symbols can be loaded in GDB by uncommenting this line in `.gdbinit`:

```gdb
add-symbol-file -readnow ../bootloader/Debug/Meadow.BL.elf
```

## Stack Dumps via UART

To get kernel panic stack dumps on USART1 (COM1) instead of using RAMLOG:

```bash
./build.sh --esd
```

This reconfigures the build to route syslog output to USART1 and disables RAMLOG. Useful for post-mortem analysis of crashes where the RAMLOG cannot be retrieved via the debugger.

## Tips

- If GDB fails to connect, ensure the OpenOCD server is running in Terminal 1 first. The script checks port 4242 with `nc` before launching GDB.
- For ST-Link clone adapters, `reset_config none separate` in `debug.cfg` is required when the RST pin is not wired.
- The bootloader is at `0x08000000` and NuttX kernel at `0x08040000`. Ensure your flash offsets are correct when loading images manually.
- Use `mon reset halt` in GDB to reset and halt the target before re-loading symbols after a reflash.
