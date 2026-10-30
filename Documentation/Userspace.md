# User Space

## Overview

The single user-space application is **hcom** (Host Communication). It is the primary application running on Meadow.OS and provides all communication between the Meadow board and a host computer. It runs entirely in unprivileged ARM mode and calls into the kernel through the NuttX syscall interface and through `ioctl()` calls on the `/dev/upd` driver.

- **Source**: `apps/system/hcom`
- **Entry point**: `hcom_main()` in `hcom_startup_manager.c`
- **NuttX config key**: `CONFIG_INIT_ENTRYPOINT="hcom_main"` (previously `CONFIG_USER_ENTRYPOINT`)
- **Kconfig**: `CONFIG_MEADOW_HCOM=y`

## Startup Sequence

```
NuttX starts the hcom task (CONFIG_INIT_ENTRYPOINT)
        |
        v
hcom_main()          hcom_startup_manager.c
        |
        +-> Open the kernel driver (/dev/upd): hcom_via_nx_upd_setup()
        |   (a board that is not supported starts host comms only, see below)
        +-> Diagnostics setup (trace priority level, common utils, misc)
        +-> Start the ESP32 UART network monitor thread, reset the ESP32
        +-> NSH support, misc request, file transfer and file write setup
        +-> Host enqueue/dequeue and route setup
        +-> Mono stdout/stderr read thread  (if CONFIG_HCOM_MONO_STDERR_STDOUT)
        +-> Mono remote-debug setup         (if CONFIG_HCOM_MONO_REMOTE_DEBUGGING)
        +-> ESP32 UART comms setup          (if CONFIG_HCOM_ESP32_COMMS)
        +-> Host send setup (no thread: allocates a buffer and a semaphore,
        |   registers a power-management callback)
        +-> Start host USB receive thread
        +-> Start kernel-to-user traverse thread
        +-> Mono main setup
        +-> Start Mono                      (if CONFIG_SYSTEM_MONO)
        +-> Start the cell network          (if CONFIG_MEADOW_CELL_INCLUDE_IN_BUILD, Mono
        |   started and the cell interface selected: see Cell.md)
        +-> Launch NSH                      (if CONFIG_SYSTEM_NSH)
        +-> hcom_host_process_setup(): hcom_main itself becomes the
            request-processing thread and never returns
```

There is no host send thread. The only thread created in `comms/` is the receive thread
(`hcom_host_receive.c`). Messages are sent synchronously by whichever thread calls the send
functions, serialised by a semaphore (`hcom_host_send.c`).

The diagram is a summary; `hcom_startup_manager.c` is the authority on the exact order. The OTA
start-up calls are an `#if(0)` block in that file, so they are not shown (see below).

Module dependencies: `comms/hcom_host_route.c` depends on both `ota/` and `esp32/`, and `mono/`
depends on `diag/hcom_diag_gpio.h`.

### Currently Disabled Subsystems

The OTA startup calls (`os_update()`, `app_update()`, `firmware_update()`) are wrapped in an
`#if(0)` block in `hcom_startup_manager.c`, with a comment noting they are not usable without
further work. In the current build:

- **`ota/` is not started at boot.** `ota/hcom_ota.c` is compiled (when `CONFIG_MEADOW_CLOUD=y`),
  but the calls that would launch it are compiled out.
- **`cell/` is started at boot** when `CONFIG_MEADOW_CELL_INCLUDE_IN_BUILD=y`, Mono has started and
  `meadow.config.yaml` selects the cell interface. See [Cell.md](Cell.md).
- **`mono/` starts the Mono .NET runtime when `CONFIG_SYSTEM_MONO` is enabled.**
  `hcom_mono_ctrl_start_mono_main()` creates the "Mono" task, running `meadow_mono_main()` from
  `apps/system/mono/`, but only when the runtime is enabled, its version matches the OS, and the
  required assemblies (`System.Private.CoreLib.dll`, `Meadow.dll`) are present in `/meadow0`.
- OTA update requests are still reachable *reactively*: `comms/hcom_host_route.c` can call into
  `ota/` in response to a host command, independent of the boot-time startup sequence.

Treat any reasoning about "what runs on this board" as excluding OTA unless
`hcom_startup_manager.c` has been changed to re-enable it.

## Module Structure

```
apps/system/hcom/
+-- hcom_startup_manager.c   Entry point and startup orchestration
+-- hcom_common.h            Common includes and type definitions
+-- comms/                   USB host communication protocol - full protocol state machine
+-- file/                    File transfer - two parallel subsystems (STM32-side and ESP32-side)
+-- nx_rqsts/                Wrappers around ioctl() calls on the /dev/upd kernel driver
+-- diag/                    Diagnostics, tracing, logging, and a kernel/user IPC channel
+-- esp32/                   ESP32 coprocessor protocol stack (execute, network monitor, UART)
+-- mono/                    .NET Mono runtime management (starts apps/system/mono, see below)
+-- ota/                     OTA update initiation (currently not started, see below)
+-- cell/                    Cell network: CMUX, PPP and the AT command channel (see Cell.md)
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
- **`cell/`** (`hcom_cell.c`) runs the cell link over CMUX: the stock NuttX pppd on one channel
  and AT commands on another, in two threads (CellPPPD and CellAT). The CMUX multiplexer itself
  is NuttX's `apps/netutils/cmux`.

Two overlaps worth knowing about when adding new code: `diag/hcom_diag_misc.c` and
`misc/hcom_common_utils.c` are both generic grab-bag utility files with no stated distinction
between them, and `misc/espcp_utils.c` is ESP-coprocessor-specific logic that lives outside
`esp32/`.

`tests/` also contains `tensorflow_tests.c`, which is present in source but exercises a feature
that is not part of the current build (TensorFlow Lite for Microcontrollers). `cell_tests.c` holds
the native cell tests (`build.sh --unittests=cell`); see [Cell.md](Cell.md).

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
