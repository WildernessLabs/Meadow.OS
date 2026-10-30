# HCOM and Meadow.OS Platform Services - Overview

> **Status:** this page is the map for the documents in this directory. Every document in the
> [Document Map](#document-map) has been written.

## Purpose and Audience

These documents explain the host-communication ("HCOM") parts of Meadow.OS and the kernel-side
services that make them work. They are written for a senior developer who knows NuttX and the
STM32F7, but is new to this code: they describe what each part does, how the parts fit together,
and the details that are not obvious from reading the source (threads, buffers, state, limits and
ordering rules).

### A Note on Terminology

"HCOM" originally meant the part of Meadow that served Meadow.CLI, the host-side command-line
tool. Over time it has become a general name for most of Meadow.OS's own code, so these documents
use the terms as follows:

| Term | Meaning here |
|------|--------------|
| **HCOM** | The host communication protocol and the `hcom` user-space application (`apps/system/hcom`) that implements it, together with the user-space features that hang off it (file system access, Mono control, diagnostics) |
| **Meadow.OS** | The whole operating system: NuttX plus the Wilderness Labs additions in `F7MeadowOS/`, the Meadow board directory and `apps/system/hcom` |
| **host** / **CLI** | The PC or Mac side: Meadow.CLI, and also the HcomDiagUi test tool |
| **nx** / **NuttX side** | Kernel-side code. `hcom_via_nx_*` functions are the user-space calls into it; `hcom_nx_*` are the kernel-side implementations |
| **UPD** | The Universal Platform Driver, the `/dev/upd` character driver that is the kernel's single entry point for user space |
| **BBR** | The STM32 RTC battery-backed registers, used to keep small amounts of state across resets |
| **Mono** | The .NET runtime that runs the application code; it runs as a task inside the HCOM task group |

## Scope

**Covered:**

- Everything under `apps/system/hcom/` except the topics below
- The Meadow board directory `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/` where it matters to
  HCOM or to bringing the platform up
- The kernel-side code in `F7MeadowOS/` that serves HCOM: `hcom_nx/`, the HCOM parts of
  `meadow_os/`, and the shared headers in `F7MeadowOS/include/meadow/`
- `meadow_interrupt.c`: it is not part of HCOM, but it is part of what makes Meadow.OS work

**Not covered:**

- The ESP32 coprocessor (`esp32/`, `espcp/`, ESP UART comms), OTA updates, and the cellular stack
  (`cell/`, BG77, PPP, CMUX). Where an HCOM request is routed to one of these, the documents say so
  in a sentence and move on.
- Meadow Cloud, Ethernet and NTP.
- NuttX itself, except where Meadow changes or extends it.
- The test suites (`apps/system/hcom/tests/`, `F7MeadowOS/kerneltests/`), except for how the
  developer-command dispatcher reaches them.

Other documents already cover related ground and are linked rather than repeated:
[Architecture.md](../Architecture.md) (protected build model, boot sequence),
[Memory.md](../Memory.md) (memory map, partitions, MPU, battery-backed register map),
[Kernelspace.md](../Kernelspace.md) and [Userspace.md](../Userspace.md) (module inventories),
[DualCdcAcm.md](../DualCdcAcm.md) (the developer-only second USB serial port) and
[SdCard.md](../SdCard.md).

## The System at a Glance

```
   Host PC / Mac
   +----------------------------------------------+
   |  Meadow.CLI, HcomDiagUi   (Visual Studio for |
   |  Mono debugging, relayed through the CLI)    |
   +----------------------+-----------------------+
                          |  USB CDC/ACM serial
                          |  COBS-framed HCOM packets
                          v
 =========  user space: the HCOM task (hcom_main)  ======================

   "HcomRecv" thread                    "HcomProc" thread (= hcom_main)
   read /dev/ttyACM0                    decode, validate, route requests
          |                                     ^
          v                                     |
   +--------------------+   packets            |
   | circular buffer    |----------------------+
   +--------------------+                      |
                                               v
          +---------------+--------------+-----+---------+------------+
          v               v              v               v            v
     file transfer   Mono control    device info,    developer     trace level
     /meadow0        stdout/stderr   restart, DFU,   commands,     and routing
     /sdcard         remote debug    RTC             NSH           control
          |
          |  every reply goes out through one serialised send path
          |  (any thread -> write /dev/ttyACM0)
          |
   "Traverse" thread <---- FIFO /dev/hcom_traverse_ku <---- kernel trace
          |
          |  ioctl() on /dev/upd
 =========|=======  kernel space  =======================================
          v
   meadow-upd.c (/dev/upd) --> hcom_nx: battery-backed registers, restart
                               and DFU, flash and file-system maintenance,
                               trace routing, configuration (meadow.config.yaml)

   board_late_initialize() --> hcom_nx_setup_mgr(): brings the platform up
```

Two points shape everything else in the documentation:

1. **The kernel and user halves are separate.** Meadow.OS is built as a NuttX protected build, so
   the HCOM application cannot touch privileged hardware or kernel state directly. Everything it
   needs from the kernel goes through ioctls on `/dev/upd`; text the kernel wants to send to the
   host comes back through the `/dev/hcom_traverse_ku` FIFO.
2. **One serial link carries everything.** Requests, file data, trace output, Mono's output and
   the debugger stream all share the single USB serial port, distinguished by message type.

## Document Map

The documents are grouped by layer. The order below is also a sensible reading order.

### A. The Host Protocol and the User-Space Pipeline

| # | Document | What it covers |
|---|----------|----------------|
| 1 | [WireProtocol.md](WireProtocol.md) | **The HCOM serial protocol.** COBS framing with 0x00 delimiters; the 12-byte standard header (sequence number, protocol version, request type, extra data, user data); the four message classes (header only, file start, text, binary); protocol versions and how they are negotiated; packet size limits; the Accepted / Concluded handshake that answers every request, and the requests that conclude later; and the catalogue of requests in both directions. Notes that the transport has no per-message checksum; only file transfers carry a CRC. |
| 2 | [HostLinkUsb.md](HostLinkUsb.md) | **The USB link.** How the USB CDC/ACM device is created at boot and exposed as `/dev/ttyACM0`; the separate read and write descriptors HCOM opens; what happens when no host is attached, when the cable is removed, and across low-power transitions; the second serial port of the developer dual-CDC build; DFU entry. |
| 3 | [ReceivePipeline.md](ReceivePipeline.md) | **CLI receive.** The `HcomRecv` thread, the delimiter-aware circular buffer that cuts the byte stream into packets, the semaphores that provide flow control, and how the download watchdog wakes the processing thread. |
| 4 | [SendPipeline.md](SendPipeline.md) | **CLI send.** How a reply is encoded and written; the serialising semaphore and the task-group rule; blocked-host detection and the retry budgets; the "Concluded pending" safety net; behaviour around low power. |
| 5 | [RequestRouting.md](RequestRouting.md) | **Request processing.** The `HcomProc` loop; packet validation; protocol-version checks; the unsupported-board path; the rules while a download is active; and the route table that sends each request type to its handler. |
| 6 | [UserspaceStartup.md](UserspaceStartup.md) | **HCOM startup and threads.** The order in which `hcom_main` brings things up, why each step waits where it does, every user-space thread with its priority and role, and how the startup thread becomes the request processor. |

### B. Features Reachable from the CLI

| # | Document | What it covers |
|---|----------|----------------|
| 7 | [FileDownload.md](FileDownload.md) | **Writing a file to the device.** Path parsing and categories (`/meadow0`, `/sdcard`, sub-directories); the download state machine; CRC checking (three-way); the 3-second watchdog and its clean-up; SD-card buffering. |
| 8 | [FileUploadDeleteList.md](FileUploadDeleteList.md) | **Reading, deleting and listing files.** Upload (device to host) sequence; delete and empty-directory clean-up; the file-list requests, with and without CRCs; the nested device and file list used by HcomDiagUi. |
| 9 | [MonoLifecycle.md](MonoLifecycle.md) | **Controlling Mono and its output.** How and when Mono is started, the pre-start checks, enable and disable (which are reboots), the crash-lockout bit; how Mono's stdout and stderr reach the host through two FIFOs; the Mono-related messages. |
| 10 | [MonoRemoteDebug.md](MonoRemoteDebug.md) | **The debugger bridge.** How Visual Studio's debugger traffic is relayed through the CLI and HCOM to the Mono debugger agent. Compiled into the current build, but not yet validated on hardware, and the document says so. |
| 11 | [DeviceControl.md](DeviceControl.md) | **Device-level requests.** Device information and name, restart, DFU mode, setting and reading the RTC and the wake-up time. |
| 12 | [DeveloperCommandsNsh.md](DeveloperCommandsNsh.md) | **Developer commands and NSH.** How the developer request (0xf8) is dispatched to user-space and kernel test tables, and how the NSH shell is started on a spare serial port. |

### C. The Kernel Side and the Platform

| # | Document | What it covers |
|---|----------|----------------|
| 13 | [`UpdInterface.md`](UpdInterface.md) | **The user-to-kernel interface.** `/dev/upd`, the `HCOM_NX_UPD_*` command block and how it coexists with the Mono-facing `MUPD_*` commands; the generic "forward a CLI command to the kernel" path and how errors and text come back; direct syscalls as a second mechanism; and the battery-backed register helpers. |
| 14 | [`KernelStartup.md`](KernelStartup.md) | **Platform bring-up.** What `board_late_initialize()` does and in what order (battery-backed domain, USB, MPU regions, `/dev/upd`, QSPI and hardware-version detection, file-system layer) and the sequence inside `hcom_nx_setup_mgr()`; the kernel threads Meadow adds. |
| 15 | [`ConfigManager.md`](ConfigManager.md) | **`meadow.config.yaml`.** Where it lives, how it is parsed (libcyaml), the schema sections, when each setting takes effect, and how user space reads it. |
| 16 | [`FlashPartitioning.md`](FlashPartitioning.md) | **QSPI flash layout and the file system.** The partition table on the QSPI flash, how the partitions and the littlefs file system are created and mounted at boot, the erase and verify requests, and the file-system *renew* request. Renew is implemented in HCOM but is not currently sent by the CLI; it is documented anyway. |
| 17 | [`TraceSyslog.md`](TraceSyslog.md) | **Trace and diagnostics.** How kernel syslog reaches the host (ramlog, the reader thread, the traverse FIFO), the trace-level mask and how it persists, routing trace to the host or a UART, and protocol decoding for debugging. |
| 18 | [`PlatformState.md`](PlatformState.md) | **Persistent platform state.** The battery-backed registers HCOM uses, hardware-version detection, and fault and crash reporting across resets. |
| 19 | [`GpioInterrupts.md`](GpioInterrupts.md) | **GPIO interrupt service (`meadow_interrupt.c`).** Not an HCOM feature, but a core part of what makes Meadow.OS work. How the runtime configures GPIO interrupts, how they are dispatched and debounced, and how events reach user space. Not yet surveyed in detail. |

### Appendix

| Document | What it covers |
|----------|----------------|
| [`SharedHeaders.md`](SharedHeaders.md) | An inventory of the headers in `F7MeadowOS/include/meadow/` that form the contract between kernel and user space, and what each defines. |

## Conventions Used in These Documents

- **ASCII only**, like the rest of the repository: diagrams use `+`, `-`, `|` and `->`.
- **Source references use file and function names, not line numbers.** Line numbers go stale
  quickly. Paths are relative to the repository root.
- **Constants and limits are quoted from the source** with the name of the `#define`, so they can
  be found again if they change.
- **Behaviour that is surprising, fragile or not yet verified is called out** in a short "Things to
  know" section at the end of a document. Confirmed defects are tracked as GitHub issues and are
  referenced by number rather than described at length.
- A request or message is written the way it appears in the source (`HCOM_MDOW_REQUEST_...` for
  host to Meadow, `HCOM_HOST_REQUEST_...` for Meadow to host).
