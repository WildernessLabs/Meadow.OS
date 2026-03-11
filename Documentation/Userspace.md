# User Space

## Overview

The single user-space application is **hcom** (Host Communication). It is the primary application running on Meadow.OS and provides all communication between the Meadow board and a host computer. It runs entirely in unprivileged ARM mode and calls into the kernel only via the NuttX syscall interface.

- **Source**: `apps/system/hcom`
- **Entry point**: `hcom_main()` in `hcom_startup_manager.c`
- **NuttX config key**: `CONFIG_INIT_ENTRYPOINT="hcom_main"` (previously `CONFIG_USER_ENTRYPOINT`)
- **Kconfig**: `CONFIG_MEADOW_HCOM=y`

## Startup Sequence

```
NuttX kernel creates hcom task
        │
        ▼
hcom_main()          hcom_startup_manager.c
        │
        ├─► Initialise ESP32 comms threads (if CONFIG_HCOM_ESP32_COMMS)
        ├─► Start OTA subsystem (if CONFIG_MEADOW_CLOUD)
        ├─► Start cellular PPP daemon (hcom_pppd_start, if enabled)
        ├─► Start host USB receive thread
        ├─► Start host USB send thread
        └─► Enter main protocol dispatch loop
```

## Module Structure

```
apps/system/hcom/
├── hcom_startup_manager.c   Entry point and startup orchestration
├── hcom_common.h            Common includes and type definitions
├── comms/                   USB host communication protocol layer
├── file/                    File transfer (download/upload)
├── nx_rqsts/                NuttX syscall request wrappers
├── diag/                    Diagnostics, tracing, and logging
├── esp32/                   ESP32 coprocessor management from user side
├── mono/                    .NET Mono runtime management
├── ota/                     OTA update initiation
├── cell/                    Cellular/PPP support
├── misc/                    Shared utilities (circular buffer, utils)
└── tests/                   User-space test suite
```

## `comms` — Host Communication Protocol

Handles the binary protocol used to communicate with the host computer over USB. The host connects as a USB CDC device.

| File | Responsibility |
|------|---------------|
| `hcom_host_receive.c` | USB receive loop; reads raw bytes from USB |
| `hcom_host_preprocess.c` | Validates and frames incoming packets |
| `hcom_host_route.c` | Routes decoded commands to the appropriate handler |
| `hcom_host_encode.c` | Encodes response messages for transmission |
| `hcom_host_send.c` | USB transmit loop; writes responses to host |
| `hcom_host_enq_deq.c` | Thread-safe command queue management |
| `hcom_host_watchdog.c` | Watchdog for the host communication link |

The protocol is defined in `Meadow.OS/include/meadow/hcom_protocol.h`.

## `file` — File Transfer

Handles binary file downloads from the host to the board and uploads from the board to the host. Supports transfers to both STM32 internal filesystem and ESP32 flash.

| File | Responsibility |
|------|---------------|
| `hcom_file_dnld_stm32f7.c` | Download file to STM32 filesystem |
| `hcom_file_dnld_esp32.c` | Download file to ESP32 flash via ESPCP |
| `hcom_file_upld_proc.c` | Upload file from board to host |
| `hcom_file_write_stm32f7.c` | Low-level filesystem write operations |
| `hcom_file_delete_stm32f7.c` | File deletion |
| `hcom_file_lists.c` | Directory listing for host |
| `hcom_file_lists_subdir.c` | Recursive subdirectory listing |
| `hcom_file_dir_mgmt_utils.c` | Directory create/delete utilities |
| `hcom_file_misc.c` | Miscellaneous file utilities |

## `nx_rqsts` — NuttX Kernel Requests

Provides user-space wrappers for operations that require kernel privileges. These call through the Meadow UPD syscall interface into `hcom_nx_upd.c` in the kernel.

| File | Responsibility |
|------|---------------|
| `hcom_misc_requests.c` | Board info, version, reset, syslog level |
| `hcom_via_nx_access.c` | Generic NuttX resource access (GPIO, ADC, etc.) |
| `hcom_bbreg_access.c` | User-space access to battery-backed registers |

## `diag` — Diagnostics and Tracing

| File | Responsibility |
|------|---------------|
| `hcom_diag_logging.c` | Structured syslog capture and forwarding to host |
| `hcom_diag_trace_priority.c` | Trace message priority filtering |
| `hcom_diag_trace_to_cli.c` | Route trace output to NuttX CLI (NSH) |
| `hcom_diag_host_text_transport.c` | Transport layer for text diagnostics to host |
| `hcom_diag_decode_protocol.c` | Diagnostic decode for protocol inspection |
| `hcom_diag_gpio.c` | GPIO state diagnostic reporting |
| `hcom_diag_nsh_support.c` | NSH (NuttX Shell) integration for diagnostics |
| `hcom_diag_misc.c` | Miscellaneous diagnostic utilities |

## `esp32` — ESP32 Management (User Side)

Complements the kernel-side `espcp` module. Handles high-level ESP32 management operations accessible from user space:

| File | Responsibility |
|------|---------------|
| `hcom_esp32_comms.c` | User-side ESP32 communication threads |
| `hcom_esp32_network_monitor.c` | Monitors WiFi link state and reports to host |

## `mono` — .NET Mono Runtime

Manages the lifecycle of the .NET Mono runtime. Handles enable/disable requests from the host and communicates Mono state through battery-backed registers (`HCOM_BBREG_USER_RQST_MONO_ENABLE_BIT`, `HCOM_BBREG_MONO_LAST_RUN_LOCKUP_BIT`).

## `ota` — OTA Update Initiation

Handles OTA update requests received from the host computer. The user-space OTA module:

1. Receives an update image from the host via the file transfer protocol
2. Writes it to the secondary partition on QSPI flash
3. Sets the `update_nuttx_pending` flag in the battery-backed registers via `nx_rqsts`
4. Triggers a board reset so the bootloader can apply the update

## `cell` — Cellular (PPP)

Optional cellular connectivity via PPP daemon (`hcom_pppd`). Enabled when a supported cellular module is connected.

## `misc` — Shared Utilities

| File | Responsibility |
|------|---------------|
| `meadow_cirbuf.c` | Lock-free circular buffer used by comms layers |
| `meadow_utils.c` | General-purpose helper functions |
| `hcom_common_utils.c` | hcom-specific utility functions |
| `espcp_utils.c` | ESPCP protocol utilities used from user space |

## Shared Headers

User-space and kernel-space share protocol and data structure definitions via headers in `Meadow.OS/include/meadow`:

| Header | Contents |
|--------|---------|
| `hcom_protocol.h` | Host communication wire protocol definitions |
| `hcom_shared_common.h` | Types and constants shared between kernel and user |
| `hcom_nuttx_shared.h` | NuttX-specific shared definitions |
| `hcom_upd_shared.h` | UPD (Universal Platform Driver) shared interface |
| `hcom_dnld_shared.h` | File download shared types |
| `meadow_syscall.h` | Meadow syscall numbers and prototypes |
| `meadow_syscall_support.h` | Syscall support types |
| `meadow_apps_core_share.h` | Core app/kernel shared data structures |
| `meadow_partition_share.h` | QSPI partition layout shared between BL/OS/app |
