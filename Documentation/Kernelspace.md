# Kernel Space

## Overview

The NuttX kernel runs in ARM privileged mode and is MPU-protected from user-space code. All Wilderness Labs kernel extensions are compiled into the kernel ELF (`nuttx.bin`) and reside in `F7MeadowOS` (symlinked into the board tree as `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/F7MeadowOSLink`), alongside the NuttX board support package at `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow`.

> **Naming note:** the source directory is `F7MeadowOS`. Build *output* filenames such as
> `Meadow.OS.bin` and `Meadow.OS.Update.bin` are unrelated artifact names - don't confuse the two.

## Board Support Package

`nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src`

| File | Purpose |
|------|---------|
| `stm32_boot.c` | Early board initialisation, clock setup |
| `stm32_bringup.c` | Late initialisation: mounts procfs, initialises PWM |
| `stm32_appinitialize.c` | Launches user-space application after kernel is ready |
| `stm32_extmem.c` | FMC/SDRAM initialisation |
| `stm32_spi.c` | SPI bus configuration |
| `stm32_i2c.c` | I2C bus configuration |
| `stm32_usb.c` | USB OTG FS initialisation |
| `stm32_idle.c` | Idle thread hook (power management integration) |
| `stm32_autoleds.c` | Tri-colour LED (PA1=Green, PA0=Blue, PA2=Red) |
| `stm32_reset.c` | Board reset implementation |

## Kernel Startup Flow

```
Reset
  +-> stm32_boot()            - clocks, early SRAM init
        +-> board_late_initialize()
              +-> stm32_bringup()   - procfs, PWM
                    +-> stm32_appinitialize()
                          +-> hcom_nx_setup_mgr()   - Meadow kernel init
                                +-> task_create("hcom_main")  - launch user app
```

`hcom_nx_setup_mgr()` is the main Wilderness Labs kernel entry point. It:

1. Initialises GPIOs and board interrupt routing
2. Reads the YAML configuration from QSPI flash via `hcom_nx_config_manager`
3. Selects and starts the network interface (ESP32 WiFi or Ethernet)
4. Starts the ESP32 coprocessor threads (`espcp`)

The Universal Platform Driver is not registered here - `board_late_initialize()`
registers it earlier via `meadow_upd_initialize()` (see below).

## `hcom_nx` - Kernel-Side Host Communication

Source: `F7MeadowOS/hcom_nx`

### Config Manager (`hcom_nx_config_manager.c`)

Parses a YAML configuration file stored on the QSPI file system using the embedded `libyaml` library. Configuration includes:

- Network interface selection (ESP32 WiFi or Ethernet)
- WiFi credentials (SSID/password)
- Static IP / DHCP settings
- Cloud endpoint URLs
- Syslog verbosity
- System debug features

The default network interface falls back to `MEADOW_IFT_ESP32` if no config is found.

### Startup Manager (`hcom_nx_startup_mgr.c`)

Called once during NuttX bring-up. Orchestrates the full kernel-side initialisation sequence: GPIO setup, config load, ESP32 coprocessor start, and Ethernet bring-up.

### Universal Platform Driver (`meadow_os/meadow-upd.c`)

The single kernel-side endpoint for user space, registered as `/dev/upd` by
`meadow_upd_initialize()` from `board_late_initialize()`. Everything user space can ask the
kernel to do arrives as an `ioctl` on this node. Key responsibilities:

- Exposes board control operations to user space (reset, sleep, version query)
- Exposes peripherals the protected build puts out of user space's reach (I2C, SPI, PWM,
  GPIO interrupts, ESP32 coprocessor messaging)
- Relays OTA and CLI requests to `hcom_nx/hcom_nx_upd_route_cmd.c`
- Ensures user-space cannot directly manipulate privileged hardware

It serves two command sets, distinguished by number:

| Prefix | Range | Used by | Defined in |
|--------|-------|---------|------------|
| `MUPD_*` | 1 - 82 (block `0x0000`) | Meadow.Core / the .NET runtime | `include/meadow/meadow-upd.h` |
| `HCOM_NX_UPD_*` | `0x7000` + offset | `apps/system/hcom` | `include/meadow/hcom_upd_shared.h` |

The `MUPD_*` values are a frozen external ABI and must not be renumbered. The
`HCOM_NX_UPD_*` values are internal and sit in a block of their own chosen to avoid both
the `MUPD_*` range and the ioctl blocks NuttX reserves in `nuttx/include/nuttx/fs/ioctl.h`;
the header explains the constraint. The HCOM command block is compiled only when
`CONFIG_MEADOW_HCOM` is set, but the driver itself is always registered.

