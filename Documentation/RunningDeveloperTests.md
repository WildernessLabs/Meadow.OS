# Running Developer Tests

This document describes how to build and run the developer unit tests in Meadow.OS.

## Overview

Meadow.OS includes a variety of unit tests for both kernel-space and user-space components. These tests are used to verify the functionality of various hardware peripherals and system services.

The tests are organized into two main areas:
- **Kernel-space tests:** Located in `F7MeadowOS/kerneltests/`.
- **User-space tests:** Located in `apps/system/hcom/tests/`.

## Building Tests into the System

Tests are not built into the system by default to save space. You can enable specific tests during the build process using the `build.sh` script with the `-u` or `--unittests` flag.

### Enabling Tests via build.sh

To enable one or more tests, provide a comma-separated list of test identifiers to the `build.sh` script:

```bash
./build.sh -u=esp,ethernet,sdcard
```

### Available Test Identifiers

The following identifiers can be used with the `-u` flag:

| Identifier | Description | Kconfig Option |
|------------|-------------|----------------|
| `esp` | ESP32 Coprocessor tests | `ESP_TESTS` |
| `sqlite` | SQLite database tests | `EXAMPLES_SQLITE_TESTS` |
| `snprintf` | `snprintf` functionality tests | `SNPRINTF_TESTS` |
| `gpio` | GPIO peripheral tests | `GPIO_TESTS` |
| `overload` | MCU stress/overload tests | `MCU_OVERLOAD_TESTS` |
| `bbr` | Battery Backed Register tests | `BBR_TESTS` |
| `chat` | Ethernet Chat tests | `CHAT_TESTS` |
| `ethernet` | Ethernet peripheral tests | `ETHERNET_TESTS` |
| `bg77` | BG77 Cellular modem tests | `BG77_TESTS` |
| `iso8601` | ISO8601 time parsing tests | `ISO8601_TESTS` |
| `power` | Power management tests | `POWER_MANAGEMENT_TESTS` |
| `sdcard` | SD Card peripheral tests | `SD_CARD_TESTS` |
| `tensorflow` | TensorFlow Lite for Microcontrollers tests | `TENSORFLOW_TESTS` |
| `misc` | Miscellaneous system tests | `QUICK_MISC_TESTS` |
| `dirmgmt` | Directory management tests | `DIR_MGMT_TESTS` |
| `adc` | Analog to Digital converter tests | `ADC_TESTS` |
| `dac` | Digital to Analog converter tests | `DAC_TESTS` |
| `spidma` | SPI DMA tests | `SPI_DMA_TESTS` |
| `i2c` | I2C bus scan tests (I2C1 and I2C3) | `I2C_TESTS` |
| `sotest` | Dynamic loading (dlopen/dlsym/dlclose) tests | `SOTEST_TESTS` |

## Test Configuration

Additional parameters can be passed to a unit tests using the `unittests.config.yaml` file.  This file should be written to the root file system of a Meadow device.  The file should look something like:

```yaml
TestParameters
    Parameter1: value1
    Parameter2: value2
```

Up to 10 parameters can be defined, `Parameter1` through to `Parameter10`.  The parameters are optional and are stored in the `unit_tests_configuration_t *unit_test_configuration` variable as string values.  It is the responsibility of the test to interpret the parameter meaning.

### ESP Unit Test Configuration

The ESP tests currently uses up to five parameters:

```yaml
TestParameters
    Parameter1: SSID
    Parameter2: Password
    Parameter3: Server IP Address
    Parameter4: Server Port
    Parameter5: Server Resource
```

## Test Infrastructure

### Kernel-Side Dispatcher

Kernel-side tests are managed by a dispatcher located at `F7MeadowOS/kerneltests/meadow_kt_dispatcher.c`. This dispatcher maintains an array of available tests and routes execution requests to the appropriate test implementation.

Individual kernel-side test functions (e.g. `meadow_kt_adc_tests`, `meadow_kt_spi_dma_tests`,
`meadow_kt_sd_card_tests`, and the rest of the `_kernelTests[]` table) are called directly by the
dispatcher via a function pointer and are not themselves registered as syscalls in
`nuttx/syscall/syscall.csv` - only `meadow_kt_dispatcher` needs a syscall of its own, since it is
the only one of these functions ever called from user space (see the I2C test section below for
the same reasoning applied to a newer test).

### User-Side Dispatcher

User-side tests and the bridge to kernel tests are managed in `apps/system/hcom/tests/developer_tests.c`. 

