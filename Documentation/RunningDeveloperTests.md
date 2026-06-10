# Running Developer Tests

This document describes how to build and run the developer unit tests in Meadow.OS.

## Overview

Meadow.OS includes a variety of unit tests for both kernel-space and user-space components. These tests are used to verify the functionality of various hardware peripherals and system services.

The tests are organized into two main areas:
- **Kernel-space tests:** Located in `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/Meadow.OS/kerneltests/`.
- **User-space tests:** Located in `apps/system/hcom/tests/`.

## Building Tests into the System

Tests are not built into the system by default to save space. You can enable specific tests during the build process using the `build.sh` script with the `-u` or `--unittests` flag.

### Enabling Tests via build.sh

To enable one or more tests, provide a comma-separated list of test identifiers to the `build.sh` script:

```bash
./build.sh -u=esp,ethernet,sd_card
```

### Available Test Identifiers

The following identifiers can be used with the `-u` flag:

| Identifier | Description | Kconfig Option |
|------------|-------------|----------------|
| `esp` | ESP32 Coprocessor tests | `ESP_TESTS` |
| `sqlite` | SQLite database tests | `EXAMPLES_SQLITE_TESTS` |
| `snprintf` | `snprintf` functionality tests | `SNPRINTF_TESTS` |
| `gpio` | GPIO peripheral tests | `GPIO_TESTS` |
| `mcu_overload` | MCU stress/overload tests | `MCU_OVERLOAD_TESTS` |
| `bbr` | Battery Backed Register tests | `BBR_TESTS` |
| `chat` | Ethernet Chat tests | `CHAT_TESTS` |
| `ethernet` | Ethernet peripheral tests | `ETHERNET_TESTS` |
| `bg77` | BG77 Cellular modem tests | `BG77_TESTS` |
| `iso8601` | ISO8601 time parsing tests | `ISO8601_TESTS` |
| `power_management` | Power management tests | `POWER_MANAGEMENT_TESTS` |
| `sd_card` | SD Card peripheral tests | `SD_CARD_TESTS` |
| `tensorflow` | TensorFlow Lite for Microcontrollers tests | `TENSORFLOW_TESTS` |
| `misc` | Miscellaneous system tests | `QUICK_MISC_TESTS` |
| `dir_mgmt` | Directory management tests | `DIR_MGMT_TESTS` |
| `adc` | Analog to Digital converter tests | `ADC_TESTS` |
| `dac` | Digital to Analog converter tests | `DAC_TESTS` |
| `spi_dma` | SPI DMA tests | `SPI_DMA_TESTS` |

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

Kernel-side tests are managed by a dispatcher located at `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/Meadow.OS/kerneltests/meadow_kt_dispatcher.c`. This dispatcher maintains an array of available tests and routes execution requests to the appropriate test implementation.

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

The test numbers can be found in the file `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/Meadow.OS/include/meadow/meadow_unit_test_framework.h`

### List Available Tests

If you trigger a test with `meadow developer -p 0`, the system will output a list of all tests currently compiled into the firmware to the host.

## Adding New Tests

To add a new kernel-space test:
1. Create your test file in `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/Meadow.OS/kerneltests/`.
2. Add your test function to the `_kernelTests` array in `meadow_kt_dispatcher.c`, guarded by an appropriate `CONFIG_` option.
3. Add your source file to `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/Make.defs`.
4. Update `build.sh` to include a new identifier that enables your `CONFIG_` option.
