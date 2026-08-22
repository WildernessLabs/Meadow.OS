# USB Composite: Serial + Mass Storage

Plan of work to expose `/meadow0` to the host computer as a USB drive alongside the
existing USB serial port, without giving up `/dev/ttyACM0`.

## Status

| Stage | State |
|-------|-------|
| 1 - Composite bring-up (RAM disk LUN) | **Done.** Verified on macOS. |
| 2 - Storage mode control plane | **Done.** Verified on macOS. |
| 3 - Read-only synthetic FAT over `/meadow0` | **Done.** Verified on macOS, contents byte-identical to HCOM. |
| 4 - Write and delete path | **Done.** Add, delete and rename all verified on macOS. |
| 5 - Hardening and integration | Not started. |
| 6 - Removing the staging buffer | Measurements 1 and 2 done on macOS: streaming is viable.  Where to pick this up. |

A composite CDC/ACM plus mass storage device enumerates, `/dev/ttyACM0` and HCOM are
unaffected, and a medium appears and disappears with the runtime-enable state.  That
medium is now a read-only FAT16 view of `/meadow0`, synthesized on demand.  Stages 1 and 2
are confirmed on hardware; Stage 3 is validated off-target but has not been run on a
board.

Verified on macOS: enumeration, descriptors, mount, file data reads, host writes, eject,
mode transitions in both directions, `meadow file write` and read over HCOM.  Windows and
Linux USB testing is deferred on hardware availability; the FAT structure alone has been
checked against the Linux `vfat` driver and Apple's `fsck_msdos`.

Open items are listed per stage.  See **Picking this up again** below for the state of the
tree, what is left, and the traps.

Stage 6 is the live question: a host's copy dialog finishes at USB speed and the data then
takes up to a minute to reach flash, with nothing on the host to say so.  Investigating that
turned up a hazard that is not the staging buffer and is still present - a commit holds
`_lock` for its whole duration while one SCSI thread serves every command, so the device goes
silent for around 52 seconds on a 3 MB file.  That is the most likely cause of the original
Finder crash, and it has to be fixed before the buffer can go.  Instrumentation to settle the
remaining unknowns is built and off by default.  The first measurement is in: macOS writes
the complete FAT chain and the directory entry before any file data, and sends the data as one
contiguous ascending run, so streaming has everything it needs from the first sector.  A host
also tolerates a single command blocking for at least five seconds, against the seventy
milliseconds a cluster write costs, so the design has ample margin.

## Picking this up again

The work is on hold with stages 1 to 4 complete and verified on macOS.  Nothing is
half-finished: the tree builds clean, and every behaviour listed as done was confirmed on a
board rather than merely compiled.

### What exists

    F7MeadowOS/hcom_nx/create_fs/hcom_nx_fatshim.c   the synthesized FAT view, both directions
    F7MeadowOS/meadow_os/meadow_storage_mode.c       storage mode, derived from the runtime state
    Testing/OTG-Harness/                             off-target harnesses, see its README
    nuttx/boards/.../src/stm32_composite.c           CDC/ACM plus mass storage
    nuttx/boards/.../src/stm32_usbmsc.c              the bring-up RAM disk, still selectable
    nuttx/drivers/usbdev/usbmsc*, nuttx/include/nuttx/scsi.h   local NuttX changes

Build and flash as usual, `./build.sh` then `./flash.sh`.  Config lives under
`MEADOW_USBMSC_*` in the board Kconfig; the LUN backing is a choice between the SD card, the
bring-up RAM disk and the `/meadow0` view, and `MEADOW_USBMSC_FATSHIM_WRITE` turns the write
direction on and off.  Turning it off is the quickest way to tell a write problem from a
read one.

### How to use it

`meadow mono disable` enters storage mode and the drive appears; `meadow mono enable` leaves
it.  Both restart the board, which is what makes the mode safe: the runtime never runs while
a host holds the file system.  There is no separate storage-mode command and there does not
need to be.

### What is left

Stage 5 is untouched, and stage 6 has instrumentation but no measurements.  Beyond that, four items on stage 4 are unconfirmed and none is
blocking: a byte-exact `cmp` of a copied file, the HCOM write refusal, behaviour when a copy
is interrupted, and the `host asked for a flush` line, which probably will not appear because
the two second timer usually wins.

Windows and Linux have never been tested at the USB level, only the FAT layout.  That is the
largest gap by far, and stage 2 turning on `CONFIG_USBMSC_REMOVABLE` is why it was worth
deferring rather than doing twice.

### What to be careful of

* **Every defect that reached hardware lived in an integration, not in the synthesis.**  The
  boot regression was `find_blockdriver` refusing a driver with no write method; the double
  write was a commit that did not update the model it diffs against; the rename rewrite was a
  discriminator that could not tell "never staged" from "not staged this round".  The
  off-target harnesses found none of these and structurally cannot.  Build, flash, read the
  console.
* **The console is the instrument.**  `fatshim:` lines report the model, each flush with its
  byte count, and every applied change.  The flush pair brackets the LittleFS write closely
  enough to measure throughput.
* **Write throughput is erase-bound at about 57 KB/s** and is not a software problem.  See
  the stage 4 notes before trying to optimize it.
* **Do not run `make savedefconfig`.**  It rewrites `.config` as a side effect and silently
  enabled NSH, PROCFS and more, adding 18 KB to two builds before it was noticed.

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

## Stage 4 - Write and delete path - IMPLEMENTED

Status: **implemented and verified on hardware (macOS).**  Add, delete and rename are all
confirmed on a board, including the two cases that first went wrong, and the write path was
also validated off-target against Linux's own FAT driver.

### Decisions taken

| Question | Answer |
|----------|--------|
| How host writes become LittleFS operations | Deferred commit with a settling timer |
| `update/` and `rollback/` writable | Yes, honouring decision 5 with no exceptions |
| `meadow file write` while a host holds the drive | Refused, with a clear error |