When a test is requested:
1. The user-side dispatcher first checks if it is a user-space test.
2. If the test is not found in user-space, it attempts to call the kernel-side dispatcher via a system call (`meadow_kt_dispatcher`).

## Triggering Tests

Tests are typically triggered via HCOM (Hardware Communication) messages from a host PC/Mac, usually via the Meadow CLI.

### From the Host

When using the Meadow CLI, you can trigger developer tests with a command such as:

```bash
meadow developer -p <test number> -v <value for the test> -t <timeout>
```

* &lt;test number&gt; is the test to be run.
* &lt;value for the test&gt; `uint32_t` value to be passed to the test.  It is up to the test how (or even if) this value is processed.
* &lt;timeout&gt; Timeout in seconds that the test will run for.  This is useful for running tests that will exceed the default CLI timeout, for instance consuming 10,000 web pages.

The test numbers can be found in the file `F7MeadowOS/include/meadow/meadow_unit_test_framework.h`

### List Available Tests

If you trigger a test with `meadow developer -p 0`, the system will output a list of all tests currently compiled into the firmware to the host.

## Dynamic Loading Tests (sotest)

The `sotest` test exercises NuttX's `dlopen`/`dlsym`/`dlclose` loadable module support
(`CONFIG_LIBC_DLFCN`), confirmed working on real Meadow F7 hardware. Unlike the other
developer tests, it requires extra setup because the loadable modules are separate files
that must exist on the target filesystem before the test runs.

### Prerequisites

* Build with `./build.sh -u=sotest`. This enables `CONFIG_SOTEST_TESTS` (the test itself)
  and `CONFIG_EXAMPLES_SOTEST` (builds the loadable module binaries).
* `CONFIG_LIBC_DLFCN=y` and `CONFIG_ELF=y` must be enabled in the board defconfig (already
  the case for `meadow_os`/`stm32f777zit6-meadow`). These select the `LIBC_ELF` chain that
  `dlopen` depends on under `CONFIG_BUILD_PROTECTED`.
* Two loadable module binaries, `modprint` and `sotest` (upstream NuttX's own
  `apps/examples/sotest` smoke test, bare filenames with no `.so` extension), must be copied
  to `CONFIG_EXAMPLES_SOTEST_BINDIR` (`/meadow0`, the root of the LittleFS QSPI filesystem)
  on the device before triggering the test. They are built alongside the firmware but are
  **not** flashed automatically - push them to the device separately (e.g. via the Meadow
  CLI file-copy command) after flashing.

### What It Verifies

`apps/system/hcom/tests/sotest_tests.c` (`meadow_sotest_tests`, `MEADOW_TEST_SOTEST` = 24 in
`meadow_unit_test_framework.h`) registers an exported-symbol table via `dlsymtab()` using the
auto-generated `g_sot_exports` table, then:

1. Opens `modprint`, then opens `sotest` (which depends on `modprint`).
2. Resolves and calls `testfunc1`/`testfunc2`/`testfunc3` and their matching `g_msgN` symbols
   via `dlsym` on the `sotest` handle.
3. Confirms `dlclose(modprint)` fails while `sotest` still depends on it, then closes
   `sotest` before closing `modprint`.

A `PASS`/`FAIL` result is written to the syslog (`LOG_MTEST`) at the end of the run.

### Known Gotcha: Reconfigure Required

Enabling `CONFIG_LIBC_DLFCN` pulls in a kernel syscall proxy (`up_coherent_dcache`, used by
`libs/libc/elf/elf_bind.c` during module relocation under `CONFIG_BUILD_PROTECTED`). NuttX's
`nuttx/syscall/proxies|stubs/Make.defs` compute their source file lists via `$(shell ls *.c)`
at Makefile-parse time, so newly generated proxy/stub files are only picked up by a fresh
`./build.sh --configure` - a plain rebuild on an already-configured tree will not see them.

## SQLite Tests

The `sqlite` test exercises SQLite running against a custom application-supplied VFS
(`sqlite_meadow_vfs.c`), since NuttX's LittleFS/QSPI storage on this board doesn't support the
POSIX file locking SQLite's default unix VFS assumes. See `apps/database/sqlite/sqlite_meadow.h`,
`sqlite_meadow.c`, and `sqlite_meadow_vfs.c` for the VFS registration and implementation.

### Prerequisites

