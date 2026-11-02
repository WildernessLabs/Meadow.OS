# System Architecture

## Protected Build Model

Meadow.OS uses the NuttX **protected build** (`CONFIG_BUILD_PROTECTED=y`). The ARM Cortex-M7 MPU enforces a hard boundary between kernel-mode and user-mode code at runtime. Neither region can directly access the other's memory; user space crosses into the kernel exclusively through the syscall interface.

Two separate ELF files are produced:

| Binary | Region | Load Address |
|--------|--------|-------------|
| `nuttx.bin` (kernel) | kernel vectors at the start of uflash, code in kflash | `0x08040000` (vectors), `0x08140000` (code) |
| `nuttx_user.bin` (user) | uflash | `0x08040200` (`CONFIG_NUTTX_USERSPACE`) |

The combined `Meadow.OS.bin` image is assembled by the build script using `srec_cat` and `dd`.

## Component Diagram

```
 +---------------------------------------------------------------------+
 |  Internal Flash (2 MB)                                              |
 |                                                                     |
 |  0x08000000  +----------------------+  Bootloader (Meadow.BL)       |
 |              |     Meadow.BL        |  256 KB                       |
 |  0x08040000  +----------------------+  Kernel vector table (512 B)  |
 |  0x08040200  +----------------------+  NuttX user image             |
 |              |  nuttx_user (hcom)   |  uflash - 1 MB in all         |
 |  0x08140000  +----------------------+  NuttX kernel image           |
 |              |   nuttx (kernel)     |  kflash - 768 KB              |
 |  0x08200000  +----------------------+                               |
 +---------------------------------------------------------------------+

 +---------------------------------------------------------------------+
 |  SRAM                                                               |
 |  0x20000000  DTCM (128 KB)   - stack, fast kernel data              |
 |  0x20020000  ksram (128 KB)  - kernel .data / .bss / IDLE stack     |
 |  0x20040000  usram (240 KB)  - user .data / .bss / user heap        |
 |  0x2007c000  SRAM2 (16 KB)   - reserved                             |
 +---------------------------------------------------------------------+

 +---------------------------------------------------------------------+
 |  External Memory                                                    |
 |  0xC0000000  SDRAM (32 MB)   - heap2 (3 MB exposed to NuttX)        |
 |  QSPI        64 MB Flash     - secondary OS image / FS / runtime    |
 +---------------------------------------------------------------------+
```

## Bootloader

The bootloader (`bootloader`) is an STM32 HAL application built with a makefile. It is the first code to run after reset and is responsible for bringing the hardware to a known state before handing off to the OS.

### Boot Sequence

```
Power on / Reset
      |
      v
  HAL_Init(), SystemClock_Config()
      |
      v
  Init SDRAM (FMC), QSPI flash
      |
      v
  Detect board version (JEDEC ID)
      |
      v
  CheckPreviousOperationFailure()
      |
      +-- OTA update pending?  --> PerformUpdate()
      +-- Rollback pending?    --> PerformRollback()
      +-- No action            --> BootMeadowOS()
                                        |
                                        v
                               Jump to 0x08040000
```

### OTA State Machine

OTA state is communicated between the bootloader and NuttX using RTC battery-backed registers and OTA flag bytes stored in QSPI. The state machine uses the enums defined in `bootloader/Core/Inc/ota_data.h`:

| State | Owner | Description |
|-------|-------|-------------|
| `update_nuttx_pending` | Set by NuttX | OS requests update on next boot |
| `update_nuttx_in_progress` | Set/cleared by BL | BL is writing new image to kflash |
| `update_nuttx_complete` | Set by BL | BL finished; NuttX clears on next boot |
| `update_nuttx_failed` | Set by BL | CRC or write failed; rollback occurs |

Rollback and backup states follow the same pattern.

## NuttX Kernel

The NuttX kernel is configured with the `meadow_os` defconfig for the `stm32f777zit6-meadow` board target. Key architectural decisions:

### Scheduler
- Priority-based preemptive scheduler
- High-priority (`SCHED_HPWORK`) and low-priority (`SCHED_LPWORK`, 2 threads) work queues
- POSIX `pthread` and `waitpid` support enabled

### Memory Protection
- ARM MPU enabled (`CONFIG_ARM_MPU=y`)
- Kernel heap: 128 KB (`CONFIG_MM_KERNEL_HEAPSIZE=131072`)
- Three memory regions (`CONFIG_MM_REGIONS=3`): ksram, usram, SDRAM heap2

### Caches
- I-cache enabled (`CONFIG_ARMV7M_ICACHE=y`)
- D-cache enabled in write-through mode (`CONFIG_ARMV7M_DCACHE=y`, `CONFIG_ARMV7M_DCACHE_WRITETHROUGH=y`)
- DTCM enabled (`CONFIG_ARMV7M_DTCM=y`)

### File Systems
- FAT (`CONFIG_FS_FAT=y`) with long filename support
- LittleFS (`CONFIG_FS_LITTLEFS=y`) for wear-levelled QSPI partitions
- procfs is off in the `meadow_os` defconfig; it is mounted at `/proc` only in NSH builds that enable
  `CONFIG_FS_PROCFS`

### Networking
- Full TCP/IP stack enabled (`CONFIG_NET=y`)
- DNS client with resolv.conf at `/meadow0/dns.conf`
- IGMP, ICMP, TCP, UDP, local sockets, loopback
- Routing table support
- WiFi via ESP32 usrsock driver
- Ethernet via LAN8742A PHY (`CONFIG_ETH0_PHY_LAN8742A=y`)

## Wilderness Labs Kernel Extensions (F7MeadowOS/)

All custom kernel-mode code lives in the `F7MeadowOS` directory (symlinked into the board tree
as `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/F7MeadowOSLink`) and is compiled as part of
the kernel pass. See [Kernelspace.md](Kernelspace.md) for the full module list, including vendored
third-party libraries (`libyaml`, `libcyaml`) and modules not covered below (`timers`,
`specialized`, `ntpclient`, `meadow_cloud`). It is structured as follows:

### `hcom_nx` - Kernel-Side Host Communication

The kernel half of the host communication system. Key modules:

| File | Responsibility |
|------|---------------|
| `hcom_nx_startup_mgr.c` | Called during NuttX startup; initialises hardware, config, networking |
| `hcom_nx_config_manager.c` | Parses YAML config for system configuration |
| `hcom_nx_bbreg_access.c` | Read/write RTC battery-backed registers |
| `hcom_nx_upd_route_cmd.c` | Routes update commands between user space and the kernel |

The kernel-side entry point for user space is the Universal Platform Driver,
`meadow_os/meadow-upd.c` (`/dev/upd`). It serves both the `MUPD_*` commands used by the
.NET runtime and the `HCOM_NX_UPD_*` commands used by HCOM; the latter had their own
driver on `/dev/nxupd` until the two were consolidated.

### `meadow_os` - Core OS Services

| File | Responsibility |
|------|---------------|
| `meadow_os.c` | Board reset, core OS lifecycle |
| `meadow_os_fault_handling.c` | Hard fault and exception handlers |
| `meadow_os_battery_backed_domain.c` | RTC domain management (reset reason, syslog config) |
| `meadow_os_config.c` | OS-level configuration persistence |
| `meadow_os_info.c` | Board/version information |

### `espcp` - ESP32 Coprocessor

Custom protocol layer that communicates with the ESP32 WiFi coprocessor over a dedicated UART and SPI. Exposes network connectivity to NuttX via the `usrsock` interface.

The ESP32 firmware itself is not part of this repository: it is built and released from the
separate Meadow-ESP32 repository. This repository contains only the STM32 side (`espcp` in the
kernel and `esp32/` in `hcom`).

## User Space

The single user-space application is `hcom` (entry point `hcom_main`). See [Userspace.md](Userspace.md) for a detailed breakdown.
