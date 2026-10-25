# Appendix: The Shared Headers

## Overview

`F7MeadowOS/include/meadow/` holds the headers that are shared across the kernel/user boundary, and in a few cases
with the bootloader or the managed runtime. They are included as `<meadow/...>`. In a protected build the kernel and the
user-space applications are compiled separately and meet only through these definitions, so a change to
one of them is a change to an interface: both sides must be rebuilt, and where the other end lives in a different
repository the change has to be made there too.

This appendix lists every header, what it defines, and the document that explains it. The headers are grouped by
purpose. Files not mentioned elsewhere in this series are described here from their contents.

## Rules for Changing Them

| Header | Who else depends on it | Rule |
|--------|-----------------------|------|
| `hcom_protocol.h` | The host CLI and any HCOM client | Request numbers, header layout and versions are the wire protocol ([WireProtocol.md](WireProtocol.md)); never renumber |
| `meadow-upd.h` | The Mono runtime and the managed `Interop.upd.cs` | The `MUPD_*` numbers and structures are an ABI; the interrupt configuration structure must match the managed side ([GpioInterrupts.md](GpioInterrupts.md)) |
| `hcom_upd_shared.h` | HCOM (user) and the kernel | New commands get a new number; 0x31 was removed and must not be reused; keep clear of NuttX's reserved ioctl blocks ([UpdInterface.md](UpdInterface.md)) |
| `meadow_partition_share.h`, `hcom_shared_common.h` (flash sizes), `bootloader/meadow_os_partitions.h` | The bootloader | The layout is duplicated in the bootloader's own headers; they must stay in step ([FlashPartitioning.md](FlashPartitioning.md)) |
| `hcom_bbreg_defn.h` | Kernel, HCOM, bootloader (register meaning) | Bit assignments persist across a restart; do not reuse a bit with a different meaning ([PlatformState.md](PlatformState.md)) |
| `hcom_traverse_ku.h` | Kernel and HCOM | The envelope must stay smaller than `PIPE_BUF` ([TraceSyslog.md](TraceSyslog.md)) |
| `espcp.h` | The ESP32 driver and HCOM | Both sides open the same queues and decode the same records; values for the ESP32 firmware are elsewhere (`espcp_shared_enums.h`) |
| `hcom_nuttx_shared.h` | The build | A template: it holds `###NAME###` placeholders that the build script fills in with the version, build time and git reference |
| `syscall.csv` entries | The NuttX syscall table | A header that declares a syscall is only half of it; see [UpdInterface.md](UpdInterface.md#direct-syscalls) |

## Host Protocol and Communications

| Header | Contents | Document |
|--------|----------|----------|
| `hcom_protocol.h` | The 12-byte header layout, the four request classes, every request number (host-to-device `HCOM_MDOW_REQUEST_*` and device-to-host `HCOM_HOST_REQUEST_*`), protocol versions | [WireProtocol.md](WireProtocol.md) |
| `hcom_shared_common.h` | The large catch-all: buffer sizes and limits, file names and mount points, the flash-size constants, timeouts, the `meadow_configuration_t` structure, fault-logging bits, `LOG_MDIAG`, and the developer switches (`HCOM_DIAG_*`) | Throughout; [ConfigManager.md](ConfigManager.md), [TraceSyslog.md](TraceSyslog.md) |
| `hcom_dnld_shared.h` | State shared by the file download and upload code: packet states, directory types, file message categories, the download watchdog (3 seconds) | [FileDownload.md](FileDownload.md) |
| `meadow_cirbuf.h` | The circular receive buffer (`hcom_cirbuf_*`). Not internally locked: safe only because each instance is used by one thread, or one producer and one consumer | [ReceivePipeline.md](ReceivePipeline.md) |
| `hcom_traverse_ku.h` | The kernel-to-user FIFO envelope and API | [TraceSyslog.md](TraceSyslog.md) |
| `hcom_nuttx_shared.h` | The product name, model, version fields, build date, processor and coprocessor type strings | [DeviceControl.md](DeviceControl.md#device-information) |

## The Kernel/User Interface

| Header | Contents | Document |
|--------|----------|----------|
| `meadow-upd.h` | The `MUPD_*` ioctl numbers and structures for the Mono runtime: registers, I2C, SPI, ESP32, directory enumeration, power reset and sleep, configuration values | [UpdInterface.md](UpdInterface.md) |
| `hcom_upd_shared.h` | The `HCOM_NX_UPD_*` command numbers (base 0x7000), their structures, and the `hcom_nx_cmd_data` forwarding structure | [UpdInterface.md](UpdInterface.md) |
| `meadow_syscall_support.h` | Types, constants and prototypes for the directly-callable syscalls of the newer peripherals (the DAC, for example) | [UpdInterface.md](UpdInterface.md#direct-syscalls) |
| `meadow_apps_core_share.h` | A handful of kernel functions exposed to user space as syscalls: GPIO configuration and write, `modifyreg32`, free flash space | [UpdInterface.md](UpdInterface.md#direct-syscalls) |
| `meadow_extended_errno.h` | Meadow's own error numbers, starting at `__ELASTERROR + 1`: path errors (invalid, slashes, unknown, depth, length), returned by the directory helper `hcom_file_dir_mgmt_utils.c`, and a task-group mismatch, returned by the send routine | [SendPipeline.md](SendPipeline.md) |
| `semaphore_lock_unlock.h` | `MEADOW_SEMAPHORE_LOCK` and the matching unlock macros, thin wrappers over `sem_wait`/`sem_post` | |

## Platform State, Flash and Configuration

| Header | Contents | Document |
|--------|----------|----------|
| `hcom_bbreg_defn.h` | Which battery-backed registers are used and every `BK31` bit | [PlatformState.md](PlatformState.md) |
| `meadow_os.h` | The reset-reason codes and the OS-level functions: the configuration copy and free ([ConfigManager.md](ConfigManager.md)), reset counters, hardware and protocol version, raising an exception to managed code, board reset, and ESP32 helpers | [PlatformState.md](PlatformState.md) |
| `meadow_os_battery_backed_domain.h` | Backup SRAM address and size, and the register and SRAM helper prototypes | [PlatformState.md](PlatformState.md) |
| `meadow_os_fault_handling.h` | The two fault-handler entry points | [PlatformState.md](PlatformState.md) |
| `meadow_os_persistent_data.h` | The layout of the OS's persistent data file (version, reset and power-cycle counts) | [PlatformState.md](PlatformState.md) |
| `meadow_hw_version.h` | Hardware version numbers, flash sizes per version, the version-pin patterns, the detection functions | [PlatformState.md](PlatformState.md) |
| `meadow_partition_share.h` | The QSPI partition sizes, numbers and offsets, and `meadow_part_info_t` | [FlashPartitioning.md](FlashPartitioning.md) |
| `hcom_dfu_mode_entry.h` | The SRAM address (0x2004FFF0) and magic value (0x1c0ffee2) for the DFU request | [HostLinkUsb.md](HostLinkUsb.md#dfu-entry) |
| `bootloader/meadow_os_partitions.h` | The bootloader's view of the same flash layout (NuttX image, SDRAM, QSPI, OTA data, persistent data) | [FlashPartitioning.md](FlashPartitioning.md) |
| `meadow_client_cert.h` | Default client-certificate file names and their paths (Cloud, out of scope) | |
| `meadow_cloud.h` | Prototypes for Meadow Cloud provisioning and private-key access; compiled only with `CONFIG_MEADOW_CLOUD` | |

## Threads, Power and Peripherals

| Header | Contents | Document |
|--------|----------|----------|
| `meadow_thread_config.h` | The name, priority and stack size of every Meadow thread, kept in priority order with the reasoning for each value | [ReceivePipeline.md](ReceivePipeline.md), [TraceSyslog.md](TraceSyslog.md), [MonoLifecycle.md](MonoLifecycle.md) |
| `meadow_pwr_mgmt.h` | The low-power callback type, the subscription function, power-management initialisation and the idle hook | [UpdInterface.md](UpdInterface.md) |
| `hcom_misc_diag.h` | Idle-monitor accessor callable from a syscall | [KernelStartup.md](KernelStartup.md) |
| `meadow_interrupt.h` | The GPIO interrupt configuration structure, the message format and the queue name | [GpioInterrupts.md](GpioInterrupts.md) |
| `meadow_gpio_counter.h` | The GPIO pulse counter: configuration, read data and the calls | |
| `meadow_rotary_encoder.h` | The rotary encoder: configuration, read data and the calls | |
| `meadow_measure_freq_shared.h` | Frequency and duty-cycle measurement: configuration options, timer configuration and the returned data | |
| `meadow_sdmmc.h` | SD card initialisation (`stm32_sdio_initialize_meadow()`), the card-detect pin and the slot number; SDMMC1 is rejected at build time | [KernelStartup.md](KernelStartup.md) |
| `meadow_ethnet_common.h` | The network-interface types and names, and `meadow_eth_mngr_startup()` | |
| `espcp.h` | Values shared by the ESP32 kernel driver and HCOM: queue names and the encoded event records | |
| `stm32f777zit6-meadow.h` | Board-level prototypes and definitions: bring-up, PWM setup, LED and pin assignments, procfs mount point | [KernelStartup.md](KernelStartup.md) |

## Tests and Diagnostics

| Header | Contents | Document |
|--------|----------|----------|
| `meadow_unit_test_framework.h` | The test numbers (`MEADOW_TEST_*`), result codes, the unit-test configuration structure and the test-writing macros | [DeveloperCommandsNsh.md](DeveloperCommandsNsh.md) |
| `meadow_kernel_tests.h` | Prototypes of every kernel-side test function | [DeveloperCommandsNsh.md](DeveloperCommandsNsh.md) |
| `meadow_kt_dispatcher.h` | The dispatcher syscall that runs a kernel test by number | [DeveloperCommandsNsh.md](DeveloperCommandsNsh.md) |
| `meadow_debug_helpers.h` | The `MEADOW_TRACE_*` macros, debug-pin macros and their on/off switches | [TraceSyslog.md](TraceSyslog.md) |
| `meadow_debug_breakpoint_macro.h` | Macros that insert a debug breakpoint at a source line | |

## Notes on Particular Files

- **`hcom_shared_common.h`** is the first place to look for any limit or switch. It mixes unrelated groups in one
  1,098-line file; the sections are marked with comment banners.
- **`meadow_syscall_support.h` and `meadow-upd.h` are different things.** The first is the typed syscall interface;
  the second is the ioctl one.
- **`stm32f777zit6-meadow.h` is the board header.** It still declares `stm32_bringup()`, which this configuration
  does not call ([KernelStartup.md](KernelStartup.md)).
- **The bootloader keeps its own copy of the flash layout.** `bootloader/meadow_os_partitions.h` mirrors the
  bootloader project's `Core/Inc/partitions.h`; comments in both ask for them to be kept in sync.

## Related Documents

[Overview.md](Overview.md), [WireProtocol.md](WireProtocol.md), [UpdInterface.md](UpdInterface.md),
[PlatformState.md](PlatformState.md), [FlashPartitioning.md](FlashPartitioning.md),
[Architecture.md](../Architecture.md).
