# The Configuration Manager

## Overview

Meadow.OS reads its settings from YAML files kept on the device's flash file system. The kernel parses them
once during start-up into a single in-memory object, `meadow_configuration_t`, which the rest of the kernel
reads under a lock. User space (HCOM, and the Mono runtime) never sees that object; it receives a **copy**.
This document describes the files, how they are parsed, what the defaults are, and how the settings reach
the rest of the system.

```
   /meadow0/meadow.config.yaml ---+
   (other .yaml files, below)     |      kernel                                   user space
                                  v   +----------------------------+      +---------------------------+
                          hcom_nx_config_init()  --->  meadow_configuration_t     |  meadow_os_deep_copy_config()
                          (once, from the startup     (one object, config_lock)  |  -> private copy, kumm heap
                           manager, step 11)          |                          |  ... use ...
                                  |                   |  MUPD_GET_SET_CONFIGURATION_VALUE
                                  |                   |  <------ Mono runtime (item by item)
                                  +-------------------+  meadow_os_config_free_resources()
```

## The Files

| File | Read | Purpose |
|------|------|---------|
| `/meadow0/meadow.config.yaml` | Once, at kernel start-up | The device configuration; the subject of this document |
| `/meadow0/wifi.config.yaml` | At ESP32 start-up, then **deleted** | Default WiFi access point and password, handed to the ESP32 (out of scope) |
| `/meadow0/cell.config.yaml` | At start-up, Cell interface only | Cellular modem settings ([Cell.md](../Cell.md#configuration)) |
| `/meadow0/unittest.config.yaml` | Before each kernel test | Test parameters ([DeveloperCommandsNsh.md](DeveloperCommandsNsh.md#the-test-configuration-file)) |

All file names are in `F7MeadowOS/include/meadow/hcom_shared_common.h` and `hcom_nx_config_manager.h`. The
parser is the vendored **libcyaml** (`F7MeadowOS/libcyaml/`, built on libyaml); the schema, which says
which keys exist, is in `hcom_nx_config_manager_yaml.h`.

`meadow.config.yaml` is loaded **once**. A second call to `hcom_nx_config_init()` logs "Configuration should
only be initialised once", so edits take effect only after a restart. The file is an ordinary file on `/meadow0`, so it can be put there with the upload request
([FileUploadDeleteList.md](FileUploadDeleteList.md)); the device must then be restarted.

## Reading the File

`hcom_nx_config_process_meadow_config_file()` runs in this order:

1. Allocate a zeroed `meadow_configuration_t` from the kernel heap.
2. `cyaml_load_file()` the file against the schema.
3. **If that fails, or the file is absent:** log, and fill in the *default configuration* below.
   - The file exists but cannot be parsed: log "Error processing config file, using default config" to
     `meadow.log` and raise a simple exception to managed code (`espcp_status_codes_invalid_configuration_file`).
   - The file does not exist: log "Config file not present, using default config" and carry on quietly.
4. **If it loads:** copy each section's values into the object, using the parsing rules below.
5. `hcom_nx_config_init()` then fills in the values that never come from the file: the host name (applied with
   `sethostname()`), hardware version, the Mono Runtime version (read from the Runtime's header in flash), the
   OS version and build date (compile-time constants), the 96-bit unique ID and the serial number derived from it.

### Parsing rules

Every YAML value is read as a **string** (the schema declares each as a string pointer) and converted by a helper,
so a mistake in one value does not fail the whole file:

| Type | Accepted | If it is not accepted |
|------|----------|-----------------------|
| Boolean | `true`/`false` and similar words | The setting's default |
| Unsigned integer | Decimal digits | The setting's default |
| IP address | Dotted quad | The address is ignored |
| Choice (log destination, interface name, UART use) | Known names | Default, or an exception (log destination) |

Unknown keys are **ignored** (`CYAML_CFG_IGNORE_UNKNOWN_KEYS`) and key names are matched without regard to
case (`CYAML_CFG_CASE_INSENSITIVE`), so a misspelt key is silently skipped. All sections and keys are optional,
except that a network interface entry must have a `Name`.

## Settings

Key names are matched without regard to case. "Default" is the value used when the key is absent from a section that is present.

### Device

| Key | Meaning | Default |
|-----|---------|---------|
| `Name` | Device name; also the network host name | `MeadowF7` |
| `InitializationTimeoutSeconds` | How long the runtime waits for start-up to finish | 60 |
| `RebootOnUnhandledException` | Restart the device on an unhandled managed exception | true |
| `SdStorageSupported` | Enable the SD card | false. Honoured only on the Core-Compute V2 |
| `ReservedPins` | Pins managed code must not use (a string passed through) | none |

The name is checked before use: it must start with a letter, contain only letters, digits, `-` and `_`, and
be no longer than the NuttX host-name limit. An invalid name is **replaced by `MeadowF7`** and the original is not
used for the host name.

### InternalDebug

| Key | Meaning | Default |
|-----|---------|---------|
| `TraceLevel` | Syslog verbosity: 0 to 3 take effect (a 4 is accepted but ignored with a warning) | 0. A value above 4 becomes 0 |
| `Uart1Use` | `trace` sends the kernel trace to USART1; `profiler` sends profiling data there | neither |
| `DebuggerAttachedToEsp` | If true, the ESP32 is **not** reset at start-up | false |
| `CopyApplicationOutputToUart` | Also copy application output to the syslog UART | false |

How the trace level becomes a syslog mask, and the effect of `Uart1Use`, are in
[TraceSyslog.md](TraceSyslog.md).

### MonoControl

| Key | Meaning |
|-----|---------|
| `Options` | A string of command-line options for the Mono runtime. `mono_main_proxy()` passes the string on, minus its first token ([MonoRemoteDebug.md](MonoRemoteDebug.md)); the run mode (interpreter, JIT, AOT) is chosen from it |

### Coprocessor and Network

These configure the ESP32 and the network stack and are outside the scope of this series. For orientation:
`Coprocessor` holds the SPI speed (never less than 100 kHz), automatic start and reconnect, the retry count, and
the ESP32's log destination, components and UDP port; `Network` holds the default interface, per-interface
addressing, NTP and DNS servers, and the country code. The kernel also writes the DNS servers to the resolver
file the NuttX resolver reads.

## The Default Configuration

Used when `meadow.config.yaml` is absent or malformed. `using_default_configuration` is set to 1.

| Setting | Value |
|---------|-------|
| Device name | `MeadowF7` (the object has none; `hcom_nx_config_init()` supplies it) |
| Reboot on unhandled exception | on |
| Initialisation timeout | 60 s |
| ESP32 reset at start-up | yes |
| ESP32 SPI speed | 8 MHz |
| Maximum network retry count | 3 |
| Trace level | 0 |
| Default network interface | the ESP32 (WiFi) |
| NTP | the built-in server list, refresh every 3600 s |
| DNS | the built-in server list |
| SD card, UART trace, profiler | off |

## Concurrency and Ownership

- **`config_lock`** is a semaphore with priority inheritance disabled (`SEM_PRIO_NONE`). Every kernel reader
  takes it with `hcom_nx_config_lock()`, gets the pointer with `hcom_nx_config_get_pointer()`, and releases
  it with `hcom_nx_config_unlock()`. It protects the strings and the interface table as well as the numbers.
  The pointer is stable for the life of the system, so the lock is what protects the contents, not the address.
- **User space gets a deep copy.** `meadow_os_deep_copy_config()` (`F7MeadowOS/meadow_os/meadow_os_config.c`)
  takes the lock, copies the structure, and replaces every string and sub-structure with a copy allocated on the
  **user** heap (`kumm_malloc`); kernel-only pointers (ESP log components, default access point, country code)
  are set to NULL so they can never leak across the boundary. The caller must pass the result to
  `meadow_os_config_free_resources()`. Callers include the device-information and name handlers
  ([DeviceControl.md](DeviceControl.md)), start-up, the Mono control code, the trace-level set-up, the file
  directory code and the Cell and ESP32 monitors.
- **Changes after start-up** are made in the kernel object only. The object is **never written back** to
  `meadow.config.yaml`.

## Access From Managed Code

The Mono runtime reads and writes individual items through the `/dev/upd` command
`MUPD_GET_SET_CONFIGURATION_VALUE` ([UpdInterface.md](UpdInterface.md)), which ends in
`hcom_nx_config_get_set_config_value(item, direction, buffer, length)`. The item numbers are the
`configuration_values` enumeration in `hcom_nx_config_manager.h`:

| Item | Name | Read | Write |
|------|------|------|-------|
| 0 | device name | yes | yes |
| 1 | product (hardware version number) | yes | |
| 2 | model | yes | |
| 3, 4 | OS version, build date | yes | |
| 5 | processor type | yes | |
| 6, 7 | unique ID, serial number | yes | |
| 8, 9 | coprocessor type, firmware version | yes | |
| 10 | Mono version | yes | |
| 11 | automatically start network | yes | |
| 12 | automatically reconnect | yes | yes |
| 13 | maximum network retry count | yes | yes |
| 14 | get time at start-up | yes | |
| 15, 16 | WiFi MAC, soft-AP MAC | yes | |
| 17 | default access point | yes | |
| 18 | reset reason | yes | |
| 19, 20 | reboot on unhandled exception, initialisation timeout | no handler | |
| 21 | selected network | yes | |
| 22, 23, 24 | static IP address, subnet mask, gateway | yes | |
| 25 | SD storage supported | yes | |
| 26 | reserved pins | yes | |

Direction 0 reads and anything else writes. A write of items 12 and 13 is passed on to the ESP32 first and
stored locally only if the ESP32 accepted it; a write of item 0 likewise. Any other item gives `ERROR`.
The length conventions are strict: strings must fit with their terminator, integers need 4 bytes, booleans 1.

## Things to Know

- **A partial file does not get the same defaults as no file.** The default configuration sets reboot-on-exception
  on, a 60 s timeout and three retries. A file that parses but omits the `Device` section leaves reboot-on-exception
  **off** and the timeout **0**; one that omits `Coprocessor` leaves the retry count at **0**. The values
  are consumed by the Mono runtime, outside this repository, so the effect is not verified here.
- **Items 19 and 20 cannot be read** through the item interface, although the enumeration has them.
- **The header comment for the item interface has the direction backwards.** `meadow-upd.h` says "1 = read, 0 =
  write"; the code treats 0 as read.
- **`LogUdpPort`'s range check is overwritten** by a second assignment, so any value is used (Issue #828).
- **The device name can be written without the host-name check.** The set path used by managed code
  stores the name as given (after the ESP32 accepts it); only the file path validates it and calls
  `sethostname()`. The write also tests `buffer[buffer_length]` for a terminator, so the caller's buffer must
  be longer than the length it states.
- **Misspelt keys and bad values are silently ignored or replaced.** An out-of-range trace level, an invalid name and an unreadable boolean
  all fall back to defaults without a message.
- **The Wi-Fi credentials file is a one-shot.** It is deleted whether or not it was valid, so a typo costs the
  user the file.
- **The unit-test configuration is separate** (`/meadow0/unittest.config.yaml`); the developer documentation
  [RunningDeveloperTests.md](../RunningDeveloperTests.md) spells the file name differently from the code.

## Source Map

| Concern | File |
|---------|------|
| Parsing, defaults, getters and setters | `F7MeadowOS/hcom_nx/hcom_nx_config_manager.c` |
| Public functions, item numbers | `F7MeadowOS/hcom_nx/hcom_nx_config_manager.h` |
| YAML schema | `F7MeadowOS/hcom_nx/hcom_nx_config_manager_yaml.h` |
| The configuration structure and file names | `F7MeadowOS/include/meadow/hcom_shared_common.h` |
| User-space copy | `F7MeadowOS/meadow_os/meadow_os_config.c` |
| Item ioctl | `F7MeadowOS/meadow_os/meadow-upd.c`, `F7MeadowOS/include/meadow/meadow-upd.h` |
| YAML library | `F7MeadowOS/libcyaml/`, `F7MeadowOS/libyaml/` (vendored; do not modify) |

## Related Documents

[KernelStartup.md](KernelStartup.md), [UpdInterface.md](UpdInterface.md), [DeviceControl.md](DeviceControl.md),
[TraceSyslog.md](TraceSyslog.md), [FlashPartitioning.md](FlashPartitioning.md).
