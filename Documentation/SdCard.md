# SD Card

The CCM has an SD card slot on SDMMC2. The card is mounted on `/sdcard` by the NuttX
automounter (`CONFIG_FS_AUTOMOUNTER`, `nuttx/fs/mount/fs_automount.c`): at boot when a card is
in, and whenever one is inserted. It is unmounted when the card is removed. Nothing else in
Meadow.OS mounts or unmounts the card, and managed code should not either (Issue #88).

## When It Is Used

The SD card is set up only on a CCM (`meadow_hw_version_sdcard_supported()` is true only for the CCMv2), and only when `meadow.config.yaml` turns it on
(`Device:` `SdStorageSupported: true`, off by default). `hcom_nx_setup_mgr()` then calls
`stm32_sdio_initialize_meadow()` (`F7MeadowOS/meadow_os/meadow_sdmmc.c`). Other boards have no
`/dev/mmcsd0` and no `/sdcard`.

## Card Detect

The card detect pin is PG6 (`GPIO_MEADOW_SDIO_NCD`). It is pulled low when a card is in. Its
interrupt, `stm32_ncd_interrupt()`, does two things on each edge:

* If the state has changed, it calls `sdio_mediachange()`, so the MMC/SD driver knows at once
  whether there is a card, and logs "SDMMC interrupt. Card was ..., now is ...". Contact bounce
  gives extra edges with no change; these are not logged.
* It passes the edge to the automounter, which waits for the pin to settle before acting.

## The Automounter

`meadow_sdmmc.c` gives the automounter a `struct automount_lower_s`:

| Field | Value |
|-------|-------|
| `fstype`, `blockdev`, `mountpoint` | `vfat`, `/dev/mmcsd0`, `/sdcard` (`MEADOW_SDCARD_*` in `hcom_shared_common.h`) |
| `ddelay` | 1 s (`MEADOW_SDCARD_AUTOMOUNT_DDELAY` in `meadow_sdmmc.c`): the pin must be stable this long before a mount or unmount |
| `udelay` | 2 s (`MEADOW_SDCARD_AUTOMOUNT_UDELAY` in `meadow_sdmmc.c`): the time between unmount retries while files are open |
| `attach`, `enable`, `inserted` | Hooks: save the automounter's handler, gate the call to it from `stm32_ncd_interrupt()`, and read PG6 |

The automounter does its work on the low-priority work queue (`CONFIG_SCHED_LPWORK`), not in the
interrupt:

* **Boot.** `automount_initialize()` reads PG6 and does the first mount `ddelay` later, so boot
  does not wait for it.
* **Insertion.** About 1 second after the card goes in, it is mounted.
* **Removal.** About 1 second after the card comes out, it is unmounted. If files on it are still
  open the unmount fails (`-EBUSY`), and it is retried every 2 seconds until they are closed.
  Until then, reads and writes on those files fail.
* **Eject.** An app may unmount the card itself (`umount2("/sdcard")`) before removing it. The
  automounter accepts that, and mounts the card again only when it is next inserted.
* **A card that will not mount** (not FAT, or faulty) is logged by the automounter. SD support
  stays on, so a good card inserted later is mounted.

The automounter logs only with NuttX file system debug enabled, or `CONFIG_FS_AUTOMOUNTER_DEBUG`.
To see whether the card is mounted, look for `/sdcard`: it exists only while a card is mounted.

## Users of the Card

* **HCOM.** For CLI file commands on `/sdcard` (write, read, delete, list),
  `hcom_host_preprocess.c` only checks that `/sdcard` is mounted (`statfs()`). With no card, a
  write or delete reports "SD-Card not inserted".
* **Managed code.** Meadow.Core should treat the card as present when `/sdcard` is mounted, and
  not mount it itself (Issue #668). `CONFIG_FS_AUTOMOUNTER_DRIVER` could later let it be
  signalled on each mount and unmount instead of watching PG6.

## Tests

Developer tests 7 (`-u=sdcard`) and 13 (`-u=dirmgmt`) use the card. See the SD card section of
[RunningDeveloperTests.md](RunningDeveloperTests.md).
