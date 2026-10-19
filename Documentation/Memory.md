# Memory Layout

## Overview

The STM32F777ZIT6 provides 2 MB of internal flash, 512 KB of internal SRAM (split across three blocks), an ITCM interface, and external SDRAM and QSPI flash. The protected build partitions memory strictly between the kernel and user space.

The authoritative memory map is defined in:

```
nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/scripts/memory.ld
```

## Internal Flash (2 MB, `0x08000000`)

| Region | Name | Origin | Length | Contents |
|--------|------|--------|--------|----------|
| Boot | -- | `0x08000000` | 256 KB | Bootloader (`Meadow.BL`) |
| User flash | `uflash` | `0x08040000` | 1024 KB | NuttX user space (`nuttx_user.bin`); the kernel vector table occupies its first 512 bytes |
| Kernel flash | `kflash` | `0x08140000` | 768 KB | NuttX kernel (`nuttx.bin`) |

> The ITCM bus alias maps internal flash at `0x00200000`. NuttX boots through the `0x08xxxxxx` AXIM interface.

### Flash Sections (kernel)

Defined in `kernel-space.ld`:

| Section | Placed in | Description |
|---------|-----------|-------------|
| `.vectors` | `uflash` | Vector table |
| `.text` | `kflash` | Kernel code and read-only data |
| `.init_section` | `kflash` | C++ constructors |
| `.ARM.extab/.exidx` | `kflash` | Exception unwind tables |
| `.data` | `ksram` (LMA: `kflash`) | Kernel initialised data |
| `.bss` | `ksram` | Kernel zero-initialised data |

### Flash Sections (user)

Defined in `user-space.ld`:

| Section | Placed in | Description |
|---------|-----------|-------------|
| `.userspace` | `uflash` | User-space header |
| `.text` | `uflash` | User code and read-only data |
| `.data` | `usram` (LMA: `uflash`) | User initialised data |
| `.bss` | `usram` | User zero-initialised data |

## Internal SRAM (512 KB)

### DTCM SRAM -- `0x20000000`, 128 KB

Tightly coupled to the Cortex-M7 core: zero wait states and not cached.

```
dtcm (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
```

**Currently unused.** The linker places nothing in DTCM and `CONFIG_STM32F7_DTCMEXCLUDE=y` keeps
it out of every heap. NuttX excludes it by default when ELF loading is enabled
(`default LIBC_ARCH_ELF`), because ELF modules cannot execute from DTCM.

**Option: kernel heap headroom.** If the kernel heap (128 KB in SRAM1) runs out, DTCM can be added
to it, taking it to 256 KB. espcp allocates every socket operation from the kernel heap, so it is
the main consumer. Legacy Meadow did this in f0b7762b4f7 after espcp ENOMEM ("Unknown socket error")
under TLS/MQTT reconnect churn. It has not been needed on Rebase: no kernel-heap ENOMEM has been
seen, including in the Phase 6 TLS soak and with 15 concurrent sockets.

There is no speed cost. Before doing it:

- **DMA:** check every kernel driver that DMAs into `kmm_malloc()` buffers (espcp SPI, QSPI,
  SDMMC, USB, Ethernet). Legacy states that DTCM cannot be DMA'd and moved espcp's two SPI DMA
  buffers to static `.bss`. Confirm whether DMA can reach DTCM on the F7 (through the AHBS port;
  see RM0410) before relying on either answer.
- **Kernel heap only:** NuttX's `arm_addregion()` would add DTCM to the user heap with user MPU
  access. Add it to the kernel heap instead, with `kmm_addregion()` from the Meadow board
  initialisation rather than by changing `stm32_allocateheap.c`. That also keeps it away from
  ELF modules.