On the OTA directories: the risk was raised and the decision reaffirmed, so they are
writable.  To be precise about what that exposes, dropping a file into `update/` does not
itself start an OTA.  What it means is that an unvalidated image can be sitting there when
a later OTA trigger goes looking for one, bypassing the signature and CRC checks the HCOM
route performs.

### The trap in deferred commit

The plan described streaming data to temp files and committing once the host goes quiet.
It does not mention what happens *after* the commit, and that turns out to be the hard
part.

Committing a new file to LittleFS says nothing about which clusters it would occupy if the
view were rebuilt.  Rebuilding would therefore move the file out from under a host that is
still reading from where it put it.  So the shadow state **persists for the rest of the
session and serves reads as well as writes**: the host's own layout stays authoritative
until the mode ends and a fresh view is built from scratch.

That single decision also removes the ordering problem the plan worried about.  Because
every incoming cluster is recorded where it landed, it does not matter what order the host
writes things in.

### How it works

* **The FAT is shadowed whole.**  The first write copies all 27 sectors of the synthesized
  FAT into RAM, and the shadow is authoritative from then on.  Taking a complete copy is
  what removes any need to reason about a half-written FAT.
* **The root directory is shadowed** with a dirty bit per sector.  It needs its own store
  because, unlike subdirectories, it is not part of the data region.
* **Cluster payloads are staged in RAM**, with a cluster-to-slot map beside them.  The
  buffer comes from the user heap via `kumm_malloc()`, which is where the external SDRAM
  lives, and is allocated on the first write rather than up front.  See below.
* **Commit** walks the directories the host now describes, reassembles long names from
  their slot sets, and diffs against the model: creates, replaces, deletes, directory
  creation and removal.
* **Renames are detected rather than copied.**  A rename writes only a directory entry, so
  the file's clusters are never staged.  Treating it as a create would read from staging,
  find nothing, and write zeros.  A new entry whose clusters are absent from staging but
  match an existing model entry is therefore renamed in place.  This is a correctness
  requirement, not an optimization.
* **Host bookkeeping is filtered** - `.fseventsd`, `.Spotlight-V100`, `._*`,
  `System Volume Information` and the rest - so it is never written through.

### Refusing HCOM writes, but not as literally as asked

The decision was to refuse `meadow file write` during storage mode.  Implemented
literally, that would have broken app deployment: storage mode *is* the runtime being
disabled, and disabling the runtime is how an app is stopped before a new one is deployed.

So the refusal is gated on whether a host is **actually using** the drive, which the kernel
knows because the view is only built when a host reads from it.
`meadow_storage_mode_host_engaged()` reports that, a new `/dev/upd` command
`HCOM_NX_UPD_IS_HOST_USING_FS` exposes it, and `hcom_file_dnld_stm32f7_file_begin()`
refuses with a message naming the cause.  Deployment works when no host is attached, and is
refused when one is.

The flag latches for the boot once set, deliberately: a host that has read once may read
again at any moment, and nothing announces when it stops.

### Validated against a real host FAT driver

Rather than simulating what a host writes, Linux was made to be the host.  The harness
emits the view of a real directory to an image; that image is loop-mounted read-write in a
container where the Linux `vfat` driver adds a file, creates a directory with a file in it,
adds a file inside an existing subdirectory, deletes a file and renames another; then the
sectors that differ are replayed through `write_sector()` **in deliberately shuffled
order** and the commit is run.

Every operation came out right:

    renamed /meadow0/RenameMe.bin to /meadow0/RenamedTo.bin
    wrote /meadow0/NewlyAdded.dat, 6000 bytes
    created directory /meadow0/newdir
    wrote /meadow0/crash/added_in_subdir.log, 500 bytes
    wrote /meadow0/newdir/inside.txt, 900 bytes
    removed /meadow0/DeleteMe.txt

All four created or renamed files are byte identical to what Linux wrote, both deletions
were applied, the staging file was cleaned up, and AddressSanitizer reported no memory
errors.  The shuffled replay is what demonstrates the order independence claimed above.

Notably the one bug this found was in the harness, not the driver: a `#define rename`
expanding inside the function that implemented it, recursing until the stack ran out.

### Application files need no special handling

`Meadow.dll` and the rest of the application are treated as ordinary files, with no
protection and no recovery path in the firmware.  That is a decision rather than a gap: a
host deleting them is covered by user documentation and by the external deployment tooling,
which can put them back.

This also completes decision 5 without exception.  Nothing on the volume is carved out,
including the OTA staging directories.

### Hardware results (macOS)

`meadow mono disable`, the drive appeared, a file of roughly 9.5 KB was copied onto it and
opened from Finder, and `meadow file list` then showed it on `/meadow0` with no
`.fseventsd` alongside it.

Four things follow from that, three of which the off-target test could not reach:

* **The settling timer works.**  Nothing was done to trigger a commit, yet the file reached
  LittleFS, which is what `meadow file list` reads.  The work queue path and the timing
  both ran.
* **The junk filter works, and on the platform it exists for.**  macOS had a writable
  volume and will have tried to create `.fseventsd`; none of it reached `/meadow0`.  Linux
  never exercised this.
* **Multi-cluster staging and commit.**  At 4 KB clusters a 9.5 KB file spans three, so the
  cluster-to-slot map, the chain walk and the reassembly all ran for real rather than for a
  single cluster.
* **Reads after the commit come back correctly.**  Opening the file from Finder is served by
  the shadow, which is the part of the design that exists so a host is not shown a layout
  different from the one it created.

### Two problems the console output exposed

**The commit was not idempotent.**  The log showed the same file written twice, twenty
seconds apart:

    [23.565] fatshim: wrote /meadow0/test_ft232h.py, 9488 bytes
    [43.154] fatshim: wrote /meadow0/test_ft232h.py, 9488 bytes

The commit updated `/meadow0` but never updated the model it diffs against, so a later
commit re-diffed the same shadow against the same stale model and applied everything
again.  Redundant flash writes were the mild consequence.  The serious one was renames: a
repeat pass finds the old path gone, falls through to the create path, finds no staged
clusters, reads through the stale model to a file that no longer exists, and writes a zero
filled file.

