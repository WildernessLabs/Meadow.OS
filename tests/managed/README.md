# Managed tests

Meadow apps that test the OS and the Mono runtime on the board. `tools/testrunner/testrunner.py`
runs them; `manifest.yaml` lists them, with their Mono modes and known failures. Each app prints
the output contract described in `tools/testrunner/README.md`.

| App | What it covers |
|---|---|
| `MiniTests` | Mono's mini test suite (issue #726): 733 JIT regression tests in 10 suites, under the JIT and the interpreter |

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
