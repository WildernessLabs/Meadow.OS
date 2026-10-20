# Hardware test runner

`testrunner.py` deploys the managed test apps listed in `tests/managed/manifest.yaml` to the
attached board, once for each of their Mono modes. It watches each app's output, decides pass or
fail, and writes a run directory with the logs and results.

It is option A of the test runner plan: fixed rules, no LLM. It only deploys apps. It does not
flash the OS, the runtime or the ESP32 firmware, and it does not change any repository.

## Requirements

- Python 3 with PyYAML (`pip install pyyaml`).
- The `meadow-3.0` Meadow CLI, and a board running a Meadow.OS whose runtime matches it.
- The runtime checkout (`MEADOW_RUNTIME_DIR`) and the `meadow-3.0` Meadow.Core checkout
  (`MEADOW_CORE_DIR`), which the test apps build against.

## Running

```bash
export MEADOW_CLI="dotnet <meadow-3.0>/Meadow.CLI/Source/Meadow.CLI/bin/Release/meadow.dll"
export MEADOW_RUNTIME_DIR=<runtime checkout>        # default: ../runtime
export MEADOW_CORE_DIR=<meadow-3.0>/Meadow.Core     # default: ../../meadow-3.0/Meadow.Core

tools/testrunner/testrunner.py                      # every test, every mode
tools/testrunner/testrunner.py --test minitests     # one test, all its modes
tools/testrunner/testrunner.py --test minitests:interp
tools/testrunner/testrunner.py --list
```

The defaults are relative to the main checkout. A worktree is one directory deeper, so set the
variables there. `MEADOW_CLI` defaults to `meadow`.

The exit code is 0 when every test passes and 1 otherwise.

## Output contract for test apps

A test app prints these lines to the console. The CLI shows them with a source prefix such as
`stdout>`, which the runner removes.

```
TESTRUN BEGIN <suite> <app-version>
TESTRUN CASE <case-name> PASS|FAIL|SKIP <elapsed-ms> <detail>
TESTRUN INFO <text>
TESTRUN END failures=<n> cases=<n>
```

- **`BEGIN`:** the app started.
- **`CASE`:** one line per test case. The case name has no spaces.
- **`INFO`:** recorded in the result but does not affect the verdict.
- **`END`:** the app finished. Its counts must match the `CASE` lines.
- **Other output** is kept in the log as context.

After `END` the app must stay alive, as the Meadow apps do with a final `Task.Delay` loop. The
runner then stops the CLI.

## Manifest

```yaml
defaults:
    timeout_s: 900
modes:
    jit:    { options: "" }          # no MonoControl section
    interp: { options: "--interp" }
tests:
    - name: minitests
      project: tests/managed/MiniTests/MiniTests.csproj
      modes: [jit, interp]
      expected_s: 240                # noted in the result when a run takes over twice this
      timeout_s: 900
      known_failures:
          exceptions.test_0_ldflda_null_pointer: <reason>
      config: tests/managed/EspcpTests/meadow.config.yaml   # optional
```

`config` names the test's own `meadow.config.yaml` (for example one with a `Network` section). The
runner uses it as the base for each mode and replaces only its `MonoControl` section.

For each mode the runner writes a `meadow.config.yaml` with that mode's options. It passes the
file to the build in `MEADOW_TEST_CONFIG`, and the test app's project ships that file in place of
its own. A test app that takes part must do the same (see `tests/managed/MiniTests`).

## Verdicts

| Situation | Verdict (kind) |
|---|---|
| `END` seen, its counts match, and no `CASE` failed | pass |
| A `CASE` failed that is not in `known_failures` | fail (cases) |
| A `CASE` failed that is in `known_failures` | reported as KNOWN; does not fail the test |
| The CLI exited before `BEGIN` | fail (deploy) |
| The CLI exited after `BEGIN`, before `END` | fail (crash) |
| The CLI reported a build error, port error, CLI timeout or app failure | fail (build, port, cli-timeout or crash) |
| No `END` within `timeout_s` | fail (timeout) |
| The `END` counts do not match the `CASE` lines | fail (contract) |

## Run directory

Each run creates `test-runs/<date>-<time>/`, which git ignores. It holds:

- `run.json`: device versions, the source commits and dirty state of Meadow.OS, the runtime and
  Meadow.Core, the selected tests, and the totals.
- `events.jsonl`: one JSON line per progress event. The same events are printed to the terminal.
- `<test>-<mode>/output.log`: the CLI output, with WiFi credentials redacted.
- `<test>-<mode>/meadow.config.yaml`: the configuration deployed.
- `<test>-<mode>/result.json`: the verdict, every case, the INFO lines and the counts.
- `summary.md`: a table of the tests, plus each failed and known case.