Each applied change is now recorded in the model in place, using the clusters the host
chose - nothing is reassigned, so the host still sees its own layout.  Deleted entries are
retired rather than removed, because parents are referenced by index.  A second commit
over an unchanged shadow now reports zero changes, and the renamed file survives byte
identical where it would previously have been zeroed.

There is also a guard so a commit cannot run unless a write has arrived since the last
one, which is what the `commit applied 0 changes` line at boot was.

**Nothing was durable until the settling window expired.**  Host writes land in the RAM
staging buffer, so a copy appears finished immediately while nothing has reached
`/meadow0`.  For a 2.2 MB file the real delay is the 2 second window plus the LittleFS
write, and unplugging inside it lost the copy silently.

SYNCHRONIZE CACHE is the host asking for durability, and it is what an operating system
issues before an eject.  `usbmsc` acknowledged it without telling the block driver, which
makes that promise hollow for anything that buffers.  It now passes `BIOC_FLUSH` down, as
does the eject path, and the driver commits synchronously in response.  Ejecting therefore
means what a user expects it to mean.

The commit runs on the SCSI thread in that case, so the host's command blocks until the
flash write finishes.  That is the intent rather than a cost: it asked for durability.

### The host guards the copy for us

macOS refuses to eject while a copy is in flight.  That is worth recording rather than just
noting: it means the flush-on-eject path only ever runs when nothing is in flight, so there
is no need to defend against an eject arriving mid-transfer.

It also makes the "delete then eject" sequence awkward to perform deliberately, because
with flush-on-sync in place the delete has usually been applied before there is anything
to click.  That is the intended behaviour making the test hard rather than a test failing.

### Verifying by console rather than by timing

The remaining behaviours are easier to confirm from the syslog than by trying to interleave
host actions.  A single copy followed by a single delete should produce:

* `fatshim: host asked for a flush` - the SYNCHRONIZE CACHE path is being exercised, which
  also tells you the build includes it.
* exactly one `fatshim: wrote ...` per copy, with **no repeat twenty seconds later** - the
  idempotency fix holding.
* `fatshim: removed ...` for the delete.
* no `fatshim: commit applied 0 changes` churn between operations - the write-since-commit
  guard doing its job.

### Hardware results: the idempotency fix holds

    [ 1.477] fatshim: 9 entries, 754 clusters used of 6900, 17 root slots
    [ 1.477] Storage mode: a host is using the file system
    [ 1.977] fatshim: staging 16384 KB, 4096 clusters
    [ 4.137] fatshim: commit applied 0 changes
    [74.558] fatshim: wrote /meadow0/Scan1.pdf, 3034136 bytes
    [74.559] fatshim: commit applied 1 changes
    [86.487] fatshim: removed /meadow0/Scan1.pdf
    [86.487] fatshim: commit applied 1 changes

* **One write, not two.**  `Scan1.pdf` was written once with no repeat twenty seconds
  later, which is precisely what the earlier log showed going wrong.  The idempotency fix
  is confirmed on hardware.
* **A 3 MB file.**  3034136 bytes is about 741 clusters, so the cluster-to-slot map, the
  chain walk and the reassembly all ran at scale rather than over a handful of clusters.
* **Delete applied once**, and quickly.
* The model was built from real content and host engagement latched immediately.

The `commit applied 0 changes` at 4.137 is macOS writing something at mount.  A write did
occur, so the write-since-commit guard correctly let a commit run; it simply found no net
change.  Harmless, though it does mean a full directory walk each time a host touches the
volume.

Not shown, and therefore not yet exercised: `fatshim: host asked for a flush`.  That log
sits inside the flush path, so the commit at 74.558 came from the settling timer.  The
flush change post-dates the image this log came from.

### Throughput measured, and it was my own fault

The bracketing log lines gave the number:

    [14.025] fatshim: flushing /meadow0/Scan1.pdf, 3034136 bytes
    [66.231] fatshim: flushing /meadow0/Scan1.pdf complete

3034136 bytes in 52.2 seconds, so about **57 KB/s**.  Dividing it up is what identified the
cause: 5926 writes of 512 bytes each, at roughly 8.8 ms apiece.  That is a per-call cost
rather than a bandwidth limit, and it was self-inflicted - `commit_write_file()` walked a
cluster a sector at a time and issued a `file_write()` for each one.

It now writes a whole 4 KB cluster per call, which is one eighth as many, and does it
without a copy: a staged cluster is already contiguous in the staging buffer, so the
pointer is handed straight to the file system.  Only a cluster the host did not write has
to be gathered sector by sector, which is the uncommon case.

Whether that translates into eight times the throughput depends on how much of the 8.8 ms
is fixed per call and how much scales with the bytes, which the next measurement on
hardware will show.

Worth noting as a further avenue if it is still not enough: `CONFIG_FS_LITTLEFS_*` in the
defconfig carries program, read and cache size factors.  Those affect the on-disk layout,
so changing them on a populated volume is not something to do casually.

### Rename was rewriting the file, and the discriminator was wrong

On hardware a rename produced this:

    [275.289] fatshim: flushing /meadow0/Scan.pdf, 3034136 bytes
    [327.300] fatshim: flushing /meadow0/Scan.pdf complete
    [327.376] fatshim: removed /meadow0/Scan1.pdf

A copy and a delete, 52 seconds, where a rename should have been instant.

The detector asked whether the file's clusters had **ever** been staged, on the reasoning
that a rename rewrites only a directory entry and so leaves the data untouched.  That holds
for a file that existed before the session, and fails for one created during it: `Scan1.pdf`
had been copied on earlier, so its clusters were still sitting in the staging buffer and
the test read them as fresh data.

The question that actually distinguishes the two is whether the clusters were written
**since the last commit**.  A bit per cluster is now set on write and cleared by each
commit, so:

