# Cell Network

Meadow.OS can use a cellular modem (Quectel BG95-M3, BG770A, EG21-GL or M95) on a UART as its
network interface. The link is PPP. The modem UART is multiplexed with CMUX (3GPP 27.010), so that
AT commands (GNSS, signal quality, network scan) run alongside the data link instead of taking it
down:

```
                         +-> DLCI 0: CMUX control channel
COM1 -> cmux_create() ---+-> DLCI 1: /dev/pts/N+1 -> stock NuttX pppd() -> /dev/tun -> ppp0
(/dev/ttyS0)             +-> DLCI 2: /dev/pts/N+2 -> CellAT thread (AT commands)
```

The code is `apps/system/hcom/cell/hcom_cell.c`. NuttX's `netutils/pppd`, `netutils/chat` and
`netutils/cmux` do the PPP, chat-script and CMUX work; pppd and chat are unmodified upstream code,
and `cmux.c` carries bug fixes (see "Upstream changes" below).

Only the BG95-M3 has been tested on hardware. The others use the connect scripts carried over
from Legacy.

## Building

Cell support is built when `CONFIG_MEADOW_CELL_INCLUDE_IN_BUILD=y` (board `Kconfig`; set in the
`meadow_os` defconfig). It selects `NETUTILS_PPPD`, which selects `NETUTILS_CHAT` and `NET_TUN`.
It also:

- builds `cell/hcom_cell.c` into hcom;
- gates the kernel's modem power-on in `hcom_nx_startup_mgr.c`;
- adds the `ntpc_start` syscall, so that user space can start the kernel NTP client
  (`F7MeadowOS/ntpclient`) when the link comes up;
- binds the managed `meadow_cell_*` functions to the real implementations in
  `apps/system/mono/mappings-meadow.h` (without it, they are stubs that report "not connected").

`CONFIG_NETUTILS_CMUX=y` builds the CMUX library. `CONFIG_USART1_RXBUFSIZE=2048` gives the UART
enough room for a modem burst while the CMUX thread is held off; at the default 252 bytes, TCP
segments were lost.

## Configuration

`meadow.config.yaml` selects the interface:

```yaml
Network:
    DefaultInterface: Cell
```

`/meadow0/cell.config.yaml` holds the cell settings:

```yaml
Settings:
    APN: everywhere
    User: eesecure
    Password: secure
    Module: BG95M3
    Interface: COM1
    EnablePin: A3
```

| Key | Meaning |
|-----|---------|
| `APN` | Access point name |
| `User`, `Password` | Carrier credentials, if the carrier needs them. Sent with `AT+QICSGP` on the BG95-M3 (it rejects `AT+CGAUTH`) and with `AT+CGAUTH` on the BG770A; the other scripts do not send them |
| `Operator`, `Mode` | Manual operator selection (`AT+COPS=1,2,...`). Both are needed; otherwise selection is automatic (`AT+COPS=0`) |
| `Module` | `BG95M3`, `BG770A`, `EG21GL` or `M95` |
| `Interface` | `COM1`, `COM4` or `COM6` |
| `EnablePin` | The modem's power key pin (PWRKEY), for example `A3` |
| `Timeout` | Chat script timeout in seconds (default 30) |
| `ScanMode` | Deprecated |

### COM1 and the debug output

On a Project Lab the modem on mikroBUS 1 uses COM1, which is also where the debug output can go.
COM1 is free for the modem when `meadow.config.yaml` has no `Uart1Use` in its `InternalDebug`
section. For the syslog, build with `build.sh --dualcdc` (without `--nsh`) and set
`InternalDebug: TraceLevel: 2`: the syslog then appears on the second USB CDC port (see
[DualCdcAcm.md](DualCdcAcm.md)).

COM1 (`/dev/ttyS0`) is also the NuttX serial console, whose termios default to echo, canonical
mode and CR/LF translation. `hcom_cell.c` puts it into raw mode before use; with echo on, the
board would send everything the modem says straight back to it.

## Start-up

1. **Kernel** (`hcom_nx_startup_mgr.c`): if the cell interface is selected, the cell
   configuration is read and the modem is powered on by pulsing its enable pin.
2. **hcom** (`hcom_startup_manager.c`): once Mono has started, `hcom_cell_start()` takes a copy of
   the cell settings and starts the **CellPPPD** thread.
