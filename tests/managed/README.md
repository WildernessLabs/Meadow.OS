# Managed tests

Meadow apps that test the OS and the Mono runtime on the board. `tools/testrunner/testrunner.py`
runs them; `manifest.yaml` lists them, with their Mono modes and known failures. Each app prints
the output contract described in `tools/testrunner/README.md`.

| App | What it covers |
|---|---|
| `MiniTests` | Mono's mini test suite (issue #726): 733 JIT regression tests in 10 suites, under the JIT and the interpreter |
| `CellTests` | The managed cell API (GNSS and signal quality) through the pppd pause path, on a BG95-M3; no SIM needed |
| `EspcpTests` | espcp network robustness (issue #708): WiFi, `SO_SNDTIMEO` on new sockets, repeated HTTP and socket open/close (poll setup and teardown), and a connect that is never answered |

## MiniTests

The tests are compiled from `src/mono/mono/mini` in the runtime checkout that the OS build uses,
so they always match the runtime on the board. The suites, the exclusions and `TestDriver.cs`
follow the Legacy test runner, which exists only on Legacy branch `origin/meadow-3.0` under
`conductor/tracks/mono-upgrade-09-tests/test-runner-app`.

- **Suites:** basic, arrays, basic-calls, basic-float, basic-long, basic-math, objects,
  exceptions, generics, gshared. `gc-test.cs` and `devirtualization.cs` are left out, as in Legacy.
- **Excluded:** `begin_end_invoke` (APM is not supported) and `arm64_vtype_stack_args` (crashes in
  the gsharedvt Thumb-2 trampoline).
- **Known failure:** `exceptions.test_0_ldflda_null_pointer`. Mono throws
  `NullReferenceException` for the address of a field through a null unmanaged pointer; the test
  expects no exception.
- **Mode check:** a `TESTRUN INFO mode-probe` line times a hot loop: tens of milliseconds under the
  JIT, seconds under the interpreter. Both modes give the same test results, so this line shows
  which one ran.
- **`GetCustomAttributes` probe:** Legacy's TestDriver avoided `GetCustomAttributes` because it
  crashed on NuttX 7. The app calls it once as a separate case.

To deploy it by hand (the JIT, with the `meadow.config.yaml` in its folder):

```bash
MEADOW_RUNTIME_DIR=<runtime> MEADOW_CORE_DIR=<meadow-3.0>/Meadow.Core meadow app run tests/managed/MiniTests
```

## EspcpTests

Needs the board on WiFi and a web server on the bench network. The manifest entry's `config` key names `EspcpTests/meadow.config.yaml`, which the runner uses as the
base for the deployed `meadow.config.yaml` (only its `MonoControl` section is replaced). It sets the
WiFi/DHCP `Network` section and `CoProcessor: AutomaticallyStartNetwork: true`; without the latter
Meadow.Core does not connect to the default access point.

- **WiFi:** the test assumes `wifi.config.yaml` has already been written to the board, so the ESP32
  holds the default access point and Meadow.Core connects to it at start. The test does not
  provision WiFi. If the ESP32 has lost its network, deploy `wifi.config.yaml` once and reset the
  board (the OS sends the credentials to the ESP32 and deletes the file).
- **Order:** `wifi.connected` runs first and the other cases are skipped if it fails, so a WiFi failure
  never reaches the web server. The web server at `LocalServer` must be running for the HTTP and
  socket cases.
- **Idle server:** run `python3 tests/managed/EspcpTests/idle_server.py` on a machine on the bench network and set `IdleServer` in `MeadowApp.cs` to its address (192.168.1.65 today).
- **Constants:** `LocalServer` (192.168.1.65, a small web page) and `Blackhole` (10.255.255.1, an
  address nobody answers) are at the top of `MeadowApp.cs`.
- **Cases:** `wifi.connected`, `socket.send_timeout_default` (reads SO_SNDTIMEO back with
  `GetSocketOption`, expects 10 s; currently a known failure, see the manifest), `http.get_local`, `http.repeat_local_x30`, `tcp.connect_close_x40` (4 sockets at a time: the bench web server queues only five connections),
  `tcp.idle_receive_70s` (a receive left pending for 70 s, past the 60 s poll timeout, still completes; needs `idle_server.py` running, because the bench web server closes an idle connection after 60 s. The poll timeout itself does not fire here: the socket engine re-polls every fraction of a second),
  `tcp.connect_blackhole_bounded` (a connect that is never answered must end within 90 s),
  and the same HTTP and socket cases again afterwards to show the ESP32 still works.
- **Limit:** the 30 second response timeout in `espcp_queue_message()` is reached only when the
  ESP32 does not answer a request at all. The blackhole case holds an ESP32 worker but the ESP32
  still answers, so it does not prove that path; that needs an ESP32 that stops responding.

## CellTests

Needs an OS built with `CONFIG_MEADOW_CELL_INCLUDE_IN_BUILD=y`, a BG95-M3 click board on mikroBUS 1
(COM1, enable pin A3) and, for `cell.gnss_fix`, a GPS antenna with a view of the sky. A SIM is not
needed: without one the modem answers `CONNECT` and then hangs up, so pppd reconnects about once a
minute and runs the posted AT scripts then.

- **Config:** the app ships `meadow.config.yaml` (cell interface, the modem pins reserved, and
  `InternalDebug` with only `TraceLevel: 2`) and always ships `cell.config.yaml`. COM1 belongs to
  the modem, so there is no `Uart1Use`; for the syslog, build the OS with `build.sh --dualcdc`
  (without `--nsh`) and read the second USB CDC port. The deploy replaces the board's
  `meadow.config.yaml`, so restore the usual one afterwards.
- **SIM:** without one the two data link cases are skipped. With one, point `MEADOW_CELL_CONFIG`
  at a local `cell.config.yaml` that has the carrier's APN and credentials. Keep that file out of
  the repository. The app then ships it in place of the placeholder and requires the link:

  ```bash
  MEADOW_CELL_CONFIG=<local>/cell.config.yaml tools/testrunner/testrunner.py --test celltests
  ```
- **Mono must be running:** pppd starts only after Mono starts. Check that the boot banner says
  `Mono:Enabled`.
- **Cases:**
  - `cell.adapter`: the cell adapter exists.
  - `cell.connected` (SIM only): the PPP link comes up within 300 s (`IsConnected`).
  - `cell.http_get` (SIM only): an HTTP GET of `http://example.com/` over the link, which tests the
    network DNS servers and TCP through `ppp0`.
  - `cell.gnss_output`: `FetchGnssAtCmdsOutput` returns NMEA (`$GPGGA` and `$GPRMC`) within 300 s.
    It does not need a fix. A run takes about 210 s: a wait for the next pppd reconnect, then the
    GNSS script, which contains `PAUSE 120`.
  - `cell.gnss_fix`: GGA fix quality 1 or more and RMC status `A`. Reports only the quality, the
    satellite count, the HDOP and the GSA fix mode, because the NMEA contains the bench's
    position.
  - `cell.signal_quality`: `GetSignalQuality` returns a value other than NoSignal. This is
    a known failure without a SIM; see the manifest.
- **Limit:** the cases go through the pause path only. If the connect script itself keeps failing,
  pppd never runs the posted script and the managed call can block (`OS/CellPlan.md` 2.2). The
  runner's timeout ends the run in that case.