| Case | Clusters written this round? | Treated as |
|------|------------------------------|------------|
| Rename of a file from before the session | no | rename |
| Rename of a file created earlier in the session | no | rename |
| New file that reuses a freed cluster run | yes | new data |

That third row is why "ever staged" cannot simply be inverted: a genuinely new file landing
on clusters a deleted file used to occupy has to be written, not renamed.

Reproduced off-target first, in the sequence that failed: create a 1 MB file, commit, then
rename it, commit again.  Round two now reports `renamed` and touches no data, and the
diff shows why - exactly **one** sector differs between the two images, the directory entry.
The file arrives byte identical under its new name.

### Rename on hardware

    [21.429] fatshim: renamed /meadow0/Scan1.pdf to /meadow0/Scan2.pdf
    [21.430] fatshim: commit applied 1 changes

One millisecond, against 52 seconds of rewriting before the fix.

That first test was the pre-session variant, which would have worked before the fix too,
because clusters never staged returned `-ENOENT` and satisfied the old test.  The variant
that actually failed is the same-session one, and it is now confirmed as well:

    [ 88.717] fatshim: flushing /meadow0/Scan1.pdf, 3034136 bytes
    [140.910] fatshim: flushing /meadow0/Scan1.pdf complete
    [140.911] fatshim: commit applied 1 changes
    [161.776] fatshim: renamed /meadow0/Scan1.pdf to /meadow0/Scan3.pdf
    [161.777] fatshim: commit applied 1 changes

A 3 MB file copied on and committed, then renamed a moment later in the same session: one
millisecond, where before the fix it was a second 52 second rewrite.  Clusters staged in an
earlier round are correctly read as data already on the device rather than as something new.

The renamed file opens correctly afterwards.  For a 3 MB PDF that is worth more than a
glance at a directory listing: a truncated or zero filled file would not open at all, which
is exactly what the earlier non-idempotent commit would have produced.

### Throughput is erase-bound, not software-bound

The cluster-at-a-time change made no measurable difference: 52.008 s against 52.011 s for
the same 3 MB file, both about 57 KB/s.  So the 8.8 ms per 512 byte write was never a
per-call cost, and inferring one from a per-call average was a mistake.

The arithmetic points at the flash instead.  `W25Q*_SECTOR_SHIFT` is 12, so erases are 4 KB,
and 3034136 bytes needs 741 of them.  At a typical 45 ms sector erase that is 33 seconds
before a single byte is programmed; page programming adds about 5 more.  Some 38 of the 52
seconds is the part therefore, with LittleFS metadata plausibly accounting for the rest.

57 KB/s is close to the floor for this flash at a 4 KB erase granularity.  The
cluster-at-a-time write is kept regardless, being fewer calls and no copy, but it was never
going to move a number that erases dominate.

The one lever with real headroom is erase size: W25Q parts support 32 KB and 64 KB block
erase, which are markedly faster per byte, and 64 KB blocks would cut 741 erases to about
47.  `CONFIG_FS_LITTLEFS_BLOCK_SIZE_FACTOR` controls it - but it changes the on-disk
layout, so every existing device's `/meadow0` becomes unreadable.  That is a migration
rather than a tuning change.

### Ejecting during a flush is safe

Ejecting mid-flush completed the write and committed once, with no corruption and no abort.

There was again no `host asked for a flush` line, and that now looks correct rather than
missing: the settling timer fires two seconds after the last host write, so it had already
begun the commit.  The eject's `BIOC_FLUSH` then blocked on the mutex until that finished,
found nothing left to do, and returned quietly.

Which means flush-on-eject is largely redundant against a two second timer; it only matters
if a host ejects inside that window.  The safety property holds either way.

Worth knowing: the eject command blocked for the full 52 seconds of the commit.  macOS
tolerated it, but that is long enough that another host might time the command out.

### Still to confirm

* **The flush path**, on a build that contains it.  A copy should log
  `host asked for a flush` before the write, rather than waiting out the settling window.
* **Byte-exact content.**  Files open and render correctly, including a 3 MB PDF after a
  rename, which rules out truncation and zero filling.  Still short of a `cmp` against the
  copied source, or a `meadow file read` back to the host, which would settle it exactly.
* **The HCOM write refusal.**  A `meadow file write` while a host holds the drive should
  fail with a message naming the cause.
* **Partial writes.**  A copy interrupted, or the board unplugged before the commit, should
  leave nothing behind.  That is the intent; it is not yet evidence.  Note macOS refuses to
  eject mid-copy, which removes the most likely way of provoking this by accident.

### Memory

DTCM holds 76972 bytes of 131072: the model at 28 KB plus the shadow FAT, the root
directory shadow and the cluster-to-slot map.  `ksram` is unchanged at 79244 bytes.
`CONFIG_SCHED_LPWORKSTACKSIZE` was raised to 8192 because the commit runs on that queue.

The 16 MB staging buffer does not appear in the link map at all, being a runtime allocation
from the user heap.

### Staging lives in RAM, taken from the user heap

Staging began as a file, which meant every byte a host wrote reached flash twice, once to
`.usbwrite.tmp` and once to its destination, with the wear and the time that implies.  It
now goes to a `kumm_malloc()` buffer instead, sized by
`CONFIG_MEADOW_USBMSC_FATSHIM_STAGE_KB` and defaulting to 16 MB.

`CONFIG_HEAP2_SIZE` was deliberately **not** shrunk, so nothing is taken from the runtime
permanently.  The chain that makes this work is worth recording, because the comment in
`nuttx/include/nuttx/kmalloc.h` describes a userspace.h trampoline that does not exist in
this tree and is misleading:

* `kumm_malloc()` is `#define`d to `malloc()`.
* The kernel image has its own `malloc`, from `mm/umm_heap/umm_malloc.c`, which calls
  `mm_malloc(USR_HEAP, size)`.
* In a protected build `USR_HEAP` is `(*USERSPACE->us_heap)`, so it resolves through the
  user space blob header to the user heap.
* That heap is where `arm_addregion()` put the 32 MB of SDRAM, via `kumm_addregion()`.

