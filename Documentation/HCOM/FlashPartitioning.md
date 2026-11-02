# QSPI Flash Partitioning and the File System

## Overview

The Meadow F7 has a single external flash chip on the QUADSPI bus: a 64 MB Winbond W25Q512 (the F7 Feather V2
and the Core-Compute V2; the first-generation F7 Feather and its smaller flash are no longer supported,
Issue #453). It holds everything that is not in the MCU's own 2 MB: the Mono Runtime, the staging area for
OS updates, a few small state areas, and the user's file system. This document describes how the chip is
divided, how the file system is created and mounted at start-up, and the host requests that operate on the
flash: flashing the Mono Runtime, bulk erase, verifying an erase and renewing the file system.

OTA is out of scope; where a partition belongs to the OTA process it is only named.

## The Chip

| Property | Value |
|----------|-------|
| Size | 64 MB (`CONFIG_STM32F7_QSPI_FLASH_SIZE`) |
| Driver | NuttX `w25qxxxjv` MTD driver over the QUADSPI controller |
| Erase block ("sector") | 4096 bytes |
| Read/write block ("page") | 256 bytes |
| Memory-mapped at | `STM32_FMC_BANK4`, readable by user code through an MPU region ([KernelStartup.md](KernelStartup.md)) |

Two block sizes are in play and the code is careful, but easy to confuse. **Read and write** addresses are in
256-byte pages; **erase** addresses are in 4096-byte sectors. The flash helper functions state which they use.

## The Partition Layout

The first 5 MB is carved into fixed partitions; the file system takes **all the rest** of the chip. The sizes
are in `hcom_shared_common.h` and `meadow_partition_share.h`.

| # | Partition | Offset | Size | Used by |
|---|-----------|--------|------|---------|
| 0 | Runtime | 0x000000 | 3 MB (0x300000) | The Mono Runtime image ([MonoLifecycle.md](MonoLifecycle.md)) |
| 1 | OS update | 0x300000 | 1792 KB (0x1C0000) | OS update staging, used by the bootloader (OTA) |
| 2 | OTA state | 0x4C0000 | 4 KB | OTA state, used by the bootloader |
| 3 | OS persisted | 0x4C1000 | 4 KB | Reserved (see below) |
| 4 | Assert data | 0x4C2000 | 32 KB | Reserved (see below) |
| 5 | Unused | 0x4CA000 | 216 KB | Nothing |
| 6 | File system | 0x500000 | The remainder: 59 MB (61,865,984 bytes) on the 64 MB chip | LittleFS, mounted at `/meadow0` |

```
   0x000000 +-----------------------------------+
            |  0 Runtime                  3 MB  |
   0x300000 +-----------------------------------+
            |  1 OS update           1792 KB    |
   0x4C0000 +-----------------------------------+
            |  2 OTA state | 3 OS persisted | 4 Assert 32K | 5 Unused 216K
   0x500000 +-----------------------------------+
            |                                   |
            |  6 File system  (LittleFS)        |
            |      59 MB                        |
   0x4000000+-----------------------------------+
```

The comment in `hcom_nx_fs_part_utils.c` records that this layout "must match" the one the original Meadow OS
and the bootloader use, because the bootloader reads partitions 1 and 2 at fixed addresses. The values are
therefore **duplicated** in the bootloader (`bootloader/Core/Inc/partitions.h`) and in
`F7MeadowOS/include/meadow/bootloader/meadow_os_partitions.h`, with comments saying "sync with
`HCOM_NX_FS_MONO_RAW_PARTITION_SIZE`". Changing a size here without changing the bootloader breaks OTA.

Partitions 3 and 4 have kernel functions (`hcom_nx_exec_ex_flash_persistent_data_location()`,
`hcom_nx_exec_ex_flash_write_assertion_data()` and its reader) but in this repository only the flash test code
calls them; the reader has no callers at all. The OS's persistent data is an ordinary file
(`/meadow0/system/os_data.bin`, [KernelStartup.md](KernelStartup.md)), not the partition. Treat partitions 3, 4
and 5 as reserved but unused.

### Where the sizes come from

`MEADOW_FLASH_PART_SIZE_UNUSED_SPACE` is computed so that the total of partitions 0 to 5 is exactly 5 MB
(`MEADOW_FLASH_PART_SIZE_ALL_RESERVED`: 3 MB for the Runtime plus 2 MB "reserved"); a preprocessor check stops
the build if the arithmetic goes negative. The file system's size is not a constant. It is the chip's total size,
read from the driver's geometry at start-up, less the sizes of the other six partitions
(`MEADOW_FLASH_PART_SIZE_UNKNOWN` marks its slot in the table).

## Creating the Partitions

`hcom_nx_create_fs_initialize()` (`F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_create.c`) is startup-manager step 5
([KernelStartup.md](KernelStartup.md)). It runs once, at boot, and:

1. Clears the table of partition information (`_partInfoArray`, eight entries: the seven partitions and one
   for the whole chip).
2. Registers the whole chip as an MTD driver, `/dev/little0` (`register_mtddriver()`).
3. **Slices the chip with `mtd_partition()`**, one MTD object per partition, in order. It first checks that
   every fixed partition is a whole number of erase blocks (otherwise `-EINVAL`) and computes the file system's
   size from what remains. It records each partition's block offset, byte offset, size and MTD in
   `_partInfoArray`; entry 7 describes the whole chip.
4. Registers **only partition 6** as a device, `/dev/little0p6`.
5. Mounts it (below).

The other partitions exist only as MTD objects inside the kernel and have no device nodes of their own. The
whole-chip node `/dev/little0` does exist, so a privileged tool such as NSH can still reach every byte of the chip.

`hcom_nx_fs_get_partition_info()` returns the stored information for a partition, including its MTD. The data is not lock-protected, which the code comments accept because it is
written once at boot and every later caller runs on a single serialised task. The comments say this would need
revisiting if the `/dev/upd` entry points for OS update and Runtime copy were ever used from a second task.

## Mounting `/meadow0`

`hcom_nx_create_littlefs_mount_format_file_dev()` (`hcom_nx_fs_littlefs.c`):

```
   mount("/dev/little0p6", "/meadow0", "littlefs", 0, NULL)
        |
        +-- OK ---------------------------------------------> done
        |
        +-- -EFAULT (corrupt) or -EINVAL (never formatted) -->
        |        mount(..., "forceformat")  -- format, then mount
        |
        +-- any other error --------------------------------> failure is returned
```

The code comment says the LittleFS wrapper translates `LFS_ERR_CORRUPT` to `-EFAULT`, and adds that on a chip that had not been erased, NuttX 12 returned `-EINVAL`, and formatting resolved that too. Any
other mount error is returned up the start-up chain. Because a failing step 5 stops `hcom_nx_setup_mgr()`,
and the board file turns that into a `PANIC()`, an unusual flash failure stops the boot
([KernelStartup.md](KernelStartup.md)).

**A device that has lost its file system therefore reformats it silently** at the next boot: no message goes to the
host (HCOM is not running yet). All files, including `meadow.config.yaml`, are gone and the default
configuration is used ([ConfigManager.md](ConfigManager.md)).

## Access Rules for the Reserved Area

Kernel code that works with the first five partitions, for example the Mono Runtime flasher, does not call the
MTD driver directly. It goes through three proxies in `hcom_nx_fs_flash.c` (`MTD_BREAD_Proxy`,
`MTD_BWRITE_Proxy`, `MTD_ERASE_Proxy`) that call `partMtdBRead()`, `partMtdBWrite()` and `partMtdErase()`
(`hcom_nx_fs_part_utils.c`). Each converts an address on the **whole chip** into a partition and an offset
within it (`hcom_nx_flash_block_offset_to_part_offset()`), then calls the real MTD function on that partition's
MTD. The conversion:

- finds the partition whose range contains the address (with a one-entry cache for sequential access);
- **refuses the file system partition** with `-EINVAL` and a syslog message: "File System area in flash is
  restricted";
- checks that an address inside a partition is page-aligned and within the partition's size.

The purpose is that a bug in flashing or erasing code cannot overwrite the user's files, and that the OS's
reserved areas are bounded by their partition. The renew function is the one exception: it asks
for the file system's MTD explicitly and erases it directly.

## The Flash Requests

Four host requests, plus one internal call, act on the flash. (The request numbers are in
`hcom_protocol.h`.) All are forwarded to the kernel with
`CLI_COMMAND` ([UpdInterface.md](UpdInterface.md#forwarding-a-host-request-cli_command)) and run on the
`HcomProc` thread, so each **blocks other requests** until it finishes. The kernel's replies reach the host as
`TEXT_INFORMATION` messages through the `send_host_msg` pointer.

| Request | Kernel function | Restarts the device |
|---------|-----------------|---------------------|
| `MONO_FLASH` (and the end of a Runtime download) | `hcom_nx_exec_ex_flash_mono_flash()` | No |
| `BULK_FLASH_ERASE` | `hcom_nx_exec_ex_flash_erase_ex_flash()` | Yes |
| `VERIFY_ERASED_FLASH` | `hcom_nx_exec_ex_flash_verify_ex_flash()` | No |
| `PART_RENEW_FILE_SYS` | `hcom_nx_exec_ex_flash_renew_file_system()` | Yes |
| (internal) copy Runtime to RAM | `hcom_nx_exec_ex_flash_copy_blocks_to_memory()` | No |

### Flashing the Mono Runtime

The Runtime reaches the device as the file `/meadow0/Meadow.OS.Runtime.bin`, by an ordinary-looking download
with its own begin and end requests (`MONO_UPDATE_RUNTIME`, `MONO_UPDATE_FILE_END`;
[FileDownload.md](FileDownload.md)). When the end request arrives, the router calls the kernel
to copy the file into flash, the same function as a bare `MONO_FLASH` request:

1. Open the file. If it is missing: error text "Mono Runtime was not found in ...", return -1.
2. **The file must be exactly 3 MB** (`MEADOW_FLASH_PART_SIZE_MONO_RUNTIME`); another size gives "Mono Runtime
   binary has invalid size." and -1.
3. `TEXT_INFORMATION` "Erasing mono flash memory"; erase 768 sectors from the start of the chip through
   `MTD_ERASE_Proxy`, which confines it to partition 0; "Mono memory erase success".
4. Write the file one 256-byte page at a time through `MTD_BWRITE_Proxy`, sending "Flashing N% complete" at each
   10 %.
5. Rewind, re-read the file and compare it with the flash page by page ("Verifying N% complete"); a mismatch is
   an error.
6. "Runtime flashed successfully".

The device is **not restarted** by this code. The running copy of the Runtime is in SDRAM, copied from flash at
boot, so a new Runtime takes effect at the next start. A Runtime that fails halfway leaves partition 0
partly written; there is no second copy.

A failure at any step, including the erase, is sent to the host as `TEXT_ERROR` and the function returns -1.

### Copying the Runtime to RAM

Each time the Mono task starts, `mono_main()` asks the kernel (`COPY_RUNTIME_TO_RAM`) to read the 3 MB Runtime
partition, a page at a time, into SDRAM at `HCOM_NX_MONO_RUNTIME_SDRAM_ADDR` (0xC0000000), using
`hcom_nx_exec_ex_flash_copy_blocks_to_memory()`, which then reads it again and compares ([MonoLifecycle.md](MonoLifecycle.md)). The individual read results are not
checked; only the comparison pass can fail it, and then only with -1.

### Bulk erase

`BULK_FLASH_ERASE` sends "Bulk erase started (~2-3 minutes)" and issues `MTDIOC_BULKERASE`, which erases the
**entire chip** and so bypasses the partition rules. It replies "Bulk erase complete", or an error. The
router then sends `TEXT_RECONNECT` and restarts the device **whether or not the erase worked**, because the
forwarding call returns nothing ([DeviceControl.md](DeviceControl.md#restart)). After the restart the file
system is unformatted, so it is formatted and mounted empty; the Mono Runtime is gone, which the start-up logs as
"Mono Runtime was not found flashed in external flash", and managed code cannot run until the Runtime is flashed
again. The OS update and OTA areas are erased as well.

### Verifying an erase

`VERIFY_ERASED_FLASH` reads each 4 KB sector and counts those that are not all 0xFF, then sends "Verified N bytes
(M of T sectors), found K not erased." and replies `TEXT_CONCLUDED`. It covers the whole chip: the reserved area
(the first 5 MB) through the access proxy, and the file system partition through its own MTD
(`hcom_nx_fs_get_partition_info()`), because the proxy rejects the file system partition. A read failure is sent
to the host as `TEXT_ERROR`. The Runtime, if present, counts as not erased.

### Renewing the file system

`PART_RENEW_FILE_SYS` ("FS Renew" in the diagnostic client) discards the user's files:

1. Get the file system partition's MTD with `hcom_nx_fs_get_partition_info()`.
2. Erase its first **16 erase sectors** (64 KB) directly with `MTD_ERASE`, which removes the LittleFS superblock.
3. The router sends `TEXT_RECONNECT` and restarts the device (a failed erase is only logged).
4. At the next boot the mount fails with `-EFAULT` or `-EINVAL` and the file system is formatted fresh.

The rest of the partition is not wiped, so old data remains in the flash but is unreachable. Renew erases under a
mounted file system and relies on the immediate restart. The command exists in the protocol and in the
`HcomDiagUI` test client, but the current Meadow CLI does not send it. A device with a damaged file system is
repaired automatically at the next boot by the mount code above, which is the other route to the same result.

## Things to Know

- **A reformat is silent.** Any mount failure of the `-EFAULT`/`-EINVAL` kind erases all files at boot with no
  message to the host.
- **Bulk erase and renew restart the device even when they fail.**
- **Writes of the Runtime are not atomic.** An interrupted flash leaves a half-written, unusable Runtime and the
  device can no longer start Mono until it is flashed again; the bootloader and OS themselves are in separate
  areas and unaffected.
- **`IS_PART_MOUNTED` ignores its argument.** `hcom_nx_fs_is_mounted()` always answers for the file system
  partition. If the ioctl fails, the user-side wrapper reports "not mounted".
- **Layout changes touch the bootloader.** Partition sizes are repeated in the bootloader's headers.
- **Keep the 3 MB Runtime size in step.** The Runtime file size check, the SDRAM region size and the flash
  partition all use `HCOM_NX_FS_MONO_RAW_PARTITION_SIZE`.
- **A stale comment** says the OS update and Runtime-copy ioctls are "currently unwired"; both are handled in
  `meadow-upd.c`.

## Source Map

| Concern | File |
|---------|------|
| Partition constants | `F7MeadowOS/include/meadow/meadow_partition_share.h`, `F7MeadowOS/include/meadow/hcom_shared_common.h` |
| Partition creation, `hcom_nx_fs_get_partition_info()` | `F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_create.c` |
| LittleFS registration, mount and format | `F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_littlefs.c` |
| Partition tables and the access translation | `F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_part_utils.c` |
| Flash requests, Runtime flash and copy, assertion data | `F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_flash.c` |
| Developer flash tests | `F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_flash_tests.c` |
| Kernel routing of the requests | `F7MeadowOS/hcom_nx/hcom_nx_upd_route_cmd.c` |
| User-side routing and restart | `apps/system/hcom/comms/hcom_host_route.c` |
| Bootloader copies of the layout | `bootloader/Core/Inc/partitions.h`, `F7MeadowOS/include/meadow/bootloader/meadow_os_partitions.h` |

## Related Documents

[Memory.md](../Memory.md), [Architecture.md](../Architecture.md), [KernelStartup.md](KernelStartup.md),
[MonoLifecycle.md](MonoLifecycle.md), [FileDownload.md](FileDownload.md), [UpdInterface.md](UpdInterface.md),
[DeviceControl.md](DeviceControl.md), [ConfigManager.md](ConfigManager.md).