3. **CellPPPD**:
   1. waits for the modem to answer `AT` on the raw UART (up to 30 s);
   2. starts CMUX: `ATE0`, `AT+CMEE=2`, `AT+IFC=0,0` (no hardware flow control), then
      `AT+CMUX=0,0,5,127,10,3,30,10,2` (basic mode, UIH frames, 115200 baud, 127-byte frames), and
      opens DLCI 0-2;
   3. starts the **CellAT** thread;
   4. runs `pppd()` on DLCI 1 with the connect script for the module, and never returns.
      The connect script does not reset the modem (`AT+CFUN=1,1` would end CMUX), and the
      disconnect script hangs up (`ATH`).
4. **CellAT** turns echo on for its channel (`ATE1`), then once a second:
   - checks `ppp0` for an address;
   - when the link comes up: writes the configured `DnsServers` to the resolver file (stock pppd
     does not ask the network for its DNS servers); starts the NTP client if
     `GetNetworkTimeAtStartup` is set; reads `AT+GSN`, `AT+QCSQ` and `AT+CSQ` for the managed side;
     raises the connected event;
   - when the link goes down: raises the disconnected and connecting events;
   - while the link is down, once a minute, reports `AT+CSQ`, `AT+CEREG?` and `AT+COPS?` to the host
     (with `HCOM_CELL_DEBUG_LOGS`) and the syslog;
   - runs any request posted by the managed API.

## Managed API

`Meadow.Core`'s `F7CellNetworkAdapter` uses these native functions:

| Function | Behaviour |
|----------|-----------|
| `meadow_cell_is_connected()` | `true` while `ppp0` has an address |
| `meadow_cell_change_state(bits)` | Posts a request for the CellAT thread: GNSS (`1 << 1`), signal quality (`1 << 2`) or network scan (`1 << 3`). `0` ("resume") does nothing; the link is never paused |
| `meadow_get_cell_at_cmds_output(buf)` | The modem output of the last request, or of the connect-time read (IMEI, signal quality). Up to 1024 bytes |
| `meadow_get_cell_error()` | The last cell error code (`hcom_cell_err_t`) |

Each request ends with the AT command event, which carries a failure status if the commands did
not complete, so the managed caller stops waiting either way.

- **GNSS:** turns the receiver on, waits up to 120 s for a fix (polling `GGA` every 3 s), then reads
  `GSV`, `GGA`, `RMC`, `GSA` and `VTG` and turns it off. Only the sentences are returned.
- **Signal quality:** `AT+CSQ`, retried up to three times while the modem answers `99` ("not known").
- **Network scan:** `AT+COPS=?`, up to 180 s.

## Upstream changes

`apps/netutils/cmux/cmux.c` (NuttX) needed these bug fixes, all found by sniffing both directions
of the modem UART:

- A frame split across two UART reads was lost: the decoder returned with the opening flag
  already consumed.
- `openpty()` sets only the slave's termios. The master defaulted to `OPOST | OCRNL`, so every CR
  from the modem reached the channel as LF: AT replies were not recognised, and any PPP frame
  containing `0x0D` was corrupted.
- Control channel commands (for example MSC) were never answered, so the modem stopped serving
  the logical channels.
- The thread `select()`ed on the slave ptys, which belong to the channel users, with a 100 us
  timeout.
- The termios passed to `openpty()` was uninitialised.

pppd and chat are unmodified.

## Testing

`tests/managed/CellTests` runs through the hardware test runner. With an EE SIM (EE's public
settings are in `cell.ee.config.yaml`):

```bash
MEADOW_CELL_CONFIG=tests/managed/CellTests/cell.ee.config.yaml tools/testrunner/testrunner.py --test celltests
```

Without `MEADOW_CELL_CONFIG` the data link cases are skipped. See `tests/managed/README.md`.

## Known limitations

- The `cell.http_get` test takes about 40 s; the cause is not yet known.
- While acquiring a GNSS fix with the data link up, a BG95-M3 once went silent on every channel
  for about 215 s, then resumed. This is probably GNSS and LTE sharing the RF path (see
  `AT+QGPSCFG="priority"`); the GNSS fix wait is bounded at 120 s.
- Stock pppd's receive buffer is 1024 bytes. 968-byte TCP segments have been tested; full
  1500-byte IP packets have not.
- Stock pppd gives no connect-script progress, so the retry-exceeded event is not raised.