So kernel code genuinely reaches the SDRAM, and the only dependency is that user space is
up - which it is long before a host writes anything, since the buffer is allocated on the
first write rather than at boot.

The memory is held for the rest of the boot once allocated, because the shadow it belongs
to keeps serving reads after the commit.  That is affordable precisely because storage mode
means the runtime is not running.

The cost is a ceiling.  A host writing more than the budget in one settling window gets a
write error, where the file-based version could have used the whole partition.  Nothing is
committed from a partial copy, so the failure is safe rather than corrupting, but 16 MB is
a limit where there was none.  It is a Kconfig value if that turns out to matter.

Removing the staging file also removed the handling that kept it hidden from the view.  A
file left behind by earlier firmware is unlinked when the model is built.

## Stage 5 - Hardening and integration

Not started.  The list below was written before stages 1 to 4 were built, so it is worth
reading against what those stages actually turned out to need.  Two items are now more
important than they looked: the RAM audit is largely answered, since DTCM is at 62 percent
and the staging buffer comes from the user heap at runtime, whereas throughput turned out to
be a flash property rather than something tuning will fix.

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

## Removing the staging buffer, so a host's copy progress means something

Not started beyond instrumentation, which is built and in the tree.  This is the
work that came out of asking why a copy appears instant and then takes a minute.

### The problem as reported

A small file copied to the volume worked.  A large one made Finder exit and the
volume unmount.  RAM staging was introduced in response, and it removed that
failure - but it replaced it with a copy that appears to finish at USB speed and
then spends up to a minute reaching flash with nothing on the host to say so.
CircuitPython and every other mass storage device manages without any of this.

### Why CircuitPython does not need it

Structural, not a matter of tuning.  CircuitPython exposes a real block device
holding a real FAT file system: the host writes sector N, the device writes
sector N.  No interpretation, uniform bounded work per command, and the device
never has to know what a file is.

This shim is doing something categorically harder on two counts.  The FAT is
synthesized, so there is no backing store and a sector write has to be
interpreted at all.  And the destination is LittleFS reached through a *file*
API, so placing a byte needs a path and an offset within that file - which means
the directory entry and the FAT chain, neither of which is guaranteed to have
arrived.  That is the reason staging exists, and removing the buffer does not
make it go away.

### What the investigation actually found, and it was not the cache

`settle_worker()` holds `_lock` across the whole of `commit_apply()`, and
`fatshim_read()`, `fatshim_write()` and `fatshim_ioctl()` all take that same
lock.  There is exactly one SCSI worker thread, and `USBMSC_DRVR_READ` and
`USBMSC_DRVR_WRITE` are called from inside its state machine, so while a commit
runs **no mass storage command of any kind is serviced** - not a read, not a
write, not TEST UNIT READY.  For a 3 MB file that is around 52 seconds of a
device that is not slow but silent.

That explains the original failure exactly, including why it depended on size.
With a small file the commit is quick and the lock is never held long enough to
matter.  With a large one the host has a command outstanding into a 52 second
hole, times it out, resets the device and detaches the volume; Finder exits
because the volume vanished under an open copy.

The consequence that matters for this work: **the hazard is still present.**  The
staging buffer did not fix it, it moved it to a window in which macOS usually has
nothing outstanding.  Stage 4 already recorded the same thing from the other
side - "the eject command blocked for the full 52 seconds of the commit.  macOS
tolerated it, but that is long enough that another host might time the command
out."  Remove the buffer without fixing the lock and the crash returns, which is
most likely what happened the first time.

So the ordering is not a preference: honest progress requires flash writes while
the host is actively transferring, which requires the lock problem solved first.

### The design that looks right

Stream to a provisional file and rename at commit.  Measurement 1 below has since
shown the provisional name is only needed as a fallback on macOS, because the
directory entry and the whole FAT chain arrive before any data does.

* Keep a small table of open write streams, each `{ firstcluster, next expected
  cluster, struct file, bytes written }`.
* A data cluster that continues an open stream is buffered to the 4 KB boundary
  and written straight through to a provisional path, `.usbstream-<cluster>` or
  similar.
* Anything that does not fit the append fast path falls back to the staging
  buffer, which can then be a few hundred KB instead of 16 MB.
* At settle, `commit_apply()` does what it already does for metadata: the
  streamed files are renamed to their real names and their sizes fixed up, and
  renames, deletions and directory creation are unchanged.

Three things recommend it over the alternatives:

* **The name is not needed while data flows.**  Rename in LittleFS is a metadata
  operation, measured at 1 ms in stage 4 against 52 seconds for a rewrite, so the
  provisional name costs essentially nothing.
* **One write per byte, and appends.**  The file-staging version that crashed
  wrote every host byte to flash *twice*, once to `.usbwrite.tmp` and once to the
  destination, and inflated a nearly full 27 MB volume while doing it.  Streaming
  is strictly cheaper than the thing that failed, not more expensive.
* **It dissolves the lock problem instead of shortening it.**  Data writes happen
  on the SCSI thread, which is serialized by construction, so there is no
  cross-thread contention for the expensive part.  Only the short metadata commit
  stays on the work queue.

Throughput does not change and is not meant to: 57 KB/s is erase-bound, as stage
4 established.  What changes is that the host sees 57 KB/s from the first second
instead of seeing an instant copy followed by silence.

### One config lever worth knowing about

`CONFIG_USBMSC_WRMULTIPLE=y` with `CONFIG_USBMSC_NWRREQS=8` makes `usbmsc.c`
size its I/O buffer at `sectorsize * NWRREQS` and hand `fatshim_write()` up to 8
sectors per call - one 4 KB cluster, which is exactly the granularity the
streaming path wants.  It costs 4 KB of the kernel heap.  It is `default n`
upstream, so the multi-sector path is lightly travelled and wants reading before
it is trusted.

### The instrumentation, which is built

Three Kconfig symbols under `MEADOW_USBMSC_FATSHIM_WRITE`, all default off, all
compiling to nothing when off.  Each answers one question that the design above
depends on and that nothing currently knows the answer to.