Until recently the HCOM commands lived in a second driver, `hcom_nx/hcom_nx_upd.c`, on a
separate node (`/dev/nxupd`). That driver has been removed and its commands merged here.

### OTA Update Router (`hcom_nx_upd_route_cmd.c`)

Handles the kernel-side of OS update requests received from user space. Writes OTA state flags to the RTC battery-backed registers, which are read by the bootloader on the next boot.

### Battery-Backed Register Access (`hcom_nx_bbreg_access.c`)

Provides safe read/write access to the STM32 RTC `BKxxR` registers for inter-boot persistent state. See [Memory.md](Memory.md) for the register map.

### Subdirectories

`hcom_nx` also contains three subdirectories not covered by the flat-file list above:

| Directory | Purpose |
|-----------|---------|
| `create_fs/` | Creates and formats the LittleFS QSPI filesystem partitions at first boot |
| `diag/` | Kernel-side trace/traverse diagnostics support |
| `tests/` | QSPI flash test cases |

## `meadow_os` - Core OS Services

Source: `F7MeadowOS/meadow_os`

| File | Key Functionality |
|------|-----------------|
| `meadow_os.c` | `meadow_os_reset_board()` - issues a controlled board reset with status code |
| `meadow_os_fault_handling.c` | Hard fault and UsageFault handlers; stores fault info to battery-backed registers before reset |
| `meadow_os_battery_backed_domain.c` | Enables RTC backup domain; reads/writes reset reason and control flags |
| `meadow_os_config.c` | Persistent OS configuration (stored to QSPI) |
| `meadow_os_info.c` | Board hardware version string, OS version string |
| `meadow_os_espcp.c` | Glue between meadow_os and the ESP32 coprocessor |
| `meadow_os_exceptions.c` | Pass erroro conditions through to .NET applications |
| `meadow_sdmmc.c` | SD/MMC card initialisation and mount |
| `meadow_hw_version.c` | Runtime detection of board hardware revision |
| `meadow-gpio.c` | GPIO configuration, Meadow-specific pin multiplexing |
| `meadow_interrupt.c` | External interrupt routing and management |

## `espcp` - ESP32 Coprocessor

Source: `F7MeadowOS/espcp`

The ESP32 coprocessor provides WiFi connectivity. The STM32 communicates with it over a dedicated UART and SPI interface using a custom binary protocol.

### Architecture

```
NuttX usrsock socket API
        |
        v
  espcp_usrsock_sockif.c   - NuttX usrsock interface implementation
        |
        v
  espcp_message_dispatcher.c  - routes messages to handlers
        |
        v
  espcp_queue.c            - thread-safe message queues
        |
        v
  espcp_thread.c           - UART reader/writer threads
        |
        v
  [ESP32 SPI]
```

Key modules:

| File | Purpose |
|------|---------|
| `espcp_coprocessor.c` | Initialisation, start/stop of the coprocessor subsystem |
| `espcp_message.c` | ESPCP message structures |
| `espcp_message_dispatcher.c` | Routes decoded messages to their handlers |
| `espcp_queue.c` | Thread-safe message queues between reader/writer threads |
| `espcp_thread.c` | UART reader/writer threads |
| `espcp_usrsock_sockif.c` | Implements the NuttX `usrsock` socket interface over ESPCP |
| `espcp_wifi.c` | WiFi-specific commands (connect, scan, credentials) |
| `espcp_encoders.c` | Protocol field encoders |
| `espcp_system.c` | ESP32 system-level commands (reset, version) |
| `espcp_file_system.c` | File operations forwarded to ESP32 flash |
| `espcp_core_dump.c` | Captures ESP32 core dumps on crash |
| `espcp_uart_monitor.c` | Monitors UART for control messages from the EP32 to the STM32 |
| `espcp_event_handlers.c` | Handles asynchronous events from ESP32 |
| `espcp_common.c`, `generic_list.c` | Shared helpers and a generic linked-list utility |

This table covers the most-referenced modules; `espcp` has 20+ source files in total. For the
full wire protocol, GPIO handshake lines, firmware upload procedure, and the cross-layer `poll()`
implementation (Mono/NuttX/usrsock/ESP32), see `F7MeadowOS/espcp/readme.md` and the docs under
`F7MeadowOS/espcp/doc/` (`coprocessor.md`, `ImplementingPoll.md`) - these are more detailed than
this summary and are not duplicated here.

