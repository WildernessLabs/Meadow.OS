# USB Composite: Serial + Mass Storage

Plan of work to expose `/meadow0` to the host computer as a USB drive alongside the
existing USB serial port, without giving up `/dev/ttyACM0`.

## Status

| Stage | State |
|-------|-------|
| 1 - Composite bring-up (RAM disk LUN) | **Done.** Verified on macOS. |
| 2 - Storage mode control plane | **Done.** Verified on macOS. |
| 3 - Read-only synthetic FAT over `/meadow0` | **Done.** Verified on macOS, contents byte-identical to HCOM. |
| 4 - Write and delete path | Not started. |
| 5 - Hardening and integration | Not started. |

A composite CDC/ACM plus mass storage device enumerates, `/dev/ttyACM0` and HCOM are
unaffected, and a medium appears and disappears with the runtime-enable state.  That
medium is now a read-only FAT16 view of `/meadow0`, synthesized on demand.  Stages 1 and 2
are confirmed on hardware; Stage 3 is validated off-target but has not been run on a
board.

Verified on macOS: enumeration, descriptors, mount, file data reads, host writes, eject,
mode transitions in both directions, `meadow file write` and read over HCOM.  Windows and
Linux USB testing is deferred on hardware availability; the FAT structure alone has been
checked against the Linux `vfat` driver and Apple's `fsck_msdos`.

Open items are listed per stage.  The ones that could still find a defect are the host's
behaviour while the drive is empty, and concurrent HCOM plus mass storage load.

## Goal

The host computer should see a single USB device that presents both:

1. A CDC/ACM serial port, exactly as today (HCOM depends on `/dev/ttyACM0`).
2. A USB Mass Storage (MSC) drive exposing `/meadow0`, which the host can mount and
   browse natively.

## Scope Decisions

These were settled before planning and constrain everything below.

| # | Decision | Consequence |
|---|----------|-------------|
| 1 | **Target `/meadow0` only.** The SD card is not in scope. | The synthetic FAT layer is mandatory - there is no cheap path. |
| 2 | **Long term full read / write / delete. Start read-only.** | Stage 3 ships read-only; Stage 4 adds writes. |
| 3 | **Explicit storage mode**, not always-on, and active whenever the runtime is disabled. | Ownership arbitration collapses to a mode transition, and the transition is an existing command plus the restart it already does. No new protocol was needed. |
| 4 | **New USB PID: undecided.** | Tracked as the one remaining open item. See "Remaining Open Item". |
| 5 | **Full filesystem access required.** No protected-file carve-outs. | Subdirectories become mandatory in Stage 3, not deferred. Raises the stakes on Stage 4. |

Decision 5 is the one with the largest planning impact: "full filesystem access" means
`update/`, `rollback/`, `crash/` and `system/` must all be visible, so the synthetic
filesystem cannot be a flat root directory. That moves FAT subdirectory support out of
late-stage polish and into Stage 3, the first deliverable that exposes `/meadow0` at all.

## Which Storage Device

For the record, since the terminology is ambiguous. Three distinct storage devices exist
on the Meadow F7; per decision 1, only the middle one is in scope.

| Device | Contents | Host-readable? | In scope |
|--------|----------|----------------|----------|
| STM32 internal flash (2 MB, `0x08000000`) | Bootloader, kernel at `0x08040000`, user image | No filesystem at all | No |
| External QSPI NOR (32 MB, S25FL / W25QXXXJV) | `/meadow0`, ~27 MB, **LittleFS** | No - no host OS reads LittleFS | **Yes** |
| SD card | `/sdcard`, **vfat**, block device `/dev/mmcsd0` | Yes, natively | No |

`/meadow0` is LittleFS registered as an MTD driver (`/dev/little0`, via
`register_mtddriver()` in `F7MeadowOS/hcom_nx/create_fs/hcom_nx_fs_create.c`), not a
block driver. MSC requires a block driver, and no host OS can read LittleFS. Both
problems are solved by the same thing: a synthetic FAT block driver.

## Current State (verified)

What already exists in the tree:

* `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_composite.c` - full composite
  descriptor assembly, with `CONFIG_CDCACM_COMPOSITE` and `CONFIG_USBMSC_COMPOSITE`
  blocks and endpoint / interface / string base accounting already written. Contains
  `board_mscclassobject()`, which calls `usbmsc_configure()` then
  `usbmsc_bindlun(g_mschandle, "/dev/mmcsd0", 0, 0, 0, false)` - an upstream leftover to
  be retargeted.
* `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_usbmsc.c` - stub
  `board_usbmsc_initialize()` returning `OK`.
* `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/configs/dualcdcacm/defconfig` - a
  working composite configuration on this board; reference for `CONFIG_USBDEV_COMPOSITE`,
  `CONFIG_COMPOSITE_IAD`, `CONFIG_COMPOSITE_MSFT_OS_DESCRIPTORS`.
* Make.defs already gates both files correctly: `stm32_usbmsc.c` on `CONFIG_USBMSC`,
  `stm32_composite.c` on `CONFIG_USBDEV_COMPOSITE`.
* `nuttx/boards/boardctl.c` handles `BOARDIOC_USBDEV_COMPOSITE` with
  `INITIALIZE` / `CONNECT` / `DISCONNECT`.
* `ramdisk_register()` in `nuttx/drivers/misc/ramdisk.c` is built unconditionally
  whenever `CONFIG_DISABLE_MOUNTPOINT` is unset, so it is already available - useful as
  a zero-cost Stage 1 test vehicle.
* Kernel-space file and directory access is proven here: `file_open()` in
  `F7MeadowOS/meadow_os/meadow_os_info.c`, `opendir()` / `readdir()` in
  `F7MeadowOS/meadow_os/meadow-upd.c`.
* HCOM request/response enums live in `F7MeadowOS/include/meadow/hcom_protocol.h`
  (`HCOM_MDOW_REQUEST_*` host-to-device, `HCOM_HOST_REQUEST_*` device-to-host) - where
  the storage-mode control commands will be added.

What the `meadow_os` defconfig does today:

* `CONFIG_CDCACM=y`, `CONFIG_USBDEV=y`, `CONFIG_BOARDCTL_USBDEVCTRL=y`, VID `0x2E6A`,
  PID `0x0001`. No composite and no MSC symbols.
* `stm32_boot.c` `board_init_usbdev()` connects a standalone `BOARDIOC_USBDEV_CDCACM`.

## Key Constraints

1. **MSC is a block protocol.** It ships raw sectors; the *host* runs the filesystem
   driver. `usbmsc_bindlun()` requires a registered block driver with a working
   `geometry()` - `usbmsc.c` rejects a LUN whose `geometry()` is absent or reports
   `geo_available == false`.

2. **The SCSI layer does single-sector I/O.** `usbmsc_scsi.c` calls
   `USBMSC_DRVR_READ(lun, priv->iobuffer, sector, 1)` and the matching single-sector
   write. No multi-sector spanning to handle in the block driver.

3. **`usbmsc` never checks `geo_mediachanged`.** The only host-invalidation path in the
   SCSI layer is `lun->uad` (unit attention), set at bind time. There is no upstream
   mechanism to tell an already-mounted host that the board changed the filesystem.
   Explicit storage mode (decision 3) sidesteps this rather than solving it.