| Symbol | Question |
|--------|----------|
| `MEADOW_USBMSC_FATSHIM_TRACE` | In what order does a host write? |
| `MEADOW_USBMSC_FATSHIM_STALL_MS` | How long may a single command block? |
| `MEADOW_USBMSC_FATSHIM_LOCKWARN_MS` | How long does a host wait on `_lock`? |

**The trace** records every sector a host writes and dumps it at the top of
`commit_apply()`, before any flash writing - the point at which the round is over
and the console is not yet competing with a commit.  Ascending runs are coalesced
into one record, so a host that writes a file in order costs a handful of records
and one that scatters costs many; the size of the dump is part of the answer
rather than just its contents.  Only the start of each run is timestamped,
because logging as the write happens would perturb the timing being measured.

This is the gating measurement.  Streaming needs the directory entry and the FAT
chain to precede the data, and whether they do is a property of the host that has
never been looked at.

**The stall** blocks on the last sector of every cluster, once per cluster rather
than once per sector, because one flash write per 4 KB cluster is the shape a
streaming write would have.  It finds the host's tolerance for a slow command
without needing the streaming path to exist first: set it to what a cluster write
would cost and see whether the host copes.  This is the number that decides
whether the design is viable at all, and nobody knows it.

**The lock warning** logs when a host command waits longer than the threshold to
acquire `_lock`.  It is the diagnosis above measured from the side that suffers,
and it is cheap enough to leave on: one clock read per command, and silent until
the threshold is crossed.

### Cost, measured

Built both ways with `--wlclean` in between:

| | Off | On (512 runs) | Delta |
|---|---|---|---|
| kflash | 338236 | 339008 | +772 |
| dtcm | 81932 | 88076 | +6144 |
| ksram | 79228 | 79244 | +16 |

The DTCM figure is exactly 512 records of 12 bytes.  DTCM sits at 62.5 percent
with the instrumentation off and 67.2 percent with it on, so the ring can be made
considerably larger if a trace turns out to be dropping runs.

### How to turn it on

`make menuconfig` under the board options works, or directly:

    ./build.sh --configure
    kconfig-tweak --file nuttx/.config --enable MEADOW_USBMSC_FATSHIM_TRACE
    kconfig-tweak --file nuttx/.config --set-val MEADOW_USBMSC_FATSHIM_STALL_MS 40
    kconfig-tweak --file nuttx/.config --set-val MEADOW_USBMSC_FATSHIM_LOCKWARN_MS 250
    ./build.sh --wlclean

**`--wlclean` is required and this is a trap.**  A bare `./build.sh` after a
`.config` change regenerates `include/nuttx/config.h` but does *not* recompile
the Meadow objects, so the build silently keeps the previous setting.  It cost
two builds that reported identical section sizes with the trace supposedly
toggled before the cause was found.  Confirm what was actually built rather than
trusting the config:

    grep FATSHIM nuttx/include/nuttx/config.h
    arm-none-eabi-nm F7MeadowOS/hcom_nx/create_fs/hcom_nx_fatshim.o | grep trace

Note also that `./build.sh --configure` regenerates `.config` from the defconfig,
where all three symbols are off, so it silently undoes every tweak above.  Apply
them after a configure, never before.

**`flash.sh` leaves the board halted**, with no CDC/ACM port and no volume, which
looks exactly like a board that failed to boot.  It needs a reset afterwards:

    ${CUBE_APP} -c port=swd -hardRst

`flash.sh --reset` does this as part of the run, but a bare `./flash.sh` does not.
The volume comes back two or three seconds later.

Start with the trace alone and `STALL_MS` at 0.  Stalling and tracing at once
measures a host reacting to the stall rather than a host copying a file.

### Measurement 1: the host write order, on hardware (macOS)

Answered, and the answer is better than the design above assumed.  A 3 MB file
copied onto the volume produced this, trimmed to the essentials:

    fatshim: trace: 6207 sectors in 16 runs, 0 runs dropped
    trace:        0 ms  fat   lba      1 x24
    trace:       13 ms  fat   lba     28 x24
    trace:       48 ms  root  lba     55 x32
    trace:      179 ms  fat   lba      1 x24
    trace:      191 ms  root  lba     55 x32
    trace:      207 ms  fat   lba     28 x24
    trace:      220 ms  data  lba    215 x5927   cluster 18
    trace:     3378 ms  root  lba     55 x32
    trace:     3445 ms  root  lba     55 x32
    fatshim: flushing /meadow0/Scan1.pdf, 3034136 bytes
    fatshim: flushing /meadow0/Scan1.pdf complete      (52.08 s later)

**The whole file arrived as a single contiguous ascending run.**  5927 sectors,
cluster aligned - `lba 215` is exactly cluster 18 - with no gaps and no
out-of-order stragglers anywhere in the body.

**And there is not one FAT write after 207 ms.**  macOS allocated the complete
chain for all 741 clusters and committed it to both FAT copies before sending a
single byte of data.  The root directory went down at 48 ms and again at 191 ms,
also before the body.  So at the moment the first data sector arrives, the full
cluster chain and the directory entry are both already in the shadow.

Both prerequisites for streaming are therefore satisfied outright, with about 170
ms to spare.  Nothing has to be deferred and nothing has to be guessed.

The only data cluster written outside the big run was cluster 17, rewritten five
times - macOS bookkeeping, already caught by `name_is_hostjunk()`.

Two rates fall out of the same trace:

| | Rate |
|---|---|
| Host over the wire | 5927 sectors in 3.1 s, about 976 KB/s - full speed USB doing what it can |
| Flash on commit | 3034136 bytes in 52.08 s, 58.3 KB/s - the erase-bound figure from stage 4 |

Which states the present defect precisely: the dialog shows three seconds and the
data lands fifty-two seconds later.

The mount also produces a round of its own, 233 sectors in 12 runs ending in
`commit applied 0 changes`.  It is entirely repeated rewrites of the FAT and the
whole root area - macOS housekeeping, harmless, and the thing the plan earlier
noted as a full directory walk for nothing.

