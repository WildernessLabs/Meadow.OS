# Developer Commands and the NSH Shell

## Overview

Two facilities exist for developers working on the device itself, as opposed to applications running on
it:

- The **developer request** (`DEVELOPER`, `0x01f8`) lets the host run a built-in test by number, in user
  space or in the kernel, and see its output.
- **NSH**, the NuttX shell, can be started on a spare serial port (or a second USB port) for interactive
  work.

Both are optional. Which tests exist depends on the build options chosen with `build.sh --unittests=...`
([RunningDeveloperTests.md](../RunningDeveloperTests.md)); NSH exists only in a build made with NSH
enabled (`CONFIG_SYSTEM_NSH`, see [SettingupMeadowNSH.md](../SettingupMeadowNSH.md)). Neither is part of
the shipping configuration.

## The Developer Request

### Parameters

| Field of the request | CLI option | Meaning |
|----------------------|-----------|---------|
| `extraData` (16 bits) | `-p` | The test number. `0` lists the available tests |
| `userData` (32 bits) | `-v` | A value passed to the test; its meaning is the test's own |

The router replies `TEXT_ACCEPTED`, calls `hcom_developer_tests_developer(extraData, userData)`, and replies
`TEXT_CONCLUDED` ([RequestRouting.md](RequestRouting.md)). The call runs on the `HcomProc` thread and
**blocks it** until the test returns, so a long test holds up all other requests.

### What the function does

```
   hcom_developer_tests_developer(param, value)
        |
        |  param == 0 --> send "Available tests:" and one line per test built in
        |
        |  otherwise:
        |    print "Executing <description>" if the test is in the name table
        |    1. user-space table  _userspaceTests[]   -> run the test method here
        |    2. not found, and CONFIG_KERNEL_TESTS_SYSCALL set
        |         -> meadow_kt_dispatcher() (a syscall into the kernel)
        |              reloads the unit-test configuration file, then runs the kernel test
        |    3. report: completed successfully / cannot be found / invalid configuration / unknown error
```

The name table (`_testNames[]`, used for the list and the "Executing" line) covers both user-space and
kernel tests, because both are switched by the same build options. The test methods themselves are in two
other tables: `_userspaceTests[]` in `apps/system/hcom/tests/developer_tests.c`, and `_kernelTests[]` in
`F7MeadowOS/kerneltests/meadow_kt_dispatcher.c`.

An unknown test number is not an error to the protocol: the host gets "Test N cannot be found. Check that
the test has been compiled into the system." in an ordinary message.

### How output reaches the host

