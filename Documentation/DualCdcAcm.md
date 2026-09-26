# Dual USB CDC/ACM (Developer Builds)

## Overview

A normal Meadow.OS build presents a single USB serial port, used for HCOM (the
Meadow.CLI protocol). The `--dualcdc` build option adds a second USB serial port on
the same cable, for developer use only (see issue #457):

| Board device   | Windows Hardware Id | Linux `/dev/serial/by-id` suffix | Used for                      |
|----------------|---------------------|----------------------------------|-------------------------------|
| `/dev/ttyACM0` | `MI_00`             | `-if00`                          | HCOM (unchanged)              |
| `/dev/ttyACM1` | `MI_02`             | `-if02`                          | Second port (see Status below) |

This option is never used for shipping builds. The tracked `defconfig` carries none of
it, and the normal build's USB setup is unchanged.

### Status

Phase 1 (this document) brings up the second port only. It enumerates and can be opened
from the host, but nothing on the board reads from or writes to it yet. Routing syslog
to it, and optionally NSH, are later phases.

## Building

```bash
# First build with the second port - --force resets .config to the defconfig first
./build.sh --force --dualcdc

# Back to the normal single-port build
./build.sh --force
```

Flash the result with `flash.sh` as usual.

`--dualcdc` calls `enable_dualcdc()` in `scripts/configuration_tweaks.sh`, which switches
USB from the stand-alone CDC/ACM driver to the NuttX composite driver with two CDC/ACM
members (`CONFIG_USBDEV_COMPOSITE`, `CONFIG_CDCACM_COMPOSITE`,
`CONFIG_BOARD_DUAL_CDCACM`). As with `--nsh`, that function is the only place these
settings live. The board side is `board_init_usbdev()` in `src/stm32_boot.c` and
`board_composite0_connect()` in `src/stm32_composite.c`.

The dual-port build uses USB product ID `0x0002` instead of `0x0001`, with the same
vendor ID (`0x2E6A`) and the same vendor and product strings. Meadow.CLI matches on the
vendor ID, so it still finds the board. The different product ID keeps Windows from
reusing the driver setup it cached for the single-port device. The first time you plug
in a dual-port build, Windows installs drivers for it as a new device.

## Using Meadow.CLI with Two Ports

Meadow.CLI treats every serial port with the Wilderness Labs vendor ID as a Meadow, so
it sees both ports:

```
C:\>meadow port list
Found devices on ports:
 1: COM5
 2: COM7
```

The order is not guaranteed, and the second port can be listed first. If the CLI picks
it, HCOM commands time out. **Always pin the CLI to the HCOM port:**

1. Find the HCOM port:
   - **Windows:** in Device Manager, open each Meadow port's Properties, then the
     Details tab, and choose "Hardware Ids". The one containing `MI_00` is HCOM.
   - **Linux:** `ls -l /dev/serial/by-id/`. The `...-if00` entry is HCOM.
2. Pin the route: `meadow config route COM7` (or the Linux device path).
3. Check it: `meadow device info`.

The route stays set until you change it. COM numbers can change when you switch between
single-port and dual-port builds, so re-check the route after switching.

## Flashing

Use `flash.sh` for dual-port builds, not `meadow firmware write`. After a DFU write,
the CLI reconnects by looking for serial ports that were not there before. When two new
ports appear, it tries only the first one and gives up if that port isn't HCOM
(`FindMeadowConnection()` in Meadow.CLI's `FirmwareUpdater.cs`).

## Opening the Second Port

Any terminal program works (PuTTY, Tera Term, picocom). The baud rate setting is ignored
for USB CDC/ACM. Only one program can hold a COM port at a time. On Windows, "Access is
denied" (error 5) when opening the port means another program already has it open. It
does not mean the board has a problem.
