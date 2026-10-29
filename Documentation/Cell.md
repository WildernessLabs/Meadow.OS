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
   - checks that `ppp0` is up and has an address (pppd takes `ppp0` down while it redials but
     leaves the address set);
   - when the link comes up: writes the configured `DnsServers` to the resolver file (stock pppd
     does not ask the network for its DNS servers); starts the NTP client if
     `GetNetworkTimeAtStartup` is set; reads `AT+GSN`, `AT+QCSQ` and `AT+CSQ` for the managed side;
     raises the connected event;
   - when the link goes down: raises the disconnected and connecting events;
   - while the link is down, once a minute, reports `AT+CSQ`, `AT+CEREG?` and `AT+COPS?` to the host
     (with `HCOM_CELL_DEBUG_LOGS`) and the syslog;
   - after every 10 minutes without a link, raises the retry-exceeded event (managed
     `ConnectFailed`). Stock pppd retries without reporting progress; Legacy's pppd raised the
     event after 15 failed connect attempts, about 10 minutes;
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

- **GNSS:** turns the receiver on (`AT+QGPS=1`) and waits up to 120 s for a fix, then reads `GSV`,
  `GGA`, `RMC`, `GSA` and `VTG` and turns it off (`AT+QGPSEND`). Only the sentences are returned.
  It polls `GGA` every 3 s for the first 15 s, sends nothing from 15 s to 90 s, then polls every 3 s
  again (see the known limitations).
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
- Everything read from a channel went out as one frame, so frames longer than the negotiated
  maximum (N1 = 127) were sent; the modem may discard them.
- Frames were written to the non-blocking UART with a single `write()`. USART1's transmit buffer
  is 252 bytes, so a long frame was cut short and the rest of the channel data was lost. Uploads
  of more than a couple of hundred bytes per packet never got through.
- A channel or serial port whose other end was closed is reported readable on every `select()` and
  reads end of file, so the thread spun at CMUX priority and starved other threads (the cell link's
  AT requests took minutes). It now sleeps 10 ms when a `select()` round delivers no data.

chat is unmodified. pppd's frame buffer (`PPP_RX_BUFFER_SIZE` in `apps/netutils/pppd/ppp_conf.h`)
was a fixed 1024 bytes; it is now the TUN MTU plus 8. pppd does not negotiate an MRU, so the modem
sends packets of up to 1500 bytes, and every packet over about 1020 bytes was dropped, in both
directions. pppd's `ppp_arch_putchar()` dropped a byte when the tty was still full after one 1 s
wait, corrupting the frame; it now keeps waiting (up to 10 s).

pppd decides the link is lost after `AHDLC_TX_OFFLINE` (`apps/netutils/pppd/ppp_conf.h`) with nothing
received. Two changes:
- It counted every frame sent, so a burst of packets nobody answered (SYNs to a host that drops
  them) took the link down for about 17 s while the modem was fine. It now counts only its LCP echo
  requests (one every 20 s); any received frame still resets the count.
- The limit is 15, not 5: at 5 a GNSS request longer than about 105 s also cost a redial. At 15 a
  silent modem is detected after about 5 minutes.

## Testing

There are two test suites: native tests, to check OS changes quickly without Mono, and a managed test
app, which checks the link between the managed API and the OS.

### Native tests

Build with `build.sh --unittests=cell` and run developer test 20 (`meadow developer -p 20`). The
code is `apps/system/hcom/tests/cell_tests.c`. Without a modem it checks:

- the connect scripts for each module (credentials, operator selection, no modem reset, defaults);
- the AT reply parser, with the test playing the modem on a pty pair (`OK` whole or split, `ERROR`,
  `+CME ERROR`, a bounded timeout, CR/LF kept);
- GGA fix detection;
- CMUX, with the test playing a CMUX modem on a pty pair: whole, split and back-to-back frames, a bad
  FCS, CR delivered unchanged, the control channel answered, and channel-to-modem framing (including
  400 bytes split into frames of at most 127 bytes). These pin the `cmux.c` fixes listed above.

`meadow developer -p 20 -v 1` adds live checks, which need the cell link to be running: the link
is up, a DNS lookup works, and a signal quality request through the native API returns `+CSQ`. CMUX
has no tear-down, so each run leaves a CMUX thread and three ptys behind; restart the board after a
few runs.

### Managed tests

`tests/managed/CellTests` runs through the hardware test runner. With an EE SIM (EE's public
settings are in `cell.ee.config.yaml`):

```bash
MEADOW_CELL_CONFIG=tests/managed/CellTests/cell.ee.config.yaml tools/testrunner/testrunner.py --test celltests
```

Without `MEADOW_CELL_CONFIG` the data link cases are skipped. See `tests/managed/README.md`.

## Known limitations

- Disposing a socket while a blocking (synchronous) `Socket.Connect()` is pending takes until the
  connect times out (about 46 s): .NET waits for the call to return and NuttX cannot abort a pending
  connect. `ConnectAsync` (HttpClient, MQTTnet) is not affected (issue #716).
- Socket options that .NET and MQTTnet set before connecting (issue #718):
  - `SO_LINGER` is accepted but has no effect (`CONFIG_NET_SOLINGER` is off): `getsockopt` reports
    it off and `close()` never lingers. Enabling `CONFIG_NET_SOLINGER` would make MQTTnet's
    `LingerOption(true, 0)` an abortive close and disables `tcp_alloc()`'s reuse of closing
    connections.
  - `SO_RCVBUF` works: `CONFIG_NET_RECV_BUFSIZE=16384` gives each socket a receive limit, above the
    4704-byte IOB pool, so it does not shrink the receive window.
  - `SO_SNDBUF` is accepted but has no effect: `CONFIG_NET_SEND_BUFSIZE` needs TCP write buffers,
    which are off.
- The `cell.http_get` test takes about 40 s; the cause is not yet known.
- The BG95-M3 cannot run GNSS and the data link at the same time. In its default GNSS priority
  mode (`AT+QGPSCFG="priority"`) it hands the radio to GNSS as soon as `AT+QGPS` runs, so no data
  flows during a GNSS request; it resumes after `AT+QGPSEND`. WWAN priority mode needs eDRX, which
  the network did not grant in testing, so GNSS never ran. Applications should not send traffic
  during a GNSS request.
- With the data link up, a BG95-M3 polled with AT commands while it searched for a slow GNSS fix
  stopped answering on every channel (AT and PPP) for 224 s or more. Left unpolled, it did not.
  Hence the quiet period in the GNSS request. Firmware tested: `BG95M3LAR02A03_01.014.01.014`.
- Large transfers are slow. The `cell.http_large` case (an 8 KB download and a 4 KB upload,
  1400-byte packets both ways) takes about 25 s: NuttX TCP drops out-of-order segments and offers a
  4.5 KB receive window, so each reordered segment costs a retransmission timeout. 1400-byte IP
  packets have been tested; 1500-byte ones have not (the servers tested offered an MSS of 1360).