4. **Concurrent ownership is unsafe.** The host caches sectors and directory metadata and
   assumes exclusive ownership. Board-side writes to `/meadow0` (`meadow.log`, OTA
   staging, crash dumps) underneath a mounted host view corrupt both views. Exactly one
   writer at a time.

5. **Descriptor change is a host-compatibility event.** Moving from standalone CDC/ACM to
   composite changes the device descriptors. Needs a new PID, and Meadow CLI enumeration,
   udev rules and Windows driver binding must tolerate a composite parent with the ACM
   port as a child interface.

6. **Endpoint budget is adequate.** OTG FS provides 6 endpoint pairs
   (`STM32_NENDPOINTS`, `nuttx/arch/arm/src/stm32f7/stm32_otg.h`). CDC/ACM needs int-IN +
   bulk-IN + bulk-OUT; MSC needs bulk-IN + bulk-OUT. EP0 plus 3 IN and 2 OUT, with
   headroom.

7. **ASCII only, checkpatch clean.** Per `CLAUDE.md`. New and touched files must pass
   `./tools/checkpatch.sh -f` from `nuttx/`.

---

## Stage 1 - Composite bring-up (RAM disk LUN) - IMPLEMENTED

Status: **implemented and confirmed working on hardware (macOS).**  Windows and Linux
still unverified.

Purpose was to prove the composite USB descriptor change end to end, in isolation from
any synthetic file system work.  The LUN is a throwaway test vehicle, not a feature.

Since the SD card is out of scope (decision 1), the vehicle is a small FAT12 RAM disk.
It needs no card, no new heap, and no ownership arbitration, which is exactly what makes
it a clean isolation of the descriptor risk.

### What was changed

| File | Change |
|------|--------|
| `Kconfig` | New `MEADOW_USBMSC_RAMDISK` (default n) and `MEADOW_USBMSC_RAMDISK_SECTORS` (default 128). |
| `configs/meadow_os/defconfig` | +13 lines: composite and MSC symbols, provisional PID `0x0002`, RAM disk enabled. |
| `src/stm32_usbmsc.h` | New. `MEADOW_USBMSC_LUN0_DEVPATH` and `meadow_usbmsc_lun_available()`. |
| `src/stm32_usbmsc.c` | Hand-built FAT12 formatter and `ramdisk_register()`; block device existence check. |
| `src/stm32_composite.c` | LUN retargeted off the hardcoded `/dev/mmcsd0`; MSC interface now conditional; `board_composite_initialize()` implemented. |
| `src/stm32_boot.c` | `board_init_usbdev()` gained the composite INITIALIZE + CONNECT path. |
| `scripts/kernel-space.ld` | New `.dtcm (NOLOAD)` section. |

Resulting configuration, confirmed by regenerating `.config` from the defconfig:
`CONFIG_USBDEV_COMPOSITE`, `CONFIG_CDCACM_COMPOSITE`, `CONFIG_USBMSC`,
`CONFIG_USBMSC_COMPOSITE`, `CONFIG_COMPOSITE_IAD`,
`CONFIG_COMPOSITE_MSFT_OS_DESCRIPTORS`, VID `0x2E6A`, PID `0x0002`, bulk request
lengths raised to 512.  `CONFIG_COMPOSITE_SERIALSTR` is deliberately absent:
`CONFIG_BOARD_USBDEV_SERIALSTR` is set, so the serial string keeps coming from
`board_usbdev_serialstr()` in `F7MeadowOS/hcom_nx/hcom_nx_common_utils.c`.

### Findings worth carrying forward

**The RAM disk does not fit in ksram, and DTCM is free.**  A 64 KB static buffer in
kernel `.bss` overflowed `ksram` by 13548 bytes at link time.  `ksram` is 128 KB and the
kernel already uses 79084 bytes of it, leaving only about 50 KB.  The buffer also cannot
come from the heap: `arm_addregion()` hands every extra region, the 32 MB of external
SDRAM at `CONFIG_HEAP2_BASE` included, to the *user* heap via `kumm_addregion()`, so a
kernel allocation can only draw on the 128 KB `CONFIG_MM_KERNEL_HEAPSIZE`.

DTCM turned out to be the answer: 128 KB at `0x20000000` that nothing claims, because
`CONFIG_STM32F7_DTCMEXCLUDE=y` keeps `stm32_allocateheap.c` from adding it to any heap.
A `.dtcm (NOLOAD)` section in `kernel-space.ld` plus `locate_data(".dtcm")` puts the
buffer there at zero cost to `ksram`, which returned to its exact pre-change 79084 bytes.

This is directly relevant to the Stage 3 RAM audit: there is 128 KB of unclaimed DTCM
available for the synthetic file system's directory model, not just `ksram` scraps.

**A pre-existing bug in the USB error check.**  The call site in `board_late_initialize()`
tested `if(ret > 0)`, but `board_init_usbdev()` returns zero or a negated errno, so USB
failures were silently ignored.  Corrected to `ret < 0`.  The handler deliberately logs
and continues rather than returning: the file system and `/dev/upd` registration further
down matter more than USB, so aborting board init on a USB fault would turn a minor
problem into an unusable board.

### Graceful degradation

Task 5 of the original plan is implemented as a runtime decision at connect time rather
than an error path.  `board_usbmsc_initialize()` confirms the backing block device exists
with `nx_stat()`, and `board_composite0_connect()` omits the mass storage devdesc entry
entirely when it does not.  Advertising the interface and then failing `usbmsc_bindlun()`
would fail `composite_initialize()` outright and cost the CDC/ACM interface too, which is
the one HCOM needs.  A missing medium therefore degrades to a serial-only device.

### Hardware results

Confirmed on macOS:

* The board boots and enumerates as a composite device.
* A volume named `MEADOW BOOT` appears in Finder and mounts cleanly.  **The 64 KB FAT12
  concern is resolved** - 121 clusters is unusually small but macOS accepted it without
  complaint, so there is no need to raise
  `CONFIG_MEADOW_USBMSC_RAMDISK_SECTORS`.
* `/dev/ttyACM0` survived the descriptor change: Meadow CLI lists files on `/meadow0`,
  which means HCOM is talking normally.
* `meadow file write` transfers a file to `/meadow0` successfully.  This is stronger than
  the listing: it exercises the HCOM protocol, the download state machine and the
  LittleFS write path, all over the CDC/ACM interface of a composite device.  The two
  interfaces coexist on the shared OTG FS core without endpoint or enumeration conflict.
* Reading a file back from the device to the host over HCOM also works, so both directions
  of the file transfer path are confirmed.
* Meadow CLI discovers the board on the new PID `0x0002` with no host-side change.  This
  was the main descriptor risk and it did not materialize.
* Dragging a file onto the volume in Finder works, so host writes reach the medium
  through the SCSI and block layers.
* `README.TXT` is listed and its contents read back correctly.  This is the significant
  one: it closes the **file data read path** end to end, which an empty volume could not
  exercise.  Directory entry synthesis, the FAT12 cluster chain and multi-cluster data
  reassembly are all confirmed against a real host FAT driver, not just against the
  Python cross-check.