`developer_tests_report()` sends **every** line, including the list of tests and the success message, as
`TEXT_ERROR`. This appears to be so that the CLI displays them; the type is not an indication of failure.
Each line goes through the simple string sender, so it is cut at the first CR or LF
([WireProtocol.md](WireProtocol.md#text-bodies)). Where the `meadowtest` command is built in, the same text
is also printed to the NSH console with `printf`.

Kernel tests write their own messages, either to syslog or with `syslogToHost()` (`kerneltests/meadow_kt_log.c`),
which sends a `TEXT_INFORMATION` message to the host through the kernel-to-user channel
([TraceSyslog.md](TraceSyslog.md)). The latter exists for tests that use pins otherwise reserved for the
syslog UART.

### Test numbers

| Range | Area | Examples |
|-------|------|----------|
| 1 to 26 | Miscellaneous | 1 snprintf, 2 GPIO, 3 SQLite, 6 battery-backed registers, 7 SD card, 8 power management, 9 ISO 8601, 10 quick/misc, 13 directory management, 14 ADC, 15 DAC, 16 interrupts, 17 SPI DMA, 18 rotary encoder, 19 frequency measurement, 20 cell network ([Cell.md](../Cell.md#native-tests)), 22 kernel/user traverse channel, 23 GPIO counter, 25 and 26 I2C bus scans |
| 800 to 870 | PWM | one test per pin and duty cycle (D02 to D15, A01 to A04, at 20/40/60/80 %), 868 all on, 869 all off, 870 cycle all; `-v` is the frequency in kHz |
| 900 to 907 | Meadow OS | 900 user-space assert, 901 user-space board reset, 902 kernel assert, 903 to 905 battery-backed domain tests, 906 CLI timeout, 907 get the test configuration |
| 1000 to 1002 | ESP32 | all ESP32 tests, start WiFi, web page load (`-v` is the count) |
| 1100 to 1105 | Bluetooth | start service A, B, C, D, F7, and stop |
| 1200, 1201 | Ethernet | all tests, web page load |
| 1600 to 1602 | QSPI flash | init/erase, write, read |

Test numbers are defined as `MEADOW_TEST_*` in `F7MeadowOS/include/meadow/meadow_unit_test_framework.h`.
Test 0 on a device lists exactly what that build contains. The result codes are `TEST_ERR_OK` (0),
`TEST_ERR_NOT_FOUND` (1) and `TEST_ERR_INVALID_CONFIG` (2).

### The test configuration file

Before each kernel test, the dispatcher frees and re-reads the unit-test configuration
(`process_unit_tests_configuration_file()`), `/meadow0/unittest.config.yaml`, so edits take effect without a
restart. ([RunningDeveloperTests.md](../RunningDeveloperTests.md) describes this file.) Test 907 asks the kernel for the parsed configuration. A malformed file gives
`TEST_ERR_INVALID_CONFIG`.

### The `meadowtest` shell command

In an NSH build, the `meadowtest` command (`apps/system/meadowtest/meadowtest_main.c`) calls the same
function from the shell:

```
   nsh> meadowtest -p 0           list the tests
   nsh> meadowtest -p 23 -v 1     run test 23 with value 1
```

It rejects a negative or non-numeric value, and a test number above 65,535 (the request field is 16 bits).

## NSH

### Why it needs special handling

The Mono runtime sends `Console.Write` to standard output, so HCOM redirects standard output and standard
error for the Mono task ([MonoLifecycle.md](MonoLifecycle.md#output-redirection)). To keep that redirect from
reaching every other task, the build is configured so that new tasks do **not** inherit the parent's
standard input and output. That is exactly what NSH normally relies on, so NSH needs help: HCOM
starts it as its own task and gives it the right serial port explicitly. The NuttX configuration
(an alternate console device for NSH's input) is set up by the build scripts and described in
[SettingupMeadowNSH.md](../SettingupMeadowNSH.md).

### The serial port

`HCOM_DIAG_NSH_SERIAL_DEVICE` (in `hcom_common.h`) names the device NSH uses:

| Build | Device |
|-------|--------|
| NSH (default) | `/dev/ttyS3`, USART6 |
| `build.sh --dualcdc --nsh` | `/dev/ttyACM1`, the second USB serial port ([DualCdcAcm.md](../DualCdcAcm.md)) |

### Starting NSH

`hcom_diag_misc_launch_nsh(userData)`:

1. **Restore the UART pins** (UART builds only). Mono reconfigures every GPIO when it starts, so the
   function asks the kernel to put the pins of the NSH UART back (`hcom_via_nx_restore_uart_reconfig()`);
   an unknown device name is an error and nothing starts. For the USB port there are no pins to restore.
2. If NuttX's alternate console is configured, the device must equal `CONFIG_NSH_ALTSTDIN`; otherwise an
   error is logged and nothing starts.
3. If NSH was already started this run: `TEXT_INFORMATION` "NSH already enabled".
4. If `userData` is 1: create the task `nsh` (priority `CONFIG_SYSTEM_NSH_PRIORITY`, stack
   `CONFIG_SYSTEM_NSH_STACKSIZE`) running `nsh_main_proxy()`, set the "enabled" flag, and send "NSH enabled
   until Meadow is restarted". Any other `userData`: "It is not possible to disable NSH".

In an NSH build, **HCOM starts NSH itself at the end of start-up** (`hcom_diag_misc_launch_nsh(1)`,
[UserspaceStartup.md](UserspaceStartup.md)), so a later `ENABLE_DISABLE_NSH` request normally finds it
already running and just says so. The CLI sets `userData` to 1 for its `--NshEnable` command.

`nsh_main_proxy()`, the task's entry point:

1. Opens the serial device for writing, retrying every 100 ms while it does not exist or the USB port has
   not yet been set up by the host (`ENOENT`, `ENOTCONN`); any other error ends the task.
2. `dup2()`s it onto standard output, and configures the terminal, then does the same for standard error.
3. Calls `nsh_main()`.

The terminal configuration is needed because NuttX's serial driver sets the line-handling flags only for the
console device, which is USART1 here, not the NSH port. Without it, Enter (a bare carriage return) would
never become the newline NSH waits for, typed characters would not be echoed, and NSH's own output would
not get carriage returns added. The code sets `ICRNL`, `OPOST | ONLCR`, and `ECHO | ISIG`. `ISIG` makes
Ctrl-C deliver SIGINT, which needs `CONFIG_TTY_SIGINT` in the build. Because the device echoes, the host's
terminal program must have **local echo off**.

### Replies in a build without NSH

When `CONFIG_SYSTEM_NSH` is not set, the request is answered `TEXT_INFORMATION` "NuttShell not available",
and nothing starts.

## Things to Know

- **Developer output is sent as `TEXT_ERROR`**, including the test list and "completed successfully". A
  host should not treat that type as failure here.
- **A test blocks request processing.** All tests run on `HcomProc`; a test that takes seconds stalls the
  host until it is done.
- **Test 0 shows what is built.** The list is generated from the build, not from a fixed table.
- **`ENABLE_DISABLE_NSH` is one-way and one-shot.** It cannot disable NSH, and once it has started (at
  boot, in an NSH build) it cannot be restarted without restarting the device. If the NSH task ends,
  because its device could not be opened, the "enabled" flag stays set and NSH cannot be started again until
  the device restarts.
- **The UART pins are restored before the "already enabled" check.** A second request, or a "disable"
  request, therefore reconfigures the NSH UART's pins again before replying that nothing will happen, which
  could disturb a session in progress.
- **NSH output is only on the NSH port.** Output to the NSH console is not sent to the host over HCOM,
  apart from the `meadowtest` mirror above.
- **A stale comment** in the router says `ENABLE_DISABLE_NSH` "currently does nothing"; it starts NSH in a
  build that has it.

## Source Map

| Concern | File |
|---------|------|
| The developer request and the user-space test table | `apps/system/hcom/tests/developer_tests.c` |
| The kernel test table and dispatcher | `F7MeadowOS/kerneltests/meadow_kt_dispatcher.c`, `F7MeadowOS/include/meadow/meadow_kernel_tests.h` |
| Test numbers and result codes | `F7MeadowOS/include/meadow/meadow_unit_test_framework.h` |
| The shell command | `apps/system/meadowtest/meadowtest_main.c` |
| Starting NSH | `apps/system/hcom/diag/hcom_diag_nsh_support.c` |
| NSH device name | `apps/system/hcom/hcom_common.h` (`HCOM_DIAG_NSH_SERIAL_DEVICE`) |
| Routing | `apps/system/hcom/comms/hcom_host_route.c` |

## Related Documents

[RunningDeveloperTests.md](../RunningDeveloperTests.md), [SettingupMeadowNSH.md](../SettingupMeadowNSH.md),
[DualCdcAcm.md](../DualCdcAcm.md), [RequestRouting.md](RequestRouting.md),
[MonoLifecycle.md](MonoLifecycle.md), [UserspaceStartup.md](UserspaceStartup.md).