#### What this changes in the design

The provisional-name-and-rename step is no longer the main path.  It existed
because the file's name might not be known when data began arriving; it is known
170 ms early.  So:

* Resolve `(path, offset)` on arrival from the shadow FAT and the shadow root,
  open the real destination file and append as sectors come in.  One open serves
  the whole body.
* Keep the provisional name as the **fallback**, for a host that does not
  preallocate.  Windows and Linux are still unmeasured and may well differ.
* The fallback staging buffer can be small.  Nothing out of order arrived at all,
  so **16 MB of SDRAM is replaceable by a few tens of KB**.

Be explicit about the trade this buys.  Honest progress makes the copy dialog
about seventeen times longer, because it stops lying; total time to durability
improves slightly, because the writing overlaps the transfer instead of following
it.

#### The number measurement 2 has to clear

741 clusters in 52.08 s is **about 70 ms per 4 KB cluster write**.  That is the
per-command block streaming would impose, so the question is now narrow: does a
host tolerate a 70 ms WRITE(10)?  The 50 ms step should pass; 200 ms passing
would mean comfortable headroom.

#### One gap in this evidence

The trace records sector addresses, not contents, so it shows *that* the root
directory was written at 48 ms and not that it already named `Scan1.pdf` at
cluster 18.  The inference is strong - a complete FAT that is never touched again
only makes sense for a fully allocated file - but it is an inference.  Logging the
resolved name and first cluster when a root sector write lands would settle it,
and is cheap.

### Measurement 2: how long a host will let a command block

Run on hardware against macOS, with `MEADOW_USBMSC_FATSHIM_STALL_MS` stepped up.
Each row is one build, flash and copy; the stall applies once per 4 KB cluster,
which is the shape a streaming write would have.

| Stall per cluster | Clusters | Predicted | Elapsed | Result |
|---|---|---|---|---|
| 50 ms | 256 (1 MiB) | 12.8 s | 14.5 s | pass, byte identical |
| 200 ms | 64 (256 KiB) | 12.8 s | 16.1 s | pass, byte identical |
| 1000 ms | 16 (64 KiB) | 16.0 s | 27.9 s | pass, byte identical |
| 5000 ms | 4 (16 KiB) | 20.0 s | 63.3 s | pass, byte identical |
| 5000 ms | 8 (32 KiB) | 40.0 s | 63.0 s | **fail** - volume detached |
| 200 ms | 512 (2 MiB) | 102.4 s | 107.6 s | pass, device copy byte identical |

**The per-command answer is at least 5 seconds.**  Many 5000 ms writes completed
before anything went wrong, so a host tolerates a single mass storage command
blocking for five seconds.  A real cluster write costs about 70 ms, so streaming
has roughly seventy times the margin it needs.  This was the question that
decided whether the design was viable at all, and it is answered.

**Total duration is not the limit either.**  A 2 MB copy took 107.6 s of steady
200 ms commands and completed, so a long operation is not itself fatal.  That
matters, because a streamed 3 MB file takes about 52 s and a larger one more.

**Elapsed always exceeds prediction, and the excess scales with the stall** - 1.7
s at 50 ms, 3.3 s at 200 ms, 11.9 s at 1000 ms, 43 s at 5000 ms.  The cause is
macOS writing its own bookkeeping clusters, which measurement 1 already showed it
doing five times to cluster 17; each of those pays a stall too.  A streaming
implementation would not pay it, because those names are already filtered by
`name_is_hostjunk()` and never reach flash.

**Failure costs the volume, never the device.**  After the detach,
`/dev/cu.usbmodem...` was still present and `meadow file list` still worked.  The
CDC/ACM interface survived every failure in this whole exercise, so HCOM is not at
risk from a storage fault.  This is what the stage 1 graceful-degradation work was
aiming at, arrived at from the other direction.

**Integrity held even across a detach.**  The 2 MB run reported a mismatch when
compared through the host mount, which looked alarming until the same file was
read back with `meadow file read` - the independent HCOM and LittleFS path - and
compared byte for byte against the source.  All 2097152 bytes identical.  The
mismatch was the host reading a mount that was going away, not a defect in the
write path.  Worth recording as a method note: **compare through HCOM before
believing a mismatch seen through the mount.**

#### One thing this did not explain

Both 5000 ms runs ended at almost exactly 63 s - one completed there, one died
there - and the 2 MB run completed at 107.6 s but had lost the volume shortly
afterwards.  So something detaches the volume at long elapsed times that is
neither a per-command timeout nor total duration alone.

A plausible explanation is command starvation rather than command latency: there
is one SCSI thread, so a TEST UNIT READY or SYNCHRONIZE CACHE issued while a slow
write is in progress waits behind it, and those commands may carry much shorter
timeouts than a write.  That would make the hazard "any long operation starves
everything else", which is precisely the shape of the `_lock` problem this stage
exists to fix, and it would explain why the original crash looked like a host
timeout without any single write being outrageous.

It is a hypothesis.  Diagnosing it needs the console, and it does not block the
design: at 70 ms per cluster nothing is starved for more than 70 ms.  It does say
the 52 second commit must go, which was already the conclusion.

### Reading the console at all: two pre-existing breakages fixed

`CONFIG_RAMLOG_SYSLOG=y` puts the syslog in a RAM buffer at `/dev/kmsg`, so it
reaches neither the CDC/ACM port nor the ST-Link VCP.  `meadow listen` shows
application output and stays silent in storage mode, where the runtime is by
definition not running.  Reading the trace therefore needs `./build.sh --esd`,
which disables RAMLOG and routes the syslog to USART1.

That configuration did not build, for two unrelated reasons, neither introduced
by this work:

* `apps/system/hcom/comms/hcom_host_send.c` defines
  `hcom_trace_is_sending_to_cli()` unconditionally, but its only caller sits
  inside `#if defined CONFIG_RAMLOG_SYSLOG`.  Without RAMLOG the function is
  unused and `-Werror=unused-function` fails the build.  Now guarded on the same
  symbol as its caller.
