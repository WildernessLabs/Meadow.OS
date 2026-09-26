# Dual USB CDC/ACM (Developer Builds)

## Overview

A normal Meadow.OS build presents a single USB serial port, used for HCOM (the
Meadow.CLI protocol). The `--dualcdc` build option adds a second USB serial port on
the same cable, for developer use only (see issue #457):

| Board device   | Windows Hardware Id | Linux `/dev/serial/by-id` suffix | Used for                      |
|----------------|---------------------|----------------------------------|-------------------------------|
| `/dev/ttyACM0` | `MI_00`             | `-if00`                          | HCOM (unchanged)              |
| `/dev/ttyACM1` | `MI_02`             | `-if02`                          | Syslog (see below)            |

This option is never used for shipping builds. The tracked `defconfig` carries none of
it, and the normal build's USB setup is unchanged.

### What Uses the Second Port

`HCOM_USB_PORT2_USAGE` in `F7MeadowOS/include/meadow/hcom_shared_common.h` selects it.
The default, `HCOM_USB_PORT2_SYSLOG`, sends syslog there. `HCOM_USB_PORT2_NSH` is
reserved for running NSH on the port and is not implemented yet.

## Syslog on the Second Port

In a `--dualcdc` build, syslog always goes to the second port. No CLI command is needed.
It is sent in addition to the existing outputs: the syslog UART and the CLI
(`meadow uart trace` and `meadow trace` commands) work as before and are not affected
by it.

A separate kernel thread, `RamlogUsb` (`F7MeadowOS/hcom_nx/diag/hcom_nx_trace_usb.c`),
does this. It has its own reader on the NuttX RAMLOG buffer (32 KB), independent of the
`RamlogRead` thread that feeds the UART and the CLI. A RAMLOG reader starts at the
oldest data in the buffer and keeps its own position, so:

- **You see the whole boot.** Opening the port shows everything still in RAMLOG, back
  to the start of boot, then live output.
- **No gaps while nothing is reading.** When no host program has the port open, the
  thread waits and RAMLOG keeps the backlog. Output is only lost if more than 32 KB
  goes unread, and then the oldest part is lost.
- **Nothing else waits.** Only this thread blocks. The UART and CLI outputs, and the
  rest of the system, are unaffected.
- **Survives a USB disconnect.** If the host drops the USB connection while the board
  stays powered (for example `pnputil /restart-device` on Windows), the thread reopens
  the port and continues from its RAMLOG position. The few hundred bytes that were in
  transit at the time are lost.
- **Same verbosity.** The port shows whatever the current syslog mask allows, the same
  as the UART.

Each line ends in CR-LF.

### Resets and Terminal Programs

Resetting the board restarts its USB controller, so Windows removes both COM ports and
adds them back, with the same numbers, once the board is up again. Most terminal
programs, PuTTY included, report an error and stop when the port disappears. To keep
watching across resets, use a viewer that reopens the port, such as this PowerShell
loop:

```powershell
$Port = "COM5"
while ($true) {
  $p = $null
  try {
    $p = New-Object System.IO.Ports.SerialPort $Port
    $p.Open()
    Write-Host "`n--- connected to $Port ---" -ForegroundColor Green
    while ($p.IsOpen) {
      $s = $p.ReadExisting()
      if ($s) { [Console]::Write($s) } else { Start-Sleep -Milliseconds 20 }
    }
  } catch {
    Write-Host "." -NoNewline -ForegroundColor DarkGray
  } finally {
    if ($p) { $p.Dispose() }
  }
  Start-Sleep -Milliseconds 250
}
```

Paste it at a PowerShell prompt, after setting `$Port` to the `MI_02` port. Stop it with
Ctrl+C. Grey dots mean it is waiting for the port. If it only prints dots while the
board is up, another program has the port open.

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