* Build with `./build.sh -u=sqlite`. This enables `CONFIG_EXAMPLES_SQLITE_TESTS`, which compiles
  in `apps/system/hcom/tests/meadow_sqlite_tests.c`.
* A tab-delimited file of first/last names, `/meadow0/TabDelimited200Names.txt`, must exist on
  the device's filesystem before running the "populate database" test (test value 1) - it is not
  bundled with the firmware and must be copied to the device separately (e.g. via the Meadow CLI
  file-copy command), the same way the `sotest` loadable modules are pushed to `/meadow0`. A
  ready-to-use copy (200 first/last name pairs) is provided at
  [`TabDelimited200Names.txt`](TabDelimited200Names.txt) - copy it to the device as
  `/meadow0/TabDelimited200Names.txt`.
* The test database itself, `/meadow0/Test1.db`, is created automatically by the test (dropping
  and recreating its `Names` table each run) - no setup needed for it.

### What It Verifies

`hcom_meadow_sqlite_tests` (`MEADOW_TEST_SQLITE` = 3 in `meadow_unit_test_framework.h`) dispatches
on the `-v` value passed from the CLI:

* `-v 1` - **Populate database.** Registers/opens the Meadow VFS, drops and recreates the `Names`
  table in `/meadow0/Test1.db`, then reads first/last name pairs from
  `/meadow0/TabDelimited200Names.txt` and inserts each as a row inside a single transaction.
* `-v 2` - **Query 1.** Runs `SELECT * FROM Names ORDER BY last_name` against the database
  populated by test 1 and logs each row (`id`, `first_name`, `last_name`) via syslog.
* `-v 3` - **Query 2.** Currently a no-op placeholder (`execute_sqlite_query2_experiment`) -
  reserved for a future test.

Errors at any step (VFS registration, table create/drop, file open, insert, or query) are logged
via `syslog(LOG_MTEST, ...)`.

## I2C Bus Scan Tests

The `i2c` test exercises the two I2C buses Meadow F7 exposes to board code - I2C1 (the
Feather bus, present on both the Feather form factor and the CCM) and I2C3 (an additional
bus present only on the CCM).

### Prerequisites

* Build with `./build.sh -u=i2c`. This enables `CONFIG_I2C_TESTS`, which compiles in
  `F7MeadowOS/kerneltests/meadow_i2c_tests.c`. Unlike `sotest`, no reconfigure is needed -
  the test is only ever called from kernel space (via `meadow_kt_dispatcher`, which already
  has its own syscall), so it does not need a syscall of its own.

### What It Verifies

`meadow_kt_i2c1_scan_tests` (`MEADOW_TEST_I2C1_SCAN` = 25) and `meadow_kt_i2c3_scan_tests`
(`MEADOW_TEST_I2C3_SCAN` = 26), both in `meadow_unit_test_framework.h`, scan their respective
bus for connected devices:

* `-v 0` (default) - probes the standard 7-bit address range, `0x08`-`0x77`.
* `-v 1` - probes the full `0x00`-`0x7f` range, including reserved addresses.

The scan is fixed at 100 kHz (I2C standard mode) and is not affected by the `-v` value beyond
selecting the address range. Each probed address is a single-byte `I2C_M_READ` transfer; a
successful transfer means a device ACK'd and is logged as present. Results - not a pass/fail
verdict - are written to the syslog (`LOG_MTEST`): one line per device found (address in hex
and decimal), or a single "No devices found" line if the bus is empty.

## SPI DMA Tests

The `spidma` test (`MEADOW_TEST_SPI_DMA` = 17 in `meadow_unit_test_framework.h`) exercises
DMA-driven SPI transfers on SPI5 by running a loopback test: it clocks a pseudo-random pattern
out on COPI (MOSI) while simultaneously capturing on CIPO (MISO), then compares the two buffers
byte-for-byte.

### Prerequisites

* Build with `./build.sh -u=spidma`. This enables `CONFIG_SPI_DMA_TESTS`, which compiles in
  `F7MeadowOS/kerneltests/meadow_spi_dma_tests.c`.
* **A physical jumper wire connecting COPI to CIPO on SPI5 is required.** The test does not
  enable any hardware/internal loopback mode in the STM32F7 SPI peripheral or driver - it
  relies entirely on an external wire looping the output back into the input. On the
  STM32F777ZIT6, SPI5 is mapped (see `board.h`, `GPIO_SPI5_MOSI`/`GPIO_SPI5_MISO`) to:
  - COPI (MOSI): PF9
  - CIPO (MISO): PF8
  - SCK: PH6

  Without the jumper, CIPO floats and is sampled as a constant `0xff` for every byte
  transferred, which the test reports as a transfer failure starting at offset 0 for every loop.

