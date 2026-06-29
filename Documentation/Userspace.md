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

## Shared Headers

User-space and kernel-space share protocol and data structure definitions via headers in `Meadow.OS/include/meadow`:

| Header | Contents |
|--------|---------|
| `hcom_protocol.h` | Host communication wire protocol definitions |
| `hcom_shared_common.h` | Types and constants shared between kernel and user |
| `hcom_nuttx_shared.h` | NuttX-specific shared definitions |
| `hcom_upd_shared.h` | UPD (Universal Platform Driver) shared interface |
| `hcom_dnld_shared.h` | File download shared types |
| `meadow_syscall_support.h` | Syscall support types |
| `meadow_apps_core_share.h` | Core app/kernel shared data structures |
| `meadow_partition_share.h` | QSPI partition layout shared between BL/OS/app |