## `ethernet` - Wired Ethernet

Source: `F7MeadowOS/ethernet`

Ethernet is supported on hardware versions that include the LAN8742A PHY (`CONFIG_MEADOW_ETHNET_INCLUDE_IN_BUILD=y`, `CONFIG_ETH0_PHY_LAN8742A=y`). The hardware version is detected at runtime by `meadow_hw_version.c`.

| File | Purpose |
|------|---------|
| `meadow_ethnet_start.c` | Initialises the Ethernet interface |
| `meadow_ethnet_connect.c` | Manages link connect/disconnect |
| `meadow_ethnet_dhcp.c` | DHCP client lease management |
| `meadow_ethnet_monitor.c` | Polls link state and reconnects on drop |
| `meadow_ethnet_utils.c` | Ethernet helper/utility routines |

## `pwrmgmt` - Power Management

Source: `F7MeadowOS/pwrmgmt`

A custom implementation of STM32 Stop mode (low-power sleep). Does **not** use the NuttX PM framework. The idle thread hook (`stm32_idle.c`) calls into this subsystem when the scheduler determines the system can sleep.

| File | Purpose |
|------|---------|
| `pwrmgmt_control.c` | Main entry point; decides whether/when to enter stop mode |
| `pwrmgmt_enter_stop_mode.c` | Executes the Stop mode entry sequence |
| `pwrmgmt_config_wakeup_alarm.c` | Configures RTC alarm as wake-up source |
| `pwrmgmt_config_wakeup_timer.c` | Configures RTC wake-up timer |
| `pwrmgmt_switch_rtc_clock.c` | Switches between HSE and LSI as RTC clock source |
| `pwrmgmt_calib_lsi_clock.c` | Calibrates the LSI RC oscillator frequency |
| `pwrmgmt_time_support.c` | Time conversion utilities for wake-up calculations |
| `pwrmgmt_misc.c` | Miscellaneous helpers |

## `specialized` - Specialized Peripheral Drivers

Source: `F7MeadowOS/specialized`

| File | Purpose |
|------|---------|
| `meadow_rotary_encoder.c` | Rotary encoder driver |
| `meadow_measure_freq.c` | General-purpose frequency measurement |

## `ntpclient` - NTP Client

Source: `F7MeadowOS/ntpclient`

A Wilderness Labs fork of the stock NuttX `apps/netutils/ntpclient` app (`meadow_ntpclient.c`),
with its own `Kconfig`. Provides network time synchronisation.

## `meadow_cloud` - Meadow Cloud Provisioning

Source: `F7MeadowOS/meadow_cloud`

Single-file module (`meadow_cloud.c`) handling provisioning and storage of Meadow Cloud
public/private key files via the ESP32 coprocessor's file system.

## `libyaml` / `libcyaml` - Vendored YAML Parsing

Source: `F7MeadowOS/libyaml`, `F7MeadowOS/libcyaml`

Vendored third-party libraries - **do not modify**. `libyaml` is the upstream YAML parser;
`libcyaml` is a schema-driven wrapper around it (ISC license, from the netsurf-browser.org
project). `hcom_nx_config_manager.c` calls `libcyaml` (`cyaml_load_file`, `cyaml_free`) directly,
not `libyaml`.

## `kerneltests` - In-Kernel Tests

Source: `F7MeadowOS/kerneltests`

Optional in-kernel test suite compiled into the kernel build when specific kconfig options are enabled (e.g., `CONFIG_MEADOW_OS_TESTS`, `CONFIG_GPIO_TESTS`). Tests are triggered by unit test flags set in `build.sh` via `kconfig-tweak`.

## `misc` - Shared Kernel Utilities

Source: `F7MeadowOS/misc`

Shared utilities used across kernel modules including structured logging (`meadow_logging`) and
specialised hardware access routines.

## `include` - Public Meadow API Headers

Source: `F7MeadowOS/include/meadow`

~35 public headers defining the API surface shared between kernel modules and, for the protocol
and shared-data-structure headers, with the `hcom` user-space application. See
[Userspace.md](Userspace.md) for the subset shared with user space.

## Linker Scripts

All linker scripts are in `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/scripts`:

| Script | Purpose |
|--------|---------|
| `memory.ld` | Master memory region definitions (included by others) |
| `kernel-space.ld` | Section layout for the kernel ELF |
| `user-space.ld` | Section layout for the user ELF |
| `flash.ld` | Flat (non-protected) build layout |