### Running It

```bash
meadow developer -p 17 -v <n>
```

The `-v` value selects the buffer size for the transfer (see the `switch` in
`meadow_kt_spi_dma_tests` for the full list) - `0`-`13` step through common sizes from 4 KB down
to 1 byte, `14`-`32` step up in 10 KB increments to 200 KB, and `33`-`37` specifically probe
around the F7 DMA controller's 65535-item single-transaction limit (see ARM reference manual
section 8.3.6/8.3.16) to exercise the driver's split-transfer workaround for buffers that exceed
it.

### What It Verifies

For each configured buffer size, the test fills a TX buffer with a repeating pattern, exchanges
it with the RX buffer via `SPI_EXCHANGE`, and compares the two. Results are written to the
syslog (`LOG_MTEST`): a successful loop reports the number of bytes transferred; a failed loop
reports the offset of the first mismatch along with total error/success byte counts.

### Sample Results for SPI DMA Tests

Results will be written to the developer / debug console (when enabled).

The following shows the sample output when the COPI and CIPO lines are not connected (a failed test):

```
[   23.692000] [10] Developer test param: 17 - value: 0
[   23.692000] [10] Checking for userspace test param: 17 - value: 0
[   23.692000] [10] Checking for kernel test param: 17 - value: 0
[   23.692000] [10] Number of kernel tests: 1
[   23.694000] [10] Unit test configuration file could not be processed. Error: Could not open file
[   23.698000] [10] F7MeadowOSLink/kerneltests/meadow_spi_dma_tests.c@339-DMA test, SPI5, allocate 4096 bytes
[   23.701000] [ 1] -->Loop:00 - Error sent:4096(0x00001000) bytes, err@bufOff:0(0x00000000), txBuff:0xc000c0e0, rxBuff:0xc000d0e8 [txBuff:0x00(0xc000c0e0) != rxBuff:0xff(0xc000d0e8)]
[   23.702000] [ 1] Loop:00 - Transfer failed. First offset:0, total errors:1, total success:0
```

The following sample output shows a number of successful tests:

```
[  312.225000] [10] Developer test param: 17 - value: 0
[  312.225000] [10] Checking for userspace test param: 17 - value: 0
[  312.225000] [10] Checking for kernel test param: 17 - value: 0
[  312.226000] [10] Number of kernel tests: 1
[  312.227000] [10] Unit test configuration file could not be processed. Error: Could not open file
[  312.483000] [10] F7MeadowOSLink/kerneltests/meadow_spi_dma_tests.c@339-DMA test, SPI5, allocate 4096 bytes
[  312.486000] [ 1] Loop:01 - Successful transfer of:4096 bytes
[  351.156000] [10] Developer test param: 17 - value: 10
[  351.157000] [10] Checking for userspace test param: 17 - value: 10
[  351.157000] [10] Checking for kernel test param: 17 - value: 10
[  351.157000] [10] Number of kernel tests: 1
[  351.158000] [10] Unit test configuration file could not be processed. Error: Could not open file
[  351.163000] [10] F7MeadowOSLink/kerneltests/meadow_spi_dma_tests.c@339-DMA test, SPI5, allocate 8 bytes
[  351.163000] [ 1] Loop:01 - Successful transfer of:8 bytes
[  359.158000] [10] Developer test param: 17 - value: 20
[  359.158000] [10] Checking for userspace test param: 17 - value: 20
[  359.158000] [10] Checking for kernel test param: 17 - value: 20
[  359.159000] [10] Number of kernel tests: 1
[  359.160000] [10] Unit test configuration file could not be processed. Error: Could not open file
[  359.355000] [10] F7MeadowOSLink/kerneltests/meadow_spi_dma_tests.c@339-DMA test, SPI5, allocate 81920 bytes
[  359.420000] [ 1] Loop:01 - Successful transfer of:81920 bytes
```

## Adding New Tests

To add a new kernel-space test:
1. Create your test file in `F7MeadowOS/kerneltests/`.
2. Add your test function to the `_kernelTests` array in `meadow_kt_dispatcher.c`, guarded by an appropriate `CONFIG_` option.
3. Add your source file to `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/Make.defs`.
4. Update `build.sh` to include a new identifier that enables your `CONFIG_` option.
