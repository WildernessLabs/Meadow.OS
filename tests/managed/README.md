# Managed tests

Meadow apps that test the OS and the Mono runtime on the board. `tools/testrunner/testrunner.py`
runs them; `manifest.yaml` lists them, with their Mono modes and known failures. Each app prints
the output contract described in `tools/testrunner/README.md`.

| App | What it covers |
|---|---|
| `MiniTests` | Mono's mini test suite (issue #726): 733 JIT regression tests in 10 suites, under the JIT and the interpreter |
| `CellTests` | The cell network on a BG95-M3: the managed API (GNSS and signal quality) without a SIM, and the data link with one |
| `EspcpTests` | espcp network robustness (issue #708): WiFi, `SO_SNDTIMEO` on new sockets, repeated HTTP and socket open/close (poll setup and teardown), and a connect that is never answered |
| `SoakTests` | Soak and torture tests (issue #727), run from their own manifest `soak.yaml`: the faults that short tests miss, over many windows |

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
- **Cases:** `wifi.connected`, `http.get_local`, `http.repeat_local_x30`, `tcp.connect_close_x40` (4 sockets at a time: the bench web server queues only five connections),
  `tcp.idle_receive_70s` (a receive left pending for 70 s, past the 60 s poll timeout, still completes; needs `idle_server.py` running, because the bench web server closes an idle connection after 60 s. The poll timeout itself does not fire here: the socket engine re-polls every fraction of a second),
  `tcp.connect_blackhole_bounded` (a connect that is never answered must end within 90 s),
  and the same HTTP and socket cases again afterwards to show the ESP32 still works.
- **Limit:** the 30 second response timeout in `espcp_queue_message()` is reached only when the
  ESP32 does not answer a request at all. The blackhole case holds an ESP32 worker but the ESP32
  still answers, so it does not prove that path; that needs an ESP32 that stops responding.

## CellTests

Needs an OS built with `CONFIG_MEADOW_CELL_INCLUDE_IN_BUILD=y`, a BG95-M3 click board on mikroBUS 1
(COM1, enable pin A3) and, for `cell.gnss_fix`, a GPS antenna with a view of the sky. The GNSS and
signal quality cases do not need a SIM: they run on the CMUX AT channel, independently of the data link
(see `Documentation/Cell.md`).

- **Config:** the app ships `meadow.config.yaml` (cell interface, the modem pins reserved, and
  `InternalDebug` with only `TraceLevel: 2`) and always ships `cell.config.yaml`. COM1 belongs to
  the modem, so there is no `Uart1Use`; for the syslog, build the OS with `build.sh --dualcdc`
  (without `--nsh`) and read the second USB CDC port. The deploy replaces the board's
  `meadow.config.yaml`, so restore the usual one afterwards.
- **SIM:** without one the two data link cases are skipped. With one, point `MEADOW_CELL_CONFIG`
  at a `cell.config.yaml` that has the carrier's APN and credentials. The app then ships it in place
  of the placeholder and requires the link. For an EE (UK) SIM, use `cell.ee.config.yaml` in this
  folder. It holds EE's public, published settings (APN `everywhere`, PAP), and passed 6/6 on
  2026-10-27:

  ```bash
  MEADOW_CELL_CONFIG=tests/managed/CellTests/cell.ee.config.yaml tools/testrunner/testrunner.py --test celltests
  ```

  For a SIM on a private APN, point `MEADOW_CELL_CONFIG` at a local file instead of adding its
  credentials to the repository.
- **Mono must be running:** the cell code (CMUX, pppd and the AT channel) starts only after Mono
  starts. Check that the boot banner says `Mono:Enabled`.
- **Cases:**
  - `cell.adapter`: the cell adapter exists.
  - `cell.connected` (SIM only): the PPP link comes up within 300 s (`IsConnected`).
  - `cell.http_get` (SIM only): an HTTP GET of `http://example.com/` over the link, which tests the
    network DNS servers and TCP through `ppp0`.
  - `cell.http_large` (SIM only): an 8 KB download (`http://httpbin.org/bytes/8192`) and a 4 KB
    upload (`http://httpbin.org/post`), so that full-size (about 1400-byte) IP packets cross the PPP
    link in both directions. About 20 KB of data per run.
  - `cell.socket_linger` (SIM only): reads `Socket.LingerState`, sets `LingerOption(true, 0)` and
    connects, as MQTTnet does (issue #718).
  - `cell.mqtt_connect` (SIM only): an MQTTnet connect and disconnect to `broker.hivemq.com:1883`
    (plain MQTT, a few hundred bytes).
  - `cell.connect_cancel` (SIM only): closes 24 pending `ConnectAsync` and 6 pending blocking
    `Connect` calls to an unrouted address (192.0.2.1), then checks that connects still work, at once
    and after 10 s and 30 s (issue #716). Each blocking cancel takes about 46 s (see
    `Documentation/Cell.md`), so the case takes about 5 minutes.
  - `cell.gnss_output`: `FetchGnssAtCmdsOutput` returns NMEA (`$GPGGA` and `$GPRMC`) within 300 s.
    It does not need a fix. The request waits up to 120 s for a fix, so without one a run takes about
    two minutes. The data link carries no traffic during the request, so the data link cases run
    first.
  - `cell.gnss_fix`: GGA fix quality 1 or more and RMC status `A`. Reports only the quality, the
    satellite count, the HDOP and the GSA fix mode, because the NMEA contains the bench's
    position.
  - `cell.signal_quality`: `GetSignalQuality` returns a value other than NoSignal. This is
    a known failure without a SIM; see the manifest.
- **Limit:** the managed requests need CMUX to be running. If the modem never answers `AT` at
  start-up, or CMUX fails to start, the AT channel is not started and the managed calls wait until their own
  timeouts.

## SoakTests

Finds the faults that short tests miss: runtime restarts (Meadow_Issues#901), leaks, socket
exhaustion, flash damage and bus hangs. It has its own manifest, so the nightly run and
`manifest.yaml` do not run it.

```bash
tools/testrunner/testrunner.py --manifest tests/managed/soak.yaml                 # 2 windows of 300 s for each scenario, about 70 minutes
MEADOW_SOAK_WINDOWS=1 MEADOW_SOAK_WINDOW_S=120 MEADOW_SOAK_SERVER=<bench server> \
    tools/testrunner/testrunner.py --manifest tests/managed/soak.yaml             # a short check, about 15 minutes
MEADOW_SOAK_SCENARIOS=http MEADOW_SOAK_WINDOWS=12 \
    tools/testrunner/testrunner.py --manifest tests/managed/soak.yaml             # an hour of HTTP alone (Meadow_Issues#901)
```

The scenarios run one at a time, in the order of `Registry`, so that a failure belongs to one of
them. Between two scenarios the app forces a GC and waits 10 s, so that the ESP32 frees its
sockets. The only combined load is inside `block`.

- **Fixtures:** the board on WiFi (as for EspcpTests), a web server on port 80 and
  `EspcpTests/idle_server.py` on port 8081, both at `MEADOW_SOAK_SERVER`. The web server queues 5
  connections, so the test holds at most 4 open to it.
- **Settings:** environment variables of the runner, which reach the build as MSBuild properties
  and are read by the app from its assembly metadata. An unset one gives the default.

  | Variable | Default | Meaning |
  |---|---|---|
  | `MEADOW_SOAK_WINDOWS` | 2 | windows for each scenario |
  | `MEADOW_SOAK_WINDOW_S` | 300 | window length in seconds |
  | `MEADOW_SOAK_SCENARIOS` | all | comma list of scenario ids |
  | `MEADOW_SOAK_SERVER` | 192.168.1.65 | address of the web server and `idle_server.py` |
  | `MEADOW_SOAK_HTTPS_URL` | `https://example.com/` | HTTPS endpoint, requested every 30 s: use one that you own for long runs |

- **Output:** the app waits 40 s before `BEGIN` (Meadow_Issues#902), prints its settings and one
  `TESTRUN CASE soak.<id>.wNN` line for each window of each scenario, and one
  `TESTRUN INFO metric s=<id> w=NN ...` line after each window: `gc_bytes` (after a forced
  collect), `cpu_pct` and the reset reason, with the reset and power-cycle counters as the change since a `s=start w=00` line printed after `BEGIN` (each `meadow app run` raises the counters by 2). A disabled scenario has no CASE lines.
  A second `BEGIN`, or the Mono start line after `BEGIN`, gives verdict `restart`. This needs a
  restart that the CLI stays attached through: after a board reset `meadow` 3.0.4 prints nothing
  more, and the test ends with a timeout.
- **`mallinfo` is not a metric.** `PlatformOS.GetMemoryAllocationInfo()` is in the code behind
  `MetricsMallinfo` (false). On OS 2.999.53.37 the runtime hung in the window after a call while
  HTTP traffic ran: the HTTP scenario alone, 3 windows of 60 s, hung in window 2 with it and
  passed without it. Set it to true to reproduce.
- **Scenarios:** `Registry` in `MeadowApp.cs` lists them. Each limit is a named constant at the top
  of `Scenarios.cs`.

  | Id | What it does | Fails when |
  |---|---|---|
  | `http` (S1) | HTTP GET to the server every 2 s, HTTPS GET every 30 s | over 1% of requests fail, or an HTTPS request fails twice running |
  | `gc` (S2) | allocation churn: small objects, strings, a rebuilt tree, large arrays, finalizers, weak references (see "What gc allocates") | memory after a forced collect is over 1 MB above the baseline at the end of window 1 (window 1 is warm-up), or `OutOfMemoryException` |
  | `block` (S3) | a thread blocked in `Socket.Receive` on `idle_server.py`, a thread in a tight loop (6 s on, 4 s off), the S1 traffic and the S2 churn beside them, `GC.Collect` timed every 15 s | a collect takes over 2 s, the traffic fails as in S1, or the blocked thread is not released at the end |
  | `sockets` (S4) | a burst of 24 connects to `idle_server.py` (more than the ESP32's 15, so some fail and are counted, `unknown` counts "Unknown socket error"), all closed, then 4 connects to the web server and one GET; once in the first burst, a connect to 10.255.255.1:81 that is cancelled at 30 s | a connect gives "Unknown socket error", the recovery check fails, or the blackhole connect takes over 60 s |
  | `flash` (S5) | write, append, rename, read back with a CRC and delete files in `<data directory>/soak`, in 4 KB chunks: at most 256 KB written in a window, files at most 32 KB, the directory emptied at start and end | a read-back differs, or the directory is not gone |
  | `i2c` (S6) | raw chip-ID reads on I2C bus 1: BME688 (0x76, register 0xD0, 0x61) and BMI270 (0x68, register 0x00, 0x24), every 100 ms | an exception or a wrong ID |
  | `gpio` (S7) | 500 toggles of an output with an interrupt input that counts the edges | the counts differ. `SKIP` without the jumper |
  | `ap` (S8), `power` (S9) | not written: they need a switchable access point and a USB power switch, with a host-side driver. One `TESTRUN INFO disabled <id>` line, no CASE lines | |

- **What gc allocates:** a loop allocates 500 small arrays and strings (32 to 127 bytes), rebuilds a
  tree of 255 nodes of 64 bytes, replaces one 16 KB array in a ring of four, makes 50 finalizable
  objects and keeps a weak reference to a 1 KB array. About 140 KB is live and about 90 KB is garbage
  in each loop, one loop every 50 ms. The GC heap on this OS is capped at 8 MB (`max-heap-size=8m`,
  `soft-heap-limit=4m` in `apps/system/mono/mono_main.c`). The sizes stay below 48 KB because on this
  OS a contiguous array of more than about 40 to 64 KB fails with `OutOfMemoryException` after a short
  while, with 15 to 20 MB free (the largest free block is that small).
- **S7 jumper:** wire MikroBus 1 socket pin PWM (STM32 PB8) to pin INT (PC2) on the Project Lab
  (from `ConnectorProviderV3` in the `Meadow.ProjectLab` package; no Project Lab part uses them).
  The scenario drives the output high and low and reads the input twice, and skips if the input
  does not follow. Without a mikroBUS board on socket 1, nothing else is on those pins.
- **S6 limits:** only the two parts that answer chip-ID reads without a driver. SPI is not covered:
  the display is write-only.
- **Not covered:** `fd_max` (the spec's idea of a file descriptor high-water mark) has no managed
  API on this OS, so it is not a metric. S4 does not run a server that resets connections.
- **Flash wear:** at most 256 KB written per window, so at most 512 KB in a default run.