* The volume can be ejected from Finder without errors.  See the caveat below - this is
  less than it appears.

An empty volume was the expected result on first boot and is not a fault.  The RAM disk
has no relationship to `/meadow0`; it is formatted blank in DTCM on every boot.

### Seeded README.TXT

An empty volume only exercises boot sector, FAT and root directory reads, so a successful
mount said nothing about whether *file data* reads work.  `meadow_usbmsc_mkfat12()` now
seeds a `README.TXT` into the root directory at format time, which puts real content in
the data region and makes every read path exercised on mount.

The file also explains, to anyone who finds the drive, that this is not the Meadow file
system and points at this document.

The seeding code is deliberately written against cluster *runs* rather than assuming one
cluster, because that is the same arithmetic the stage 3 synthetic view needs:

* `meadow_fat12_set_entry()` - packs a 12 bit FAT entry into every FAT copy, handling the
  even/odd nibble split with a read-modify-write so the neighbouring entry survives.
* `meadow_usbmsc_dirent()` - addresses a 32 byte root directory slot.
* `meadow_usbmsc_clusterdata()` - maps a cluster number to its data offset.
* `meadow_usbmsc_addfile()` - allocates a run, chains it, copies the content and fills in
  the directory entry.

This turned out to matter: the README is 636 bytes, so it spans two clusters.  A
single-cluster shortcut would have produced a silently truncated file.

The layout was verified before flashing by reproducing the identical algorithm in Python
and then parsing the result back with an independent FAT12 reader: both FAT copies byte
identical, reserved entries `0xFF8`/`0xFFF`, cluster chain `[2, 3]` terminating at
`0xFFF`, and 636 bytes reassembled matching the source exactly.

Cost: 808 bytes of kernel flash, zero `ksram`.

### Descriptor capture (macOS)

Read back from the device with `ioreg` and `system_profiler SPUSBHostDataType`.

Note the data type name: on current macOS the USB type is **`SPUSBHostDataType`**.
`SPUSBDataType` no longer exists and silently produces zero bytes rather than an error,
which makes it look as though the device is absent.  `system_profiler -listDataTypes`
confirms which names the host actually supports.

| Field | Value | Meaning |
|-------|-------|---------|
| `idVendor` | `0x2e6a` | as configured |
| `idProduct` | `0x0002` | the provisional PID took effect |
| `bcdDevice` | `0x1010` | `CONFIG_COMPOSITE_VERSIONNO` |
| Manufacturer | `Wilderness Labs` | `CONFIG_COMPOSITE_VENDORSTR` |
| Product | `Meadow F7 Micro` | `CONFIG_COMPOSITE_PRODUCTSTR` |
| Serial Number | `336031743438` | **12 hex characters, not `0101`** |
| `bDeviceClass` | `239` (`0xEF`) | Miscellaneous, the IAD composite class |
| `bNumConfigurations` | `1` | |
| Link speed | 12 Mb/s | full speed, as expected for OTG FS |

Three interfaces, matching the `ifnobase` accounting in `board_composite0_connect()`
exactly:

| Interface | Class | Subclass | Protocol | Meaning |
|-----------|-------|----------|----------|---------|
| 0 | 2 | 2 | 1 | CDC communications, ACM |
| 1 | 10 | 0 | 0 | CDC data |
| 2 | 8 | 6 | 80 | Mass storage, SCSI transparent, Bulk-Only |

Two results close out the open risks:

* **The serial string check passes.**  `336031743438` is the 12 character chip serial from
  `board_usbdev_serialstr()`, not the `0101` placeholder that `composite_desc.c` compiles
  into the string table.  The board hook is being consulted through the composite path.
  Corroborated independently by the device node name, `/dev/cu.usbmodem3360317434381`,
  which macOS builds from the serial plus the interface number.
* **IAD grouping works.**  `bDeviceClass` `0xEF` is the Miscellaneous class that marks a
  device using Interface Association Descriptors, so `CONFIG_COMPOSITE_IAD=y` took effect
  and macOS binds interfaces 0 and 1 as a single modem rather than two orphans.

One field is easy to misread: `system_profiler` reports `Connection Type: Removable` for
this device.  That refers to the USB device being hot-pluggable and has nothing to do with
`CONFIG_USBMSC_REMOVABLE`, which governs SCSI *media* removability and is still off.

### What the eject did *not* prove

Finder ejecting the volume cleanly is a host-side unmount, not a SCSI eject, and it does
not validate anything Stage 2 needs.

`CONFIG_USBMSC_REMOVABLE` is deliberately unset in this configuration, and that has three
consequences in `usbmsc_scsi.c`:

* INQUIRY does not set the `RMB` bit, so the host is told the media is **not** removable.
* `SCSI_CMD_STARTSTOPUNIT` is answered with `SCSI_KCQIR_INVALIDCOMMAND` - the firmware
  actively rejects the eject command.
* `PREVENT ALLOW MEDIUM REMOVAL` is compiled out.

So macOS either did not issue START STOP UNIT or was refused, and detached the file system
on its own either way.  Good news as far as it goes - the detach is clean and provokes no
I/O errors - but the media-removal path is untested because it is not enabled.

Stage 2 will need `CONFIG_USBMSC_REMOVABLE=y`, because the media-not-present reporting it
depends on is meaningless to a host that has been told the medium is fixed.

### Cross-platform: deferred, with a partial surrogate

No Windows or Linux machine is currently available, so USB level testing on those hosts is
deferred rather than skipped.

That leaves the FAT layout itself testable without them, and it was checked against three
implementations besides the host driver on the real device:

| Check | Tool | Result |
|-------|------|--------|
| Structural integrity | `fsck_msdos -n` (Apple's checker) | Phases 1-3 clean, exit 0, "1 files, 119 clusters free" - exactly 121 total minus the 2 README clusters |
| macOS driver, disk image | `hdiutil` plus `msdosfs` | Volume `MEADOW BOOT`, `README.TXT` 636 bytes, content correct |
| **Linux driver** | kernel `vfat`, loop mounted in a privileged container | Mounts clean, `README.TXT` 636 bytes, content correct |

Be clear about what this does and does not cover.  It validates that the **FAT structure**
this firmware generates is acceptable to the Linux vfat driver and to Apple's stricter
offline checker.  It says nothing about USB enumeration, composite descriptor handling or
mass storage transport on Linux, all of which still need real hardware.

**Windows remains genuinely untested**, and Windows FastFAT is the fussiest of the three
about BPB fields.  It is the most likely of the remaining hosts to reject something
structural, so it should not be treated as a formality.

Unrelated cosmetic note: the seeded file's date renders as 2026-01-01 on Linux and
2025-12-31 on macOS.  FAT timestamps carry no timezone and are interpreted as local time,
so this is expected and not a defect.

### Still outstanding for Stage 1 sign-off

* **Concurrent load.**  HCOM and mass storage have each been exercised, but not at the same
  time.  Full speed USB has a fixed bandwidth budget and the SCSI daemon runs at
  `CONFIG_USBMSC_SCSI_PRIO` 128, so a bulk-heavy transfer on one interface starving or
  stalling the other is plausible and untested.  Copying a file off the drive while the
  runtime logs over HCOM is the case to try, and it is exactly the load Stage 3 creates.
  This is the last item that can be closed on macOS.
* Windows: mount, read, write, plus a descriptor capture.  Blocked on hardware.
* Linux: USB level mount, read, write, plus a descriptor capture.  Blocked on hardware;
  the FAT structure itself is already validated against the Linux vfat driver above.

OTA is deliberately **not** a Stage 1 criterion.  It is not fully tested in the rebase
independently of this work, so gating a USB change on it would conflate a pre-existing gap
with a regression.

The honest consequence is that the effect of the descriptor change on OTA is unknown.  When
OTA does get exercised in the rebase, the composite device is one of the variables in play,
so a failure there should be checked against a non-composite build before it is attributed
to either cause.  Stage 4 has a related dependency, noted against its OTA task.

### Sequencing note

Deferring the Windows and Linux work is not purely a concession to hardware availability.
Stage 2 has to set `CONFIG_USBMSC_REMOVABLE=y`, which changes INQUIRY, enables
`SCSI_CMD_STARTSTOPUNIT` and alters how often the host polls for media.  Any
cross-platform result gathered now would have to be gathered again afterwards.

Doing it once, after Stage 2 flips that flag, is the more efficient order.  The risk
accepted in the meantime is that Stage 3 gets built against a single host FAT driver, so a
structural objection from Windows would surface later and cost more to fix than it would
now.  Given the FAT structure already passes Apple's checker and the Linux vfat driver,
that risk is reduced but not eliminated.

### A trap worth recording

Do **not** run `make savedefconfig` in this tree as a normalization step.  It rewrites
`.config` as a side effect, and in this configuration it silently enabled NSH, PROCFS,
USART6, MKRD and readline, adding about 18 KB of kernel flash to two intermediate builds
before it was noticed.  The committed `defconfig` was unaffected, and a `distclean` plus
`configure.sh` restored the correct configuration.

It also reorders the whole file and prunes symbols that are implied by others, so its
output is not a drop-in replacement for the hand-maintained `defconfig` in this
repository.  To check that the `defconfig` still reproduces a build configuration, diff a
fresh `configure.sh` result against the working `.config` instead.

### Actual size

About 80 lines of config and Kconfig, 500 lines of new C, and a 3 line linker
change.  Kernel flash cost 808 bytes; `ksram` cost nothing, the 64 KB RAM disk
buffer having gone to otherwise unused DTCM.

## Stage 2 - Storage mode control plane - IMPLEMENTED

Status: **implemented and confirmed working on hardware (macOS).**  Mode transitions and
eject both verified.  Host behaviour while the drive is empty is still unverified; see
below.

### The design changed, and it got much smaller

The plan assumed storage mode would be a mode of its own: new HCOM commands, a new
battery backed bit, a four state machine, and a decision about what triggers it.

It is instead **derived from whether the .NET runtime is enabled**:

    runtime disabled  ->  storage mode active
    runtime enabled   ->  storage mode inactive

That is the condition under which the runtime is already guaranteed not to be writing to
`/meadow0`, so it needs no separate concept.  What it eliminated:

* **No protocol changes.**  `HCOM_MDOW_REQUEST_MONO_DISABLE` and
  `HCOM_MDOW_REQUEST_MONO_ENABLE` already exist, already set and clear
  `HCOM_BBREG_USER_RQST_MONO_ENABLE_BIT`, and already call
  `hcom_via_nx_host_restart_meadow()` afterwards.  So `meadow mono disable` enters
  storage mode and `meadow mono enable` leaves it, each by way of the restart those
  commands already perform.  Nothing was added to `hcom_protocol.h`.
* **No new battery backed bit**, and no new `/dev/upd` ioctl.
* **No four state machine.**  Because both transitions restart the board, there is no
  Entering or Leaving state to model.  The mode is a single boolean, fixed for a boot.
* **No trigger decision.**  There is no separate trigger to design.

This also answers the concern that made a physical gesture look necessary: a wedged
runtime cannot serve HCOM, but `mono disable` is handled by hcom rather than by the
runtime, and after the restart the runtime does not start at all.  Storage mode is
therefore reachable *precisely* when the runtime is broken, which is when it is most
wanted.

### What was changed

| File | Change |
|------|--------|
| `F7MeadowOS/include/meadow/meadow_storage_mode.h` | New.  The `meadow_storage_mode_active()` predicate and the reasoning behind it. |
| `F7MeadowOS/meadow_os/meadow_storage_mode.c` | New.  Latches the runtime-enable bit once per boot. |
| `src/Make.defs` | Registers the new source file. |
| `src/stm32_usbmsc.h` | LUN path is now `/dev/meadowmsc0`; documents that interface presence and media presence are separate questions. |
| `src/stm32_usbmsc.c` | `ramdisk_register()` replaced with a `block_operations` driver we own. |
| `src/stm32_composite.c` | Binds the LUN only in storage mode. |
| `configs/meadow_os/defconfig` | `CONFIG_USBMSC_REMOVABLE=y`. |
| `nuttx/drivers/usbdev/usbmsc_scsi.c` | TEST UNIT READY now reports no media for an unbound LUN; START STOP UNIT now implements eject and load.  See below. |
| `nuttx/drivers/usbdev/usbmsc.h` | A field to park the medium while ejected. |
| `nuttx/include/nuttx/scsi.h` | The standard `LOEJ` and `START` bit names. |

### The mode must be latched, and that is a correctness requirement

`meadow_storage_mode_active()` reads the battery backed bit once and caches it.  That is
not an optimization.  `hcom_mono_ctrl_disable_mono()` sets the bit and only *afterwards*
restarts the board, so between those two points the bit reads "disabled" while the
runtime is still running and still holding `/meadow0`.  Reading it live would briefly
report storage mode at exactly the wrong moment.

The latch is taken during `board_init_usbdev()`, which runs from
`board_late_initialize()` after `stm32_pwr_initbkp(true)` has made the backup domain
accessible and well before the runtime can start.  So it reports what is true for this
boot rather than what is pending for the next one.

### Media presence is expressed by binding, not by geometry

The plan said to report media absence from the block driver's `geometry()` with
`geo_available = false`.  That cannot work: `usbmsc_bindlun()` **rejects** a LUN whose
geometry reports unavailable (`usbmsc.c`, the `!geo.geo_available` test), and geometry is
read exactly once at bind time and never revisited.  There is no way to signal media
presence dynamically through it.

What works instead is to leave the LUN **unbound**.  The SCSI layer checks
`lun->inode` on every data command and answers `SCSI_KCQNR_MEDIANOTPRESENT`, while
INQUIRY still succeeds because `priv->lun` itself is non-NULL.  The host sees a card
reader with nothing in it.

The valuable consequence is that **the USB descriptors are identical in both modes**.
The mass storage interface is always advertised; only the medium comes and goes.  Had
media presence been done by omitting the interface, the device would have changed shape
between boots under one VID/PID/serial, which is exactly the kind of thing Windows
driver-binding caches handle badly.

`CONFIG_USBMSC_REMOVABLE=y` is required for this, as anticipated.  It sets the INQUIRY
`RMB` bit and enables the `SCSI_CMD_STARTSTOPUNIT` and `PREVENT ALLOW MEDIUM REMOVAL`
paths that Stage 1 left rejected.

### One upstream file was modified

`usbmsc_cmdtestunitready()` did not check media presence, so TEST UNIT READY reported
ready on an unbound LUN and only READ CAPACITY then failed with MEDIUM NOT PRESENT.
That is incoherent per SCSI, and it leads hosts that poll TEST UNIT READY to retry the
capacity query rather than concluding the drive is empty.

The fix is six lines, guarded by `CONFIG_USBMSC_REMOVABLE` because the condition cannot
arise for fixed media.  It is a candidate upstream contribution rather than a local
workaround.

### Writer gating deferred to Stage 3, deliberately

Task 4 of this stage was to gate every board-side writer to `/meadow0`.  It has not been
done, because in Stage 2 the mass storage LUN is still the RAM disk and has no
relationship to `/meadow0`.  There is nothing yet to conflict over, so any gating written
now would be unverifiable.

The runtime is the writer that matters most and it is handled structurally: in storage
mode it does not run.  The remaining writers, which do run with the runtime disabled, are
inventoried here so Stage 3 does not have to rediscover them:

| Writer | File | Target |
|--------|------|--------|
| Meadow log | `F7MeadowOS/misc/meadow_logging.c` | `/meadow0/meadow.log` - the continuous one |
| Crash report | `F7MeadowOS/meadow_os/meadow_os_fault_handling.c` | `CRASH_DIR` |
| ESP core dump | `F7MeadowOS/espcp/espcp_core_dump.c` | `CRASH_DIR` |
| OS persistent data | `F7MeadowOS/meadow_os/meadow_os_info.c` | `MEADOW_SYSTEM_OS_PERSISTENT_DATA_FILE` |
| Config manager | `F7MeadowOS/hcom_nx/hcom_nx_config_manager.c` | the config YAMLs |
| HCOM file operations | `apps/system/hcom/file/` | anywhere under `/meadow0`, from user space |

That last row is the awkward one and it is a real design question for Stage 3, not an
oversight.  `meadow file write` still works with the runtime disabled, which is exactly
how an app gets deployed.  So the tool used to deploy an app and the mode used to expose
the file system will both be available at the same time, and writing through one while
the host has the other mounted corrupts both views.  Stage 3 has to decide whether HCOM
file writes are refused during storage mode, at the cost of breaking CLI deployment
while a host is attached.

### Behaviour change to expect on the next flash

The drive **no longer appears during normal operation**.  With the runtime enabled the
host now sees an empty reader, which on macOS means nothing in Finder at all.  Run
`meadow mono disable` and after the restart `MEADOW BOOT` appears with `README.TXT`;
`meadow mono enable` puts it back.  This is intended, not a regression.

### Hardware results (macOS)

Both transitions work:

* `meadow mono disable` -> after the restart, the disk appears.
* `meadow mono enable` -> after the restart, the disk is gone.

That confirms the whole mechanism at once: deriving the mode from the runtime-enable
state, the boot-time latch, the conditional LUN bind as the expression of media presence,
and using the existing HCOM commands as the only control surface.  It also confirms HCOM
itself keeps working across both modes, since those commands are how the transitions were
issued.

**The constant-descriptor design is confirmed.**  With the runtime enabled and no volume
mounted, the device still enumerates the mass storage interface:

    interface num=2 class=8 subclass=6      (mass storage, SCSI transparent)

and `/dev/cu.usbmodem3360317434381` is still present, while `/Volumes` has no
`MEADOW BOOT`.  So the interface set does not change shape between modes; only the medium
comes and goes, exactly as a card reader behaves.  This is the property that avoids
asking Windows driver-binding caches to cope with a device that sometimes has an MSC
interface and sometimes does not.

### Eject was broken by this stage, and is now implemented

Turning `CONFIG_USBMSC_REMOVABLE` on regressed eject, and the regression was found on
hardware: in storage mode, ejecting the volume in Finder made it **remount immediately**.
`diskutil` confirmed the device still advertising `Removable Media: Removable` with the
medium present.

The cause is that NuttX never implemented the command.  With `CONFIG_USBMSC_REMOVABLE`
defined, `usbmsc_cmdstartstopunit()` accepted START STOP UNIT and did nothing at all - the
whole body was inside `#ifndef CONFIG_USBMSC_REMOVABLE`.  So the device claimed removable
media, agreed to eject it, kept it, and the host re-probed and re-mounted.  Stage 1 was
accidentally better here: with the flag off the command was refused outright, and macOS
fell back to a host-side unmount that stuck.

It is now implemented and **confirmed fixed on hardware**: after the fix the volume
stayed unmounted rather than remounting.  The observation window was 15 seconds, which is
short, but it is decisive against the failure that was actually seen - the broken
behaviour remounted immediately, not eventually.

Resetting the board brings the medium back, which is correct: a reset re-runs boot, the
storage mode latch is taken again, the runtime is still disabled, and the LUN is bound
afresh.

`LOEJ` selects a medium change and `START` says which way:

* eject (`LOEJ` set, `START` clear) parks `lun->inode` in a new `lun->ejectedinode`
  field and clears it, so every existing `!lun->inode` test reports media not present
  with no other change;
* load (`LOEJ` and `START` both set) restores it and raises
  `SCSI_KCQUA_NOTREADYTOTRANSITION` so the host re-reads the capacity rather than
  trusting what it cached.

The inode is parked rather than closed deliberately.  `usbmsc_unbindlun()` acquires
`priv->thlock`, which the SCSI worker thread already holds across command dispatch, so
calling it from a command handler would deadlock.  Parking also makes load and eject
symmetric for free.

**This gives Stage 4 something it needed.**  A working eject is the host saying "I am
finished with this medium", which is exactly the ownership handoff signal that safe
two-way access requires.  It was going to have to be invented; it now falls out of
implementing the standard command properly.

Files touched upstream for this: `nuttx/include/nuttx/scsi.h` (the standard `LOEJ` and
`START` bit names, which were missing), `nuttx/drivers/usbdev/usbmsc.h` (the parking
field, guarded by `CONFIG_USBMSC_REMOVABLE`) and
`nuttx/drivers/usbdev/usbmsc_scsi.c`.  Along with the TEST UNIT READY change, these are
candidate upstream contributions rather than local workarounds.

### Ejecting is one-way until reset, by design

Once the host ejects, the practical way to get the medium back is a board reset, because
macOS does not issue a load.  That is coherent for this feature rather than a shortcoming:
storage mode is already scoped to a boot, entering and leaving it are already restarts,
and an eject is the host declaring it is finished with the medium.  Worth knowing rather
than being surprised by.

### Still unverified

* **Host error and retry behaviour while the drive is empty.**  This is the part the
  TEST UNIT READY change was written to address and it remains the most important
  outstanding check for this stage.  It has *not* been confirmed.  An attempt to read the
  macOS unified log returned nothing at all, including for a deliberately broad query,
  which indicates no log access from the environment used rather than an absence of
  errors.  To check it properly, run this in a terminal with the runtime enabled and the
  drive therefore empty:

      log show --last 10m --style compact | grep -iE "usb|scsi|not ready|media"

  What would indicate a problem is repeated capacity queries or not-ready retries rather
  than the host settling after one probe.
* The syslog line `Storage mode ACTIVE/inactive, runtime disabled/enabled` matching
  reality on each boot.
* The load path.  macOS evidently does **not** issue START STOP UNIT with `START` set,
  since the medium did not return on its own and a board reset was needed to get it back.
  So the load half of the implementation is correct but unexercised.  Linux can drive it
  directly with `eject -t /dev/sdX`, which is worth doing whenever a Linux machine is
  available.
* Windows and Linux, still blocked on hardware.  Note that this stage is the one that
  turned `CONFIG_USBMSC_REMOVABLE` on, so it is the point after which cross-platform
  results become worth gathering.

### Actual size

About 60 lines of new Meadow C, 130 lines of block driver, 6 lines upstream, and one
defconfig symbol.  Kernel flash went **down** 40 bytes overall, because dropping
`ramdisk_register()` saved more than the block driver cost; `ksram` grew 16 bytes for the
latch.

## Stage 3 - Read-only synthetic FAT over `/meadow0` - IMPLEMENTED

Status: **implemented and verified on hardware (macOS)**, after one boot-breaking
regression described below.  A multi-cluster file read from a subdirectory compares
identical to the same file fetched over HCOM.

The first deliverable that exposes `/meadow0`.  A host can browse and copy the Meadow
file system natively, read-only.

### What was built

`F7MeadowOS/hcom_nx/create_fs/hcom_nx_fatshim.c`, about 1270 lines, registered as the
block driver behind `/dev/meadowmsc0`.  Nothing is stored: each 512 byte sector read is
answered by computing what a FAT16 volume containing those files would have held at that
offset.

* **Geometry** is fixed by configuration, not derived from the partition: 512 byte
  sectors, 4 KB clusters, 6900 data clusters, two 27 sector FATs, a 512 entry root
  directory, 55287 sectors in total, so 27.0 MB.  It has to be fixed, because
  `usbmsc_bindlun()` reads the geometry at bind time and `board_init_usbdev()` runs some
  120 lines *before* `hcom_nx_setup_mgr()` mounts the file system.  Compile-time checks
  keep the cluster count inside the 4085 to 65524 window that makes a host read the
  volume as FAT16; outside it a host silently interprets the FAT at the wrong width.
* **The model** is built lazily, on the first sector read, which is necessarily long
  after the mount.  A depth-first walk assigns each file and each directory a contiguous
  cluster run.
* **Subdirectories** are supported, as decision 5 requires: `update/`, `rollback/`,
  `crash/` and `system/` all appear, nested, with synthesized `.` and `..`.
* **Long file names** are generated, with checksummed 8.3 aliases and `~n`
  disambiguation.  Not optional: `meadow.config.yaml` and `Meadow.dll` cannot be
  represented by a short entry, the latter only because of its case.
* **The cached file handle** matters more than it looks.  The SCSI layer reads one sector
  per call, so without it every 512 bytes would re-open and re-seek LittleFS.

### Read-only is enforced by NuttX, not by us

The block driver deliberately has **no write method**.  `usbmsc_bindlun()` turns that
into `lun->readonly`, which sets the MODE SENSE write protect bit and refuses write
commands in the SCSI layer before they ever reach the driver.

This was worth getting right rather than assuming.  `geo_writeenabled` in the geometry
turns out to have **no effect at all**: `lun->readonly` comes from the `readonly`
argument to `usbmsc_bindlun()`, or from the absence of a write method, and nothing reads
the geometry flag.  A first cut that only set `geo_writeenabled = false` and returned
`-EROFS` from a write method would have advertised the volume as writable and then failed
every write.  The visible symptom would have been macOS trying to create `.fseventsd` on
mount and retrying against the refusals; the off-target test caught it by doing exactly
that to a read-write disk image.

### Validated off-target before flashing

The synthesis engine touches no file system, so it was compiled and run on the host.
`FATSHIM_HOST_TEST` swaps a small stub header in for the NuttX surface; nothing else in
the file differs between the two builds.  The harness simulates a `/meadow0` chosen to hit
the awkward cases - a 70000 byte file spanning 18 clusters, a file exactly one cluster
long, a zero-length file, three levels of nesting, two long names sharing a prefix so the
`~n` aliases have to disambiguate - then writes all 55287 sectors to an image.

| Check | Result |
|-------|--------|
| `fsck_msdos -n` (Apple's checker) | Phases 1-3 clean, exit 0, "20 files, 6849 clusters free" - exactly 6900 minus the 51 used |
| Linux kernel `vfat`, loop mounted | Mounts clean; all 20 entries present with correct long names, sizes and nesting to depth 3 |
| macOS `msdosfs` via `hdiutil` | Mounts as `MEADOW0`; same listing |
| **File contents** | Every file's bytes checked by md5 against an independently generated expectation.  15 of 15 match. |

That last row is the one that matters: it exercises the FAT16 chains, the cluster runs and
the data region mapping end to end, through a real host driver rather than through my own
reader.

What it does not cover is LittleFS itself.  The harness serves file bytes from a table, so
`read_file_bytes()` is the one function still unexercised.

### It broke the boot first, and how

The first flash of this stage left the board with no USB at all: no mass storage and, more
to the point, no `/dev/ttyACM0`, so no HCOM either.  A debugger showed no fault or
exception, which was the clue - the board was running normally and had simply failed to
create a USB device.

The cause was making the block driver read-only by giving it **no write method**.
`find_blockdriver()` refuses that:

    if (!inode->u.i_bops || !inode->u.i_bops->read ||
        (!inode->u.i_bops->write && (mountflags & MS_RDONLY) == 0))
      {
        ret = -EACCES;

and `usbmsc_bindlun()` calls `open_blockdriver(path, 0, ...)` with flags of zero, never
`MS_RDONLY`.  So the LUN could not be opened at all: `bindlun` returned `-EACCES`,
`board_mscclassobject()` returned the error, `composite_initialize()` failed, and
`board_composite0_connect()` returned NULL.  Every step a clean error return, which is
exactly why nothing faulted.

Worth noting what this says about the Stage 1 graceful-degradation path.  It was built so
that a storage fault could not cost the CDC/ACM interface, and it did not help here,
because it checks availability *before* deciding to advertise the interface while this
failure happens during the bind afterwards.  A more thorough version would tolerate a
late bind failure too.

The fix is that the write method exists and returns `-EROFS`, and read-only comes from the
**`readonly` argument to `usbmsc_bindlun()`**, which is what sets `lun->readonly` and
raises the MODE SENSE write protect bit.  The earlier reasoning had this backwards: keeping
a write method does not advertise the volume as writable, the argument does.

An upstream observation rather than something needed here: `usbmsc_bindlun()` arguably
ought to pass `MS_RDONLY` to `open_blockdriver()` when its caller asks for a read-only
LUN, which would let a genuinely write-less driver be bound.

### A second failure, found by inspection rather than by flashing

`CONFIG_USBMSC_SCSI_STACKSIZE` was at its 2048 byte default, and the SCSI daemon is the
thread that now runs `model_scan_dir()`.  That is recursive with a 256 byte path buffer per
frame, roughly 1968 bytes at the maximum depth, before the read and synthesis frames on
top.  It would have overflowed on the first host read.  Raised to 8192.

### Writer gating, carried over from Stage 2

The runtime is handled structurally, since storage mode means it is not running.  Of the
remaining writers, the log is the only one that fires continuously, and it is now gated:
`meadow_logging_write_to_file()` returns early when
`meadow_storage_mode_writes_permitted()` says no, which it does for any path under
`/meadow0` while storage mode is active.

This is not a limitation dressed up as a feature.  The host's view is a snapshot - the
cluster mapping was computed once, on first read - and mass storage has no way to tell an
already-mounted host that anything changed.  Freezing the file system is the point, and it
is what you want anyway when the reason for entering storage mode was to copy the log off.
Lines produced while blocked still reach syslog; they just do not reach the file.

Still ungated, and each only fires occasionally: crash reports, ESP core dumps, OS
persistent data, and the config manager.  They were left alone rather than gated blindly,
because each needs a decision about what should happen to the data instead of being
written, and a crash report is not something to discard casually.

**The HCOM file write question from Stage 2 is still open and still needs an answer.**
`meadow file write` works with the runtime disabled - that is how an app is deployed - so
deployment and storage mode are available at the same time, and a write through one while
a host holds the other leaves the host reading stale cluster runs.  Nothing currently
prevents it.

### Memory

The model tables, 16 KB of entries and a 12 KB name pool, are placed in DTCM by the
`.dtcm` section Stage 1 added.  A first cut left them in `.bss`, which pushed `ksram` from
79100 to 107852 bytes, 82 percent of the region.  Moving them put `ksram` back to 79180
and left DTCM at 28 KB of 128 KB.  Dropping the Stage 1 RAM disk is what freed the space,
so the two never compete; the Kconfig makes them mutually exclusive.

Limits are 512 entries and 12 KB of names, and both are reported rather than silently
truncating: exceeding them logs and omits the excess.

### Hardware results (macOS)

After the fix the board boots, `meadow device info` returns over HCOM, and the Meadow file
system is visible in Finder.

The host's own view of the volume confirms the important properties:

| Reported by macOS | Value | What it confirms |
|-------------------|-------|------------------|
| File system personality | `MS-DOS FAT16` | The cluster count landed in the FAT16 window, so the FAT is being read at the right entry width |
| Disk size | 28306944 bytes, exactly 55287 512-byte units | Exactly `FS_TOTALSECTORS`; the geometry is being taken as given |
| Allocation block size | 4096 bytes | The 4 KB cluster size |
| Media read-only | Yes | The MODE SENSE write protect bit reached the host |
| Volume read-only | Yes, read-only mount flag set | macOS mounted it read-only, so it will not try to write `.fseventsd` |
| Removable media | Removable | `CONFIG_USBMSC_REMOVABLE` |
| Volume used space | 928 sectors, 116 clusters | **The model was built on target.**  This is real content from the real LittleFS volume, not a fixed number |

That last row is the one that matters most: macOS computed used and free space by reading
the synthesized FAT, and it arrived at a figure derived from an actual walk of `/meadow0`.
The synthesis works against a real file system, not just the simulated one.

Note the mount is served by `fskit` rather than the older `msdosfs` kext, so this is a
third macOS FAT implementation beyond the two the off-target tests used.

A configuration YAML opened from the mounted volume in a text editor reads correctly.
That closes the LittleFS read path, which was the one thing nothing had exercised: the
directory entry pointed at the right cluster, and `read_file_bytes()` opened the real file,
seeked and returned the right bytes.

### Copies arriving locked on the host

Files copied off the volume in Finder landed on the host locked, needing to be unlocked
one at a time.

The cause was setting `ATTR_READONLY` on every file's directory entry, on the reasoning
that the whole view is read-only.  That reasoning was wrong.  The attribute describes the
*file*, not the volume, so it travels with a copy; the volume being read-only is
communicated entirely separately, by `lun->readonly` raising the MODE SENSE write protect
bit.  Directory entry attributes play no part in it.

Files now carry only `ATTR_ARCHIVE`.  The Linux `vfat` driver confirms the change, listing
entries as `-rwxr-xr-x` where they were `-r-xr-xr-x`, while the mount itself is still `ro`
and file contents are byte identical to before.  Nothing was given up.

### Correctness confirmed end to end

A 27 KB file from the `crash/` subdirectory was copied out twice, once through Finder and
once with the Meadow CLI, and the two files compare identical.

That single test closes both of the questions that were outstanding:

* **Multi-cluster chain following.**  At the 4 KB cluster size a 27 KB file occupies seven
  clusters, so the synthesized FAT had to link them correctly and each of the roughly 54
  sector reads had to seek to the matching offset inside the file.  The configuration YAML
  checked earlier was a few hundred bytes and only ever exercised the single-cluster path.
* **Subdirectory access.**  Reading from inside `crash/` exercises directory clusters in
  the data region, which are synthesized by a different path from the root directory's
  fixed area.

Comparing the Finder copy against the CLI copy is also the strongest available form of the
check, rather than a convenience: the two routes are independent.  Finder reads through the
synthesized FAT view, the CLI reads through HCOM and LittleFS directly, and they agree.

Long file names display correctly in Finder, which confirms the long name generation on
target: the sequence numbering, the reverse order the slots are stored in, and the
checksum tying each slot set to its 8.3 alias.  A mismatch in any of those would have made
the host discard the long names and show the `~n` aliases instead, which is the usual way
long file names fail quietly.

Copies now arrive unlocked, confirmed by being able to delete and rename them on the host
with no error dialog.  That closes the `ATTR_READONLY` regression above.

### Stage 3 is verified on macOS

Everything this stage set out to do is confirmed on hardware: the volume enumerates and
mounts as read-only FAT16, long file names display correctly, subdirectories are browsable,
a multi-cluster file read from inside one compares byte identical to the same file fetched
over HCOM, and copies arrive unlocked.  HCOM itself is unaffected throughout - `device
info` and `file read` both work.

### Still to confirm

* Throughput.  LittleFS on QSPI, read one sector at a time through SCSI, has never been
  measured.  Worth timing a large copy before anyone relies on this for bulk retrieval.
* Windows and Linux, still blocked on hardware.
* The `fatshim:` syslog lines reporting entry and cluster counts, plus any model-full or
  out-of-clusters warnings on a populated device.
* Throughput on a large copy.  The cached handle makes sequential reads cheap, but
  LittleFS on QSPI through one-sector-at-a-time SCSI has never been measured.

macOS privacy protection blocked the environment used here from listing a removable
volume, so the listing and content checks need a normal terminal or Finder.

### Actual size

About 1270 lines in the fatshim, 40 lines of storage-mode predicate, a one-line gate in
the logger, and the Kconfig and wiring.  Kernel flash grew about 2 KB; `ksram` grew 80
bytes and DTCM took the 28 KB of tables.

## Stage 4 - Write and delete path

Accept host writes and translate them into LittleFS operations. This is the hard stage
and where the schedule risk lives.

### Why it is hard

Host writes arrive in an order chosen by the host's FAT driver. Data clusters may land
before the directory entry that names them; the FAT chain may be written last; writes may
be partial. The driver cannot commit a file until it holds both a directory entry (name
and size) and a resolvable cluster chain.

### Tasks

1. Write buffering and a deferred-commit state machine: accumulate sectors, commit to
   LittleFS only once the directory entry and cluster chain agree, with a settling timer
   to decide the host has finished.
2. Directory-entry diffing to detect create, overwrite, rename (entry rewrite), delete
   (`0xE5` marker) and truncate.
3. FAT-sector write interpretation to recover cluster chains for new files.
4. Subdirectory mutation: create and delete directories, move entries between them.
5. Host-junk write filtering. Discard rather than commit: `.DS_Store`, `._*`,
   `.Spotlight-V100`, `.fseventsd`, `.Trashes`, `desktop.ini`,
   `System Volume Information`, `$RECYCLE.BIN`, `indexerVolumeGuid`. The real list is
   discovered empirically per host OS.
6. Flip `geo_writeenabled` true; rebuild the model after each commit batch.
7. **Recovery story.** Decision 5 grants unrestricted access, so a host can delete
   `Meadow.dll` or corrupt `meadow.config.yaml`. Define what the device does on next
   boot with a missing or invalid application, and confirm the HCOM re-upload path still
   works from that state. The device must always be recoverable over USB serial.
8. **OTA interaction.** A host writing directly into `update/` or `rollback/` bypasses
   the OTA validation and state machine entirely. Decide whether those directories are
   writable from the host, and if so, how the OTA state machine detects and validates
   externally-placed files.

   This task has a prerequisite outside its own scope: OTA is not yet fully tested in the
   rebase. Granting the host unrestricted write access to the OTA staging directories,
   which decision 5 implies, is not something to build on top of a path whose own
   behaviour is still unconfirmed. Expect to need OTA verified first, or to carve those
   directories out as the one exception to full filesystem access.

### Exit criteria

* Drag a file onto the drive from macOS, Windows and Linux; it appears intact in
  `/meadow0`, checksum-verified over HCOM.
* Delete, rename, overwrite, and directory create/delete all behave correctly.
* Host filesystem metadata noise never appears as real files in `/meadow0`.
* Power loss mid-write leaves LittleFS consistent - a partial file is absent or
  truncated, never a corrupted volume.
* A device whose application files were deleted by the host recovers fully over HCOM.

### Estimated size

~500-700 lines on top of Stage 3. Subdirectory mutation and the OTA question push this
above the original estimate.

---

## Stage 5 - Hardening and integration

1. Performance: measure throughput, tune the file-handle cache, consider a small sector
   cache for FAT and directory regions.
2. Interaction review: OTA (`update/`, `rollback/`), crash-dump path, `RenewFileSys`, and
   the `--unittests` configurations.
3. Soak testing: repeated attach / detach / mode-transition cycles with the runtime
   active, checking for LittleFS damage.
4. Kernel test coverage under `F7MeadowOS/kerneltests/`, following the pattern of
   `sdcard_tests.c`.
5. Documentation: update `Documentation/Architecture.md`, `Documentation/Userspace.md`,
   and `Documentation/Kernelspace.md`.
6. Host-side: Meadow CLI awareness of the new composite descriptors and PID, plus a CLI
   verb to enter and leave storage mode.

---

## Effort Summary

| Stage | New/changed kernel C | Difficulty | Main risk |
|-------|---------------------|------------|-----------|
| 1 - Composite bring-up (RAM disk) | ~300 lines (done) | Low | Host driver binding, Meadow CLI enumeration |
| 2 - Storage mode control plane | ~190 lines (done) | Low, once derived from the runtime state | Host behaviour while the drive is empty |
| 3 - Read-only synthetic FAT | ~1310 lines (done) | Moderate-high | LittleFS read path, throughput |
| 4 - Write and delete path | ~500-700 lines | **High** | Out-of-order writes, OTA bypass, recoverability |
| 5 - Hardening | Varies | Low-moderate | RAM budget, OTA interaction |

Total for full read-write: roughly 1600-2000 lines of new kernel C. That is above the
initial estimate, driven by decision 5 - full filesystem access makes subdirectory
support mandatory in both the read and write paths, and it adds the recovery and OTA
questions to Stage 4.

The read-only milestone (Stages 1-3) is roughly 1100-1300 lines and carries almost none
of the corruption risk.

## Recommended Sequencing

Stages 1 through 3 in order, delivering a read-only drive as the first shippable
milestone. Stage 1 exists purely to isolate the descriptor risk: if host driver binding
or Meadow CLI enumeration is a problem, discovering that against a RAM disk is far
cheaper than discovering it tangled up with a new block driver.

Stage 2 before Stage 3 because storage mode is a prerequisite for the drive being usable
at all.  In the event it cost far less than budgeted, because deriving the mode from the
runtime-enable state reused a command and a restart that already existed.  The writer
quiescing that was expected to make Stage 2 large moved to Stage 3, where the medium is
actually `/meadow0` and the gating can be verified.

Stage 4 only once Stage 3 is clean on all three hosts.

## Remaining Open Item

**Decision 4 - USB PID allocation.** Worth noting this is probably less blocking than it
appears: `0x2E6A` is already the Wilderness Labs vendor ID in the shipping defconfig,
with `0x0001` in use for the current CDC/ACM device. Assigning a second product ID under
a VID you already control is an internal decision, not an external registration - so
this can be settled in-house whenever convenient.

What it does gate is the host-side story: Meadow CLI device enumeration, any udev rules,
and Windows driver binding all key off VID/PID.

The tree currently carries the provisional PID `0x0002`, set as
`CONFIG_COMPOSITE_PRODUCTID` in the `meadow_os` defconfig, and Meadow CLI discovers the
board on it without any host-side change.  Confirm it or pick a different number before
this ships; changing it later means re-testing host enumeration on every platform.

## Validation Note

This work cannot be validated without hardware and real host USB stacks. Stage 4 in
particular is a category of code where "compiles and looks correct" is worth much less
than usual - the failure modes only surface by attaching a real Mac and Windows machine
and observing what they actually write. Plan on an iterate-and-observe loop with
`usbmon`, `ioreg` and syslog capture for Stages 3 and 4.

Two things partially substitute for hardware and are worth reusing in Stage 3, since the
synthetic FAT layout can be checked without any USB involvement at all:

* Build the image bytes with an independent implementation of the same layout, then parse
  them back with a *separate* reader.  This is what caught that the seeded `README.TXT`
  spans two clusters rather than one.
* Validate the result with `fsck_msdos -n` and by loop mounting it under the Linux `vfat`
  driver in a privileged container.  Both accepted the Stage 1 layout, and both are
  stricter than a permissive host mount.

Neither says anything about USB enumeration or mass storage transport, only about whether
the file system this firmware generates is well formed.
