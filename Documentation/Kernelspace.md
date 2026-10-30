# Kernel Space

## Overview

The NuttX kernel runs in ARM privileged mode and is MPU-protected from user-space code. All Wilderness Labs kernel extensions are compiled into the kernel ELF (`nuttx.bin`) and reside in `F7MeadowOS` (symlinked into the board tree as `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/F7MeadowOSLink`), alongside the NuttX board support package at `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow`.

> **Naming note:** the source directory is `F7MeadowOS`. Build *output* filenames such as
> `Meadow.OS.bin` and `Meadow.OS.Update.bin` are unrelated artifact names - don't confuse the two.

## Board Support Package

`nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src`

| File | Purpose |
|------|---------|
| `stm32_boot.c` | `stm32_boardinitialize()` (very early: SPI chip selects, SDRAM, USB GPIO) and `board_late_initialize()` (backup SRAM, PWM, USB CDC/ACM, MPU regions, `meadow_upd_initialize()`, QSPI flash MTD/FTL, then `hcom_nx_setup_mgr()`) |
| `stm32_bringup.c` | `stm32_bringup()`: mounts procfs, sets up PWM and CS4344 audio, and calls `init_corecomp()` for the F7 Core Compute variant. Only reached when `CONFIG_BOARD_LATE_INITIALIZE` is not set (see below) |
| `stm32_appinitialize.c` | `board_app_initialize()`: returns OK immediately when `CONFIG_BOARD_LATE_INITIALIZE=y` (the `meadow_os` defconfig), otherwise calls `stm32_bringup()` |
| `stm32_extmem.c` | FMC/SDRAM initialisation |
| `stm32_spi.c` | SPI bus configuration |
| `stm32_usb.c` | USB OTG FS initialisation |
| `stm32_usbmsc.c` | USB mass storage class initialisation |
| `stm32_composite.c` | USB composite device (CDC/ACM plus mass storage) |
| `stm32_idle.c` | Idle thread hook (`up_idle()`, power management integration) |
| `stm32_reset.c` | Board reset implementation |
| `stm32_ioctl.c` | `board_ioctl()`: boardctl commands that enter and exit QSPI flash memory-mapped mode |
| `stm32_fat_dma.c` | `fat_dma_alloc()` / `fat_dma_free()`: 32-byte aligned FAT volume buffers so SD card DMA works (Issue #653) |
| `stm32_pwm.c` | `stm32_pwm_setup()`: registers the PWM devices |
| `board_corecomp.c` | `init_corecomp()`: fixed devices of the F7 Core Compute variant (SD card) |

## Kernel Startup Flow

```
Reset
  +-> stm32_boardinitialize()    - very early: SPI chip selects, SDRAM, USB GPIO
  +-> (NuttX nx_start)
        +-> board_late_initialize()   - Meadow does most of its board setup here
              +-> backup SRAM, PWM, USB CDC/ACM, MPU regions
              +-> meadow_upd_initialize()   - registers /dev/upd
              +-> QSPI flash MTD + FTL
              +-> hcom_nx_setup_mgr()       - Meadow kernel init
        +-> user-space init entry point: hcom_main (CONFIG_INIT_ENTRYPOINT)
```

`stm32_bringup()` is not on this path. Its only caller is `board_app_initialize()`
(`stm32_appinitialize.c`), and that call is compiled out when `CONFIG_BOARD_LATE_INITIALIZE=y`,
which the `meadow_os` defconfig sets. `hcom_nx_setup_mgr()` does not launch `hcom_main`; it
returns to `board_late_initialize()`, and NuttX then starts `hcom_main` as the user-space
init entry point.

`hcom_nx_setup_mgr()` is the main Wilderness Labs kernel entry point. It:

1. Sets up the QSPI flash partitions and file systems, the kernel-to-user traverse channel and trace
2. Starts the ESP32 coprocessor early (`espcp_early_init()`)
3. Reads the YAML configuration from QSPI flash via `hcom_nx_config_manager`
4. Sets up the interrupt routing (`meadow_interrupt_setup()`) and power management
5. Starts the SD card and the network interface selected by the configuration (Ethernet here; ESP32
   WiFi through `espcp`)
6. Starts the idle monitor, then finishes the ESP32 start-up (`espcp_late_init()`, run mode)

[HCOM/KernelStartup.md](HCOM/KernelStartup.md) lists every step and its failure handling.

The Universal Platform Driver is not registered here - `board_late_initialize()`
registers it earlier via `meadow_upd_initialize()` (see below).

## `hcom_nx` - Kernel-Side Host Communication

Source: `F7MeadowOS/hcom_nx`

### Config Manager (`hcom_nx_config_manager.c`)

Parses a YAML configuration file stored on the QSPI file system using the vendored `libcyaml` library (see below). Configuration includes:

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
- Relays CLI requests to `hcom_nx/hcom_nx_upd_route_cmd.c`
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

### CLI Request Router (`hcom_nx_upd_route_cmd.c`)

Despite the name, this file does not handle OS update requests or write OTA state flags. `hcom_nx_route_in_bound_cli_command()` is a switch that forwards CLI requests received from user space to their kernel handlers: `MONO_FLASH`, `BULK_FLASH_ERASE`, `VERIFY_ERASED_FLASH`, `PART_RENEW_FILE_SYS`, the trace and profiler routing requests (to the host or to the UART) and `RTC_READ_TIME`.

### Battery-Backed Register Access (`hcom_nx_bbreg_access.c`)

Provides safe read/write access to the STM32 RTC `BKxxR` registers for inter-boot persistent state. See [Memory.md](Memory.md) for the register map.

### Subdirectories

`hcom_nx` also contains two subdirectories not covered by the flat-file list above:

| Directory | Purpose |
|-----------|---------|
| `create_fs/` | Creates and formats the LittleFS QSPI filesystem partitions at first boot |
| `diag/` | Kernel-side trace/traverse diagnostics support |

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
| `meadow_sdmmc.c` | SD/MMC card initialisation; the NuttX automounter mounts `/sdcard` (see [SdCard.md](SdCard.md)) |
| `meadow_hw_version.c` | Runtime detection of board hardware revision |
| `meadow_interrupt.c` | External interrupt routing and management |

## `espcp` - ESP32 Coprocessor

Source: `F7MeadowOS/espcp`

The ESP32 coprocessor provides WiFi connectivity. The STM32 communicates with it over a dedicated UART and SPI interface using a custom binary protocol.
The ESP32 firmware is built from the separate Meadow-ESP32 repository; only the STM32 side lives here.

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

### Timeouts and Recovery (Issue #708)

espcp bounds the waits that previously could block a caller for ever when the ESP32 stopped
answering or a remote peer went away:

| Limit | Where | Behaviour |
|-------|-------|-----------|
| 30 s response timeout | `espcp_queue_message()` in `espcp_common.c` (`ESPCP_RESPONSE_TIMEOUT_MILLISECONDS`) | A blocking request that gets no response returns a timeout. The message is first removed from the dispatcher's waiting list (`espcp_cancel_waiting_message()`), so a late response cannot touch the caller's freed message or stack semaphore. |
| 3 consecutive timeouts | `espcp_common.c` (`ESPCP_RESPONSE_TIMEOUT_LIMIT`) | The ESP32 is marked not responding (`esp_not_responding`); later calls fail fast with `ENETDOWN`. Any answered request resets the count. |
| 60 s poll timeout | `espcp_usrsock_poll_setup()` (`ESPCP_POLL_TIMEOUT_MILLISECONDS`) | Sent with each poll request so that an ESP32 firmware that honours it ends an orphaned poll instead of holding a worker. |
| 3 teardown attempts | `espcp_usrsock_poll_teardown()` (`ESPCP_POLL_TEARDOWN_ATTEMPTS`) | A lost teardown leaves a poll armed on the ESP32, so it is retried. Teardown also runs when the ESP32 is not responding, so the request is always removed from the list before its `pollfd` is freed. |
| 10 s `SO_SNDTIMEO` | `espcp_usrsock_socket()` (`ESPCP_SEND_TIMEOUT_SECONDS`) | Set on every new ESP32 socket (best effort) so a send to a dead peer cannot hold one of the ESP32's small pool of workers for minutes and exhaust it (seen as `ENOMEM`, "thread pool is full"). |

The `EspcpTests` managed test app (see [RunningDeveloperTests.md](RunningDeveloperTests.md))
exercises these paths on hardware, except the 30 s response timeout, which needs an ESP32 that
stops answering altogether.

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
