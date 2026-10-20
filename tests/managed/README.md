# Managed tests

Meadow apps that test the OS and the Mono runtime on the board. `tools/testrunner/testrunner.py`
runs them; `manifest.yaml` lists them, with their Mono modes and known failures. Each app prints
the output contract described in `tools/testrunner/README.md`.

| App | What it covers |
|---|---|
| `MiniTests` | Mono's mini test suite (issue #726): 733 JIT regression tests in 10 suites, under the JIT and the interpreter |
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
- **Constants:** `LocalServer` (192.168.1.65, a small web page) and `Blackhole` (10.255.255.1, an
  address nobody answers) are at the top of `MeadowApp.cs`.
- **Cases:** `wifi.connected`, `socket.send_timeout_default` (reads SO_SNDTIMEO back with
  `GetSocketOption`, expects 10 s; currently a known failure, see the manifest), `http.get_local`, `http.repeat_local_x30`, `tcp.connect_close_x40` (4 sockets at a time: the bench web server queues only five connections),
  `tcp.connect_blackhole_bounded` (a connect that is never answered must end within 90 s),
  and the same HTTP and socket cases again afterwards to show the ESP32 still works.
- **Limit:** the 30 second response timeout in `espcp_queue_message()` is reached only when the
  ESP32 does not answer a request at all. The blackhole case holds an ESP32 worker but the ESP32
  still answers, so it does not prove that path; that needs an ESP32 that stops responding.