* The kernel failed to link with `undefined reference to
  syslog_channel_register`.  `SYSLOG_CONSOLE` does `select SYSLOG_REGISTER` in
  `drivers/syslog/Kconfig`, but `enable_stack_dump()` in
  `scripts/configuration_tweaks.sh` uses `kconfig-tweak`, which writes symbols
  straight into `.config` **without running the Kconfig resolver**, so the select
  never fires.  `enable_stack_dump()` is a hand-maintained list of exactly these
  implications, so the missing one was added there.

That second point is worth generalising: **every `select` in Kconfig has to be
repeated by hand in these tweak functions.**  The symptom is a link failure for a
function that plainly exists, and the same mechanism explains why tweaked symbols
land in a block at the end of `.config` rather than in place.

`--esd` also enables `DEV_CONSOLE` and `STACK_COLORATION` and drops `ksram` from
79 KB to 46 KB by removing the RAMLOG buffer, so it is a diagnostic
configuration, not a shipping one.

#### Capturing it

    ./build.sh --wlclean --esd

then a terminal program on **USART1 / COM1 on the board header**, at **115200
8-N-1**, no flow control, from `CONFIG_USART1_BAUD` and its neighbours in the
defconfig.  Connect before resetting the board to catch the boot lines, and start
capturing to a file before the copy.

Two ports are the wrong ones and both look plausible:

* `/dev/cu.usbmodem<serial>1` is the CDC/ACM interface.  That is HCOM, not the
  console.
* the ST-Link VCP, `/dev/cu.usbmodem1203` or similar, is silent - USART1 is not
  wired to it on this board.  A separate USB to serial adapter on the header is
  needed.

`meadow listen` is also not it.  It shows *application* console output, so in
storage mode, where the runtime by definition is not running, it connects and
prints nothing at all.  That is easy to mistake for an absence of log lines.

#### Measuring without a console

Worth knowing, because it is how measurement 2 was actually run and it needs no
`--esd` build.  For the question "did the host tolerate this", four mechanical
signals are enough:

* `cp` exit status, and its stderr - a detach reports `Device not configured`;
* elapsed time against the predicted stall total, which is where the macOS
  bookkeeping overhead showed up;
* whether `/Volumes/MEADOW0` still exists afterwards;
* `meadow file list`, which reads through HCOM and LittleFS and so says whether
  the commit actually landed.

The console adds why, not whether.

### What the harnesses can and cannot do with this

`Testing/OTG-Harness/write` gained an `applyseq` mode that replays in ascending
order rather than shuffled, which is the only way to exercise the coalescing:
`apply` shuffles on purpose, so every sector starts a new run.  A 70 KB file
collapses 144 sectors into 7 runs ascending and into 144 shuffled, and the write
path is byte identical and idempotent either way.

That verifies the trace code.  It says nothing about the order a host writes in,
because both replay orders are chosen by the harness.  Per the rule this document
keeps having to restate: the answer is on a board.

### Order of work

1. **Done.**  Measurement 1: the directory entry and the whole FAT chain precede
   the data, and the body arrives as one ascending run.
2. **Done.**  Measurement 2: a command may block for at least five seconds,
   against the seventy milliseconds a cluster write needs.
3. **Next, and it needs the console.**  `LOCKWARN_MS` on during a large copy, to
   confirm the 52 second stall lands where the diagnosis says it does, and to test
   the starvation hypothesis from measurement 2 - whether what detaches the volume
   at long elapsed times is a short-timeout command queued behind a slow one
   rather than the slow one itself.  If it is, the fix is the same either way, but
   knowing tells you whether anything beyond the lock needs attention.
4. **Then the lock.**  Take the flash writing out from under `_lock`, or off the
   work queue and onto the SCSI thread, which is what streaming does anyway.  This
   is the part that must land before the staging buffer shrinks: it is the hazard
   that is present today and the likely cause of the original crash.
5. **Then the streaming path**, with the append fast path resolving `(path,
   offset)` on arrival and the provisional-name route as the fallback.
6. **Then shrink the staging buffer** and re-measure.  Nothing arrived out of
   order on macOS, so a few tens of KB should do, but Windows and Linux are
   unmeasured and this is the number they would move.

---

## Effort Summary

| Stage | New/changed kernel C | Difficulty | Main risk |
|-------|---------------------|------------|-----------|
| 1 - Composite bring-up (RAM disk) | ~300 lines (done) | Low | Host driver binding, Meadow CLI enumeration |
| 2 - Storage mode control plane | ~190 lines (done) | Low, once derived from the runtime state | Host behaviour while the drive is empty |
| 3 - Read-only synthetic FAT | ~1310 lines (done) | Moderate-high | LittleFS read path, throughput |
| 4 - Write and delete path | ~800 lines (done) | **High** | Settling timer, macOS as host, recoverability |
| 5 - Hardening | Varies | Low-moderate | RAM budget, OTA interaction |

Total for full read-write: roughly 1600-2000 lines of new kernel C. That is above the
initial estimate, driven by decision 5 - full filesystem access makes subdirectory
support mandatory in both the read and write paths, and it adds the OTA question to
Stage 4.

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

What partially substitutes for hardware now lives in the tree, at
`Testing/OTG-Harness/`, with its own README.  The read harness emits the
synthesized volume to an image for `fsck_msdos` and the Linux `vfat` driver to judge; the
write harness lets Linux be the host, modifying an image which is then replayed through the
write path in shuffled order.

The record is worth stating honestly in both directions.  Off-target testing caught a
write-protect mistake, a two-cluster file that a single-cluster shortcut would have
truncated, and a rename that would have zero filled good data.  It caught none of the three
defects that reached hardware, because all three lived in the integration - `usbmsc`,
`find_blockdriver`, the composite bind, or the interaction between successive commits - and
the harnesses touch none of that.

So: run them, and do not mistake a clean run for a working driver.
