# Running Meadow.OS in the Renode Emulator

Meadow.OS can be built for, and run in, the Renode-based emulator in the
[Meadow.OS.Emulator](https://github.com/WildernessLabs/Meadow.OS.Emulator) repository. The
emulated board boots, answers Meadow.CLI over a TCP socket, starts Mono and runs managed apps.
Mono's mini test suite (`tests/managed/MiniTests`) gives the same result as on a board: 733 ran,
2 skipped, 1 known failure (`exceptions.test_0_ldflda_null_pointer`).

This document covers the Meadow.OS side. Renode, the board definition, the hooks and the
run scripts are documented in the emulator repository's README.

## Building for the emulator: `build.sh --emulator`

`--emulator` builds the usual `meadow_os` configuration with these changes, applied by
`enable_emulator()` in `scripts/configuration_tweaks.sh`:

| Change | Why |
|--------|-----|
| USB device off (`CDCACM`, `USBDEV`, `STM32F7_OTGFS`, `BOARDCTL_USBDEVCTRL`) | Renode does not model the USB OTG peripheral |
| HCOM on UART4, `/dev/ttyS1`, 115200 8N1 | The emulator exposes UART4 to Meadow.CLI as `socket://localhost:4242` |
| Receive flow control on UART4 (`SERIAL_IFLOWCONTROL`, `UART4_IFLOWCONTROL`) | Renode delivers a file download as bursts of tens of KB; without it the serial driver drops everything past its 512 byte buffer. There is no RTS pin: `board.h` defines `GPIO_UART4_RTS` as 0, and the driver holds the RX interrupt off while the buffer is full |
| Syslog on the serial console, USART1 (`SYSLOG_CONSOLE`, `SYSLOG_REGISTER`, `RAMLOG_SYSLOG` off) | The emulator writes USART1 to its console log. In NuttX 12, `SYSLOG_CONSOLE` is what routes the syslog to the console; `SYSLOG_SERIAL_CONSOLE` alone does not |
| `BOARD_RESET_ON_ASSERT` 0 | An assert halts instead of resetting, so the failure stays visible |
| No bootloader build and no `Meadow.OS*.bin` packaging | The emulator loads `nuttx.elf` / `nuttx_user.elf` directly |

A normal build does not set any of these. After an `--emulator` build, rebuild with
`./build.sh --clean --force` before flashing a board; after a normal build, rebuild with
`./build.sh --emulator --clean` before running the emulator (the configuration changes, and
objects from the other configuration break the link).

In a worktree, give the runtime location as for any worktree build:

```bash
MEADOW_RUNTIME_DIR=<OS>/runtime MEADOW_RUNTIME_ALLOW_UNPINNED=1 ./build.sh --emulator --clean
```

## Running it

From the Meadow.OS.Emulator checkout (the Renode fork and Meadow.CLI as siblings, see its
README). `MEADOW_EMULATOR_OS_DIR` points at this repository when it is not a sibling named
`Meadow.OS` or `Rebase`:

```bash
MEADOW_EMULATOR_OS_DIR=<this repository> MEADOW_RUNTIME_DIR=<OS>/runtime \
  ./build-meadow.os-emulated.sh --config configs/meadow-os.yaml   # runs build.sh --emulator
./run.sh --rebase                                                 # starts Renode
```

The build wrapper calls `build.sh --emulator`, splits the images for Renode, builds the Mono
image (`nuttx_mono.bin`, the `.mono*` sections from `0xC0000000`) and generates the hook
addresses. Hooks are address-specific, so run the wrapper again after every rebuild.

`configs/meadow-os.yaml` emulates an F7CoreComputeV2 (CCM). The firmware only starts the
CCM's SD card and Ethernet when `meadow.config.yaml` asks for them.

The console (USART1, the syslog) goes to `/tmp/renode-uart-meadow.txt`; the Renode monitor is
on port 1234 (`sysbus.cpu PC` and so on).

## Talking to it

Use the 3.0 Meadow CLI (`meadow-3.0` branch) and give the route with `-s`, so the saved route
for a real board is not changed:

```bash
meadow device info -s socket://localhost:4242

MEADOW_RUNTIME_DIR=<OS>/runtime MEADOW_CORE_DIR=<meadow-3.0>/Meadow.Core \
  meadow app run tests/managed/MiniTests -s socket://localhost:4242
```

A full MiniTests deploy and run takes a few minutes. The QSPI flash in Renode starts empty on
every Renode start, so the first deploy sends every assembly.

## Firmware changes found through the emulator

These are general fixes, not emulator code, and apply to boards as well:

- **Builds without the RAMLOG syslog** (`--esd`, `--emulator`) did not compile (unused
  variable and function in the trace code) and had no host comms: the kernel created the ku
  traverse FIFO (`/dev/hcom_traverse_ku`) only with `CONFIG_RAMLOG_SYSLOG`, and hcom stops its
  start-up when the FIFO is missing. The FIFO is now always created.
- **hcom receive buffer race** (`apps/system/hcom/comms/hcom_host_enq_deq.c`). When the receive
  thread waited for space and the scan found no complete packet, the buffer was discarded as
  malformed. Two races made it discard good data: the waiting flag stayed set after space had
  been freed, and the higher-priority receive thread could refill the buffer with complete
  packets between the scan and the test. The buffer is now discarded only when the space the
  receive thread needs is really not free, and only after a second scan. Verified on a CCM
  board: MiniTests passes with the same baseline.

## Limits

- Flash contents do not survive a Renode restart (they do survive a reset inside a session).
- Time in the emulator is virtual and runs ahead while the CPU is idle, so time-based
  timeouts behave differently from a board. The emulator disables the hcom download
  watchdog for this reason.
- The ESP32 coprocessor is a stub in the default configuration: no WiFi or Bluetooth.
- The `mono_ver_copy`, `espcp_force_init` and `mq_receive` hooks written for Legacy do not
  match this code and are skipped.
