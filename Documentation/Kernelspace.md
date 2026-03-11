# Kernel Space

## Overview

The NuttX kernel runs in ARM privileged mode and is MPU-protected from user-space code. All Wilderness Labs kernel extensions are compiled into the kernel ELF (`nuttx.bin`) and reside in `Meadow.OS/` alongside the NuttX board support package at `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/`.

## Board Support Package

`nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/`

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
| `meadow_rotary_encoder.c` | Rotary encoder driver |

## Kernel Startup Flow

```
Reset
  └─► stm32_boot()            — clocks, early SRAM init
        └─► board_late_initialize()
              └─► stm32_bringup()   — procfs, PWM
                    └─► stm32_appinitialize()
                          └─► hcom_nx_setup_mgr()   — Meadow kernel init
                                └─► task_create("hcom_main")  — launch user app
```

`hcom_nx_setup_mgr()` is the main Wilderness Labs kernel entry point. It:

1. Initialises GPIOs and board interrupt routing
2. Reads the YAML configuration from QSPI flash via `hcom_nx_config_manager`
3. Selects and starts the network interface (ESP32 WiFi or Ethernet)
4. Registers the Universal Platform Driver (`hcom_nx_upd`) syscall endpoint
5. Starts the ESP32 coprocessor threads (`espcp`)

## `hcom_nx/` — Kernel-Side Host Communication

Source: `Meadow.OS/hcom_nx/`

### Config Manager (`hcom_nx_config_manager.c`)

Parses a YAML configuration file stored on the QSPI file system using the embedded `libyaml` library. Configuration includes:

- Network interface selection (ESP32 WiFi or Ethernet)
- WiFi credentials (SSID/password)
- Static IP / DHCP settings
- Cloud endpoint URLs
- Syslog verbosity

The default network interface falls back to `MEADOW_IFT_ESP32` if no config is found.

### Startup Manager (`hcom_nx_startup_mgr.c`)

Called once during NuttX bring-up. Orchestrates the full kernel-side initialisation sequence: GPIO setup, config load, ESP32 coprocessor start, Ethernet bring-up, and registration of the platform driver.

### Universal Platform Driver (`hcom_nx_upd.c`)

Implements the kernel-side endpoint for the Meadow UPD (Universal Platform Driver). User-space code calls kernel functions through this interface via NuttX syscalls. Key responsibilities:

- Exposes board control operations to user space (reset, sleep, version query)
- Relays OTA update requests to `hcom_nx_upd_route_cmd.c`
- Ensures user-space cannot directly manipulate privileged hardware

### OTA Update Router (`hcom_nx_upd_route_cmd.c`)

Handles the kernel-side of OS update requests received from user space. Writes OTA state flags to the RTC battery-backed registers, which are read by the bootloader on the next boot.

### Battery-Backed Register Access (`hcom_nx_bbreg_access.c`)

Provides safe read/write access to the STM32 RTC `BKxxR` registers for inter-boot persistent state. See [Memory.md](Memory.md) for the register map.

## `meadow_os/` — Core OS Services

Source: `Meadow.OS/meadow_os/`

| File | Key Functionality |
|------|-----------------|
| `meadow_os.c` | `meadow_os_reset_board()` — issues a controlled board reset with status code |
| `meadow_os_fault_handling.c` | Hard fault and UsageFault handlers; stores fault info to battery-backed registers before reset |
| `meadow_os_battery_backed_domain.c` | Enables RTC backup domain; reads/writes reset reason and control flags |
| `meadow_os_config.c` | Persistent OS configuration (stored to QSPI) |
| `meadow_os_info.c` | Board hardware version string, OS version string |
| `meadow_os_espcp.c` | Glue between meadow_os and the ESP32 coprocessor |
| `meadow_os_exceptions.c` | C++ exception handling support |
| `meadow_sdmmc.c` | SD/MMC card initialisation and mount |
| `meadow_hw_version.c` | Runtime detection of board hardware revision |
| `meadow-gpio.c` | GPIO configuration, Meadow-specific pin multiplexing |
| `meadow_interrupt.c` | External interrupt routing and management |

## `espcp/` — ESP32 Coprocessor

Source: `Meadow.OS/espcp/`

The ESP32 coprocessor provides WiFi connectivity. The STM32 communicates with it over a dedicated UART using a custom binary protocol called ESPCP (ESP32 Coprocessor Protocol).

### Architecture

```
NuttX usrsock socket API
        │
        ▼
  espcp_usrsock_sockif.c   — NuttX usrsock interface implementation
        │
        ▼
  espcp_message_dispatcher.c  — routes messages to handlers
        │
        ▼
  espcp_queue.c            — thread-safe message queues
        │
        ▼
  espcp_thread.c           — UART reader/writer threads
        │
        ▼
  [ESP32 UART]
```

Key modules:

| File | Purpose |
|------|---------|
| `espcp_coprocessor.c` | Initialisation, start/stop of the coprocessor subsystem |
| `espcp_message.c` | ESPCP message encode/decode |
| `espcp_encoders.c` | Protocol field encoders |
| `espcp_wifi.c` | WiFi connect/disconnect/scan handling |
| `espcp_system.c` | ESP32 system-level commands (reset, version) |
| `espcp_file_system.c` | File operations forwarded to ESP32 flash |
| `espcp_core_dump.c` | Captures ESP32 core dumps on crash |
| `espcp_uart_monitor.c` | Monitors UART health and reconnects on error |
| `espcp_event_handlers.c` | Handles asynchronous events from ESP32 |

## `ethernet/` — Wired Ethernet

Source: `Meadow.OS/ethernet/`

Ethernet is supported on hardware versions that include the LAN8742A PHY (`CONFIG_MEADOW_ETHNET_INCLUDE_IN_BUILD=y`, `CONFIG_ETH0_PHY_LAN8742A=y`). The hardware version is detected at runtime by `meadow_hw_version.c`.

| File | Purpose |
|------|---------|
| `meadow_ethnet_start.c` | Initialises the Ethernet interface |
| `meadow_ethnet_connect.c` | Manages link connect/disconnect |
| `meadow_ethnet_dhcp.c` | DHCP client lease management |
| `meadow_ethnet_monitor.c` | Polls link state and reconnects on drop |
| `meadow_ethnet_ping.c` | Connectivity verification via ICMP echo |

## `pwrmgmt/` — Power Management

Source: `Meadow.OS/pwrmgmt/`

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

## `kerneltests/` — In-Kernel Tests

Source: `Meadow.OS/kerneltests/`

Optional in-kernel test suite compiled into the kernel build when specific kconfig options are enabled (e.g., `CONFIG_MEADOW_OS_TESTS`, `CONFIG_GPIO_TESTS`). Tests are triggered by unit test flags set in `build.sh` via `kconfig-tweak`.

## `misc/` — Shared Kernel Utilities

Source: `Meadow.OS/misc/`

Shared utilities used across kernel modules including structured logging (`meadow_logging`), timer helpers, and specialised hardware access routines.

## Linker Scripts

All linker scripts are in `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/scripts/`:

| Script | Purpose |
|--------|---------|
| `memory.ld` | Master memory region definitions (included by others) |
| `kernel-space.ld` | Section layout for the kernel ELF |
| `user-space.ld` | Section layout for the user ELF |
| `flash.ld` | Flat (non-protected) build layout |
