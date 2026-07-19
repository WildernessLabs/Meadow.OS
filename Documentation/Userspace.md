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
        |
        v
hcom_main()          hcom_startup_manager.c
        |
        +-> Initialise ESP32 comms threads (if CONFIG_HCOM_ESP32_COMMS)
        +-> Start OTA subsystem (if CONFIG_MEADOW_CLOUD)
        +-> Start cellular PPP daemon (hcom_pppd_start, if enabled)
        +-> Start host USB receive thread
        +-> Start host USB send thread
        +-> Enter main protocol dispatch loop
```

Real dependency/start order as implemented in `hcom_startup_manager.c`: diag (trace priority)
-> esp32 network monitor -> file (STM32/ESP32 download/upload setup) -> comms (enqueue/dequeue,
route) -> mono (gated by config) -> esp32 UART comms -> comms (send/receive threads).
`comms/hcom_host_route.c` depends on both `ota/` and `esp32/`; `mono/` depends on
`diag/hcom_diag_gpio.h`.

### Currently Disabled Subsystems

The OTA, cellular/PPP, and Mono startup calls shown above are wrapped in `#if(0)` blocks in
`hcom_startup_manager.c`, with comments noting they are not usable without further work (e.g.
"undefined reference to `hcom_pppd_start`"). In the current build:

- **`mono/`, `cell/`, `ota/` are not started at boot.** Their source is present and compiled, but
  the calls that would launch them are compiled out.
- `cell/hcom_pppd.c` and `ota/hcom_ota.c` have no corresponding object files in a normal build -
  they are effectively dead code paths until re-enabled.
- OTA update requests are still reachable *reactively*: `comms/hcom_host_route.c` can call into
  `ota/` in response to a host command, independent of the boot-time startup sequence.

Treat any reasoning about "what runs on this board" as excluding these three subsystems unless
`hcom_startup_manager.c` has been changed to re-enable them.

## Module Structure

```
apps/system/hcom/
+-- hcom_startup_manager.c   Entry point and startup orchestration
+-- hcom_common.h            Common includes and type definitions
+-- comms/                   USB host communication protocol - full protocol state machine
+-- file/                    File transfer - two parallel subsystems (STM32-side and ESP32-side)
+-- nx_rqsts/                NuttX syscall request wrappers
+-- diag/                    Diagnostics, tracing, logging, and a kernel/user IPC channel
+-- esp32/                   ESP32 coprocessor protocol stack (execute, network monitor, UART)
+-- mono/                    .NET Mono runtime management (currently not started, see below)
+-- ota/                     OTA update initiation (currently not started, see below)
+-- cell/                    Cellular/PPP support incl. cmux/ (currently not started, see below)
+-- misc/                    Shared utilities (circular buffer, utils)
+-- tests/                   User-space test suite
```

Several of these one-line descriptions understate real scope - the subdirectories are large
enough to be their own subsystems:

- **`comms/`** (~3,200 lines across 7 files) implements a complete protocol state machine:
  encode, enqueue/dequeue, preprocess, receive, route, send, and a watchdog timer thread.
- **`file/`** (~3,700 lines across 9 files) is really two parallel file-transfer subsystems split
  by target (STM32 vs. ESP32) and direction (download/upload/delete/list/write), not one.
- **`esp32/`** (~2,500 lines across 8 files) is a full coprocessor protocol stack: execute,
  network monitor, recv/xmit processing, UART comms - not simple "management."
- **`diag/`** (7 files) covers protocol decoding, GPIO diagnostics, NSH support, and trace-level
  control, plus `hcom_traverse_ku_user.c`, which is really a kernel/user IPC channel, not a
  diagnostics feature.
- **`cell/`** includes a `cmux/` sub-directory implementing a full 3GPP 27.010 CMUX multiplexer.

Two overlaps worth knowing about when adding new code: `diag/hcom_diag_misc.c` and
`misc/hcom_common_utils.c` are both generic grab-bag utility files with no stated distinction
between them, and `misc/espcp_utils.c` is ESP-coprocessor-specific logic that lives outside
`esp32/`.

`tests/` also contains `tensorflow_tests.c` and `cell_tests.c`, which exercise features that are
not part of the current build (TensorFlow Lite for Microcontrollers, and the disabled `cell/`
subsystem) - present in source but not exercised by a default build.

## Shared Headers

User-space and kernel-space share protocol and data structure definitions via headers in `F7MeadowOS/include/meadow`:

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
| `hcom_bbreg_defn.h` | Battery-backed register layout, shared with `hcom_nx` |
| `hcom_dfu_mode_entry.h` | DFU mode entry shared types |
| `hcom_misc_diag.h` | Diagnostics message shared types |
| `hcom_traverse_ku.h` | Kernel/user IPC channel used by `diag/hcom_traverse_ku_user.c` |

`include/meadow` also has a `bootloader/` subdirectory of headers shared with the bootloader
build - not to be confused with the separate top-level `bootloader/` project described in
[Architecture.md](Architecture.md). Beyond the hcom-specific headers above, `include/meadow`
contains many general OS headers (e.g. `meadow_os.h`, `meadow_cirbuf.h`, `meadow_thread_config.h`,
`meadow_debug_helpers.h`) used across both kernel and user space - see
[Kernelspace.md](Kernelspace.md#include--public-meadow-api-headers) for the full picture.