- **Reserve the black box:** the deferred fault recorder (issue #709) uses the last 256 bytes
  (`0x2001FF00`); keep them out of the heap.

### SRAM1 -- Kernel + User Split

SRAM1 is 368 KB total but the layout was extended to accommodate the Ethernet DMA buffers:

```
ksram (rwx) : ORIGIN = 0x20020000, LENGTH = 128K   (kernel)
usram (rwx) : ORIGIN = 0x20040000, LENGTH = 240K   (user, 368K - 128K)
```

Approximate kernel SRAM layout:

| Address | Contents |
|---------|----------|
| `0x20020000` | Kernel `.data` region (~0.1 KB typical) |
| -- | Kernel `.bss` region (~1.8 KB typical) |
| -- | Kernel IDLE thread stack (8 KB, sized by `CONFIG_IDLETHREAD_STACKSIZE`) |
| `0x20022000` | Start of kernel heap (128 KB, `CONFIG_MM_KERNEL_HEAPSIZE`) |

User SRAM layout:

| Address | Contents |
|---------|----------|
| `0x20040000` | User `.data` and `.bss` regions |
| -- | User heap (remainder of usram) |

### SRAM2 -- `0x2007c000`, 16 KB

```
sram2 (rwx) : ORIGIN = 0x2007c000, LENGTH = 16K
```

Reserved / miscellaneous kernel use.

## External SDRAM -- `0xC0000000`, 32 MB

Managed through the FMC (Flexible Memory Controller). Initialised by both the bootloader and the NuttX board bring-up.

SDRAM is split between the Mono runtime image and the user heap:

| Range | Size | Contents |
|-------|------|----------|
| `0xC0000000` - `0xC02FFFFF` | 3 MB | Mono runtime image (`sdram` region in `memory.ld`) |
| `0xC0300000` - `0xC1FFFFFF` | 29 MB | User heap region 2 (`CONFIG_HEAP2_BASE` / `CONFIG_HEAP2_SIZE`) |

```
sdram  (rwx) : ORIGIN = 0xC0000000, LENGTH = 3072K     (memory.ld)
CONFIG_HEAP2_BASE=0xc0300000                            (defconfig)
CONFIG_HEAP2_SIZE=30408704                              (29 MB, to the end of SDRAM)
```

- **Runtime image.** The runtime archives are linked into the `.mono*` sections of `user-space.ld`, starting with the `.mono_signature` version block. `build.sh` extracts them from `nuttx_user.bin` into `Meadow.OS.Runtime.bin` (3 MB). hcom writes that file to the raw runtime partition at the start of QSPI flash, and the `HCOM_NX_UPD_COPY_RUNTIME_TO_RAM` ioctl copies it back to `0xC0000000` (`HCOM_NX_MONO_RUNTIME_SDRAM_ADDR`) before the runtime starts. `.mono_bss` is not part of the image and is zeroed by the runtime host.
- **Heap.** The user heap starts straight after the image, so copying the runtime never overwrites live heap memory.
- **One layout, four places.** The layout is described by `HCOM_NX_MONO_RUNTIME_SDRAM_ADDR` and `HCOM_NX_FS_MONO_RAW_PARTITION_SIZE` (`hcom_shared_common.h`), the `sdram` region in `memory.ld`, and `CONFIG_HEAP2_BASE`/`CONFIG_HEAP2_SIZE`. `build.sh` (`check_mono_sdram_layout`) stops the build if they disagree, and takes the image's position in `nuttx_user.bin` from them.
- **MPU.** User space can read, write and execute the whole of SDRAM: the heap region is mapped by `stm32_mpu_uheap()`, and the runtime image area by an explicit region in `stm32_boot.c`.

The `MM_REGIONS=3` setting accounts for: ksram heap, usram heap, and SDRAM heap2.

## External QSPI Flash -- 64 MB

A 64 MB Winbond W25Q512 on the F7FeatherV2 and CCMv2 (`CONFIG_STM32F7_QSPI_FLASH_SIZE`). The F7FeatherV1 and its 32 MB Spansion S25FL are not supported: `board_late_initialize()` detects the V1 from the flash chip's ID, lights the red LED and sets up nothing more; hcom starts only host comms and answers every CLI request with a "not supported" error (Issue #453).

Not directly memory-mapped by NuttX at runtime but accessed through the QSPI driver. Partition layout (from `bootloader/Core/Inc/partitions.h`):

| Partition | Offset | Size | Contents |
|-----------|--------|------|----------|
| Mono runtime image | `0x00000000` | 3 MB (`HCOM_NX_FS_MONO_RAW_PARTITION_SIZE`) | Raw copy of `Meadow.OS.Runtime.bin`, copied to SDRAM when the runtime starts |
| Secondary OS image | `0x00300000` (`NUTTX_SEC_QSPI_LOC`) | 1792 KB (`NUTTX_SIZE`) | OTA staging image |
| OTA data | `NUTTX_SEC_QSPI_LOC + NUTTX_SIZE` | 256 B | OTA flags and state |
| File system | Remaining | ~61 MB | User files, managed assemblies, configuration |

## Combined `Meadow.OS.bin` Layout

The production flash image assembled by `build.sh` using `srec_cat` and `dd`:

| Offset | Contents |
|--------|----------|
| `0x00000000` | `Meadow.BL.bin` (bootloader) |
| `0x00040000` | `Meadow.OS.Update.bin` (kernel + user, with CRC at `0x001BFFFC`) |

Total image: up to ~2 MB of internal flash consumption.

## Tracking Memory Use with MemBrowse

[MemBrowse](https://membrowse.com) records how much of each memory region the build uses, for
every commit on `main` and every pull request into `main`, so that growth can be seen as it
happens instead of when a region overflows. It reads the ELF files and the linker scripts after
a CI build.

Two targets are tracked (`.github/membrowse-targets.json`):

| Target | ELF | Linker scripts |
|--------|-----|----------------|
| `meadow-kernel` | `nuttx/nuttx.elf` | `memory.ld`, `kernel-space.ld` |
| `meadow-user` | `nuttx/nuttx_user.elf` | `memory.ld`, `user-space.ld` |

The `meadow-user` figures include the Mono runtime image. Its `.mono*` sections are linked into
`nuttx_user.elf`, in the `sdram` region (see "External SDRAM" above).

The workflows:

- **`.github/workflows/membrowse.yml` ("MemBrowse Memory Report"):** builds the OS in the CI
  Docker image, using the runtime pinned in `MONO_RUNTIME_COMMIT`, and uploads one report per
  target.
- **`.github/workflows/membrowse-comment.yml` ("MemBrowse PR Comment"):** runs when the report
  workflow finishes for a pull request and posts the combined result as a comment on the PR. It
  is triggered by `workflow_run`, so GitHub runs the copy on `main`; a change to it takes effect
  only after it is merged.
- **`.github/workflows/membrowse-onboard.yml`:** run by hand (`workflow_dispatch`) to build and
  upload reports for a number of past commits, to fill in history. Commits from the managed-code
  merge onwards need the pinned runtime, which this workflow does not fetch.

The workflows need the `MEMBROWSE_API_KEY` repository secret (and optionally the
`MEMBROWSE_API_URL` variable). When a memory budget set in MemBrowse is exceeded, the report job
fails.

## MPU Configuration

The ARM MPU enforces the kernel/user boundary at runtime. Key rules enforced:

- Kernel flash (`kflash`) is readable and executable only in privileged (kernel) mode
- User flash (`uflash`) is readable and executable from unprivileged (user) mode
- Kernel SRAM (`ksram`) is inaccessible from user mode
- User SRAM (`usram`) is accessible from both modes (kernel reads user data via syscalls)
- SDRAM heap2 is accessible from both modes

## Battery-Backed Registers

The STM32 RTC provides 32 battery-backed registers (BKxxR) that retain their values across resets as long as VBAT is present. Meadow uses three for inter-boot communication:

| Register | Define | Purpose |
|----------|--------|---------|
| `BK31R` | `HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER` | Main Meadow control flags (syslog level, Mono state, restart flags) |
| `BK30R` | `HCOM_NX_MEADOW_RESET_REASON_BBR` | Reset reason code |
| `BK29R` | `HCOM_NX_MEADOW_RESET_SOURCE_INFO_BBR` | Reset source additional info |

Notable flag bits in `BK31R`:

| Bit Mask | Flag |
|----------|------|
| `0x000000FF` | Syslog level configuration |
| `0x00000100` | Restart initiated by host command |
| `0x00000200` | Route trace messages to host |
| `0x00000400` | Route trace messages to UART |
| `0x00000800` | User requested Mono enable |
| `0x00001000` | Mono last run lockup detected |
| `0x00002000` | Mono debugging start flag |
