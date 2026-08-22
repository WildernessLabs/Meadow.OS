/****************************************************************************
 * boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_usbmsc.c
 *
 *   Copyright (C) 2015-2016 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *           David Sidrane <david_s5@nscdg.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <errno.h>

#include <nuttx/board.h>
#include <nuttx/mmcsd.h>
#include <nuttx/spi/spi.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>

#include <meadow/meadow_storage_mode.h>

#ifdef CONFIG_MEADOW_USBMSC_FATSHIM
#  include <meadow/hcom_nx_fatshim.h>
#endif

#include "stm32_usbmsc.h"

#ifdef CONFIG_USBMSC

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Configuration ************************************************************/

#ifndef CONFIG_SYSTEM_USBMSC_DEVMINOR1
#  define CONFIG_SYSTEM_USBMSC_DEVMINOR1 0
#endif

#ifdef CONFIG_MEADOW_USBMSC_RAMDISK

/* Geometry of the bring-up RAM disk.  512 byte sectors with one sector per
 * cluster keeps the FAT12 arithmetic trivial and matches the sector size the
 * mass storage SCSI layer works in.
 */

#define RD_SECTORSIZE     512
#define RD_NSECTORS       CONFIG_MEADOW_USBMSC_RAMDISK_SECTORS

#define RD_RSVDSECCNT     1     /* Boot sector only */
#define RD_NUMFATS        2
#define RD_SECPERFAT      1
#define RD_SECPERCLUS     1
#define RD_ROOTENTCNT     64    /* 64 * 32 bytes == 4 sectors */
#define RD_MEDIA          0xf8

#define RD_ROOTDIRSECS    ((RD_ROOTENTCNT * 32) / RD_SECTORSIZE)
#define RD_METASECS       (RD_RSVDSECCNT + (RD_NUMFATS * RD_SECPERFAT) + \
                           RD_ROOTDIRSECS)
#define RD_DATASECS       (RD_NSECTORS - RD_METASECS)

/* One 512 byte FAT12 sector holds 341 entries, of which entries 0 and 1 are
 * reserved, so a single FAT sector can describe at most 339 clusters.  The
 * cluster count also has to stay below 4085 or the host will read the volume
 * as FAT16 rather than FAT12.
 */

#if RD_DATASECS < 16
#  error "CONFIG_MEADOW_USBMSC_RAMDISK_SECTORS is too small for a FAT12 volume"
#endif

#if RD_DATASECS > 339
#  error "CONFIG_MEADOW_USBMSC_RAMDISK_SECTORS needs more than one FAT sector"
#endif

/* Offsets into the FAT12 boot sector / BPB */

#define BPB_BYTESPERSEC   11
#define BPB_SECPERCLUS    13
#define BPB_RSVDSECCNT    14
#define BPB_NUMFATS       16
#define BPB_ROOTENTCNT    17
#define BPB_TOTSEC16      19
#define BPB_MEDIA         21
#define BPB_FATSZ16       22
#define BPB_SECPERTRK     24
#define BPB_NUMHEADS      26
#define BPB_HIDDSEC       28
#define BPB_TOTSEC32      32
#define BS_DRVNUM         36
#define BS_RESERVED1      37
#define BS_BOOTSIG        38
#define BS_VOLID          39
#define BS_VOLLAB         43
#define BS_FILSYSTYPE     54

/* Offsets into a 32 byte FAT directory entry */

#define DIR_NAME          0
#define DIR_ATTR          11
#define DIR_NTRES         12
#define DIR_CRTTIMETENTH  13
#define DIR_CRTTIME       14
#define DIR_CRTDATE       16
#define DIR_LSTACCDATE    18
#define DIR_FSTCLUSHI     20
#define DIR_WRTTIME       22
#define DIR_WRTDATE       24
#define DIR_FSTCLUSLO     26
#define DIR_FILESIZE      28

#define DIR_ENTRYSIZE     32

#define FAT_ATTR_VOLUMEID 0x08
#define FAT_ATTR_ARCHIVE  0x20

/* FAT12 end of cluster chain marker */

#define FAT12_EOC         0xfff

/* The first allocatable cluster in any FAT volume is number 2 */

#define FAT_FIRSTCLUSTER  2

/* Exactly 11 characters, as a FAT volume label must be */

#define RD_VOLUMELABEL    "MEADOW BOOT"

/* Fixed timestamp for seeded files: 2026-01-01 00:00:00.  A constant keeps
 * the image reproducible, and the RTC is not necessarily set this early.
 * FAT packs the date as ((year - 1980) << 9) | (month << 5) | day.
 */

#define RD_FILEDATE       (((2026 - 1980) << 9) | (1 << 5) | 1)
#define RD_FILETIME       0

/* Root directory slot assignments.  Slot 0 is the volume label. */

#define RD_SLOT_VOLLABEL  0
#define RD_SLOT_README    1

#endif /* CONFIG_MEADOW_USBMSC_RAMDISK */

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_MEADOW_USBMSC_RAMDISK

/* Seeded into the root directory at format time.  Its real purpose is to put
 * something in the data region, so that mounting the volume exercises file
 * data reads and not just the boot sector, FAT and directory sectors.
 */

static const char g_readme[] =
  "This is not the Meadow file system.\r\n"
  "\r\n"
  "This volume is a small RAM disk that exists only to bring up and test\r\n"
  "the USB composite device (CDC/ACM serial plus mass storage). It lives\r\n"
  "in DTCM, it is recreated empty on every boot, and anything written to\r\n"
  "it is lost at reset.\r\n"
  "\r\n"
  "The real Meadow file system is /meadow0, a LittleFS volume on the\r\n"
  "QSPI flash. No host OS can read LittleFS, so it is presented as a\r\n"
  "synthesized FAT16 volume instead. Select the fatshim rather than this\r\n"
  "RAM disk to see it: CONFIG_MEADOW_USBMSC_FATSHIM.\r\n"
  "\r\n"
  "See Documentation/Plans/OTG-Plan.md.\r\n";

/* Content length without the terminating NUL */

#define RD_README_LEN     (sizeof(g_readme) - 1)

#define RD_README_CLUSTERS \
  ((RD_README_LEN + (RD_SECPERCLUS * RD_SECTORSIZE) - 1) / \
   (RD_SECPERCLUS * RD_SECTORSIZE))

/* A preprocessor #if cannot be used here: RD_README_CLUSTERS contains a
 * sizeof, which the preprocessor cannot evaluate.
 */

static_assert(RD_README_CLUSTERS <= RD_DATASECS,
              "g_readme does not fit in the RAM disk data region");

/* Backing store for the RAM disk.
 *
 * It cannot come from the heap: in this PROTECTED build the additional heap
 * regions, the 32 MB of external SDRAM at CONFIG_HEAP2_BASE included, are
 * handed to the user heap by kumm_addregion(), so a kernel allocation can
 * only draw on CONFIG_MM_KERNEL_HEAPSIZE, which is far too small.
 *
 * It also does not go in ksram, which only has about 50 KB spare and is
 * shared with everything else in the kernel.  Instead it is placed in DTCM,
 * 128 KB at 0x20000000 that nothing else claims because
 * CONFIG_STM32F7_DTCMEXCLUDE keeps it out of the heap.  The section is
 * NOLOAD, so meadow_usbmsc_mkfat12() is what initializes the contents.
 */

static uint8_t g_rdbuffer[RD_NSECTORS * RD_SECTORSIZE]
  locate_data(".dtcm") aligned_data(4);

#endif

/* True once the block device behind mass storage LUN 0 is known good */

static bool g_lunavailable;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_MEADOW_USBMSC_RAMDISK

/****************************************************************************
 * Name: meadow_put_le16 and meadow_put_le32
 *
 * Description:
 *   Store a little endian field into the boot sector.  The BPB is not
 *   naturally aligned, so these go byte at a time rather than casting.
 *
 ****************************************************************************/

static void meadow_put_le16(FAR uint8_t *dest, uint16_t value)
{
  dest[0] = (uint8_t)(value & 0xff);
  dest[1] = (uint8_t)((value >> 8) & 0xff);
}

static void meadow_put_le32(FAR uint8_t *dest, uint32_t value)
{
  dest[0] = (uint8_t)(value & 0xff);
  dest[1] = (uint8_t)((value >> 8) & 0xff);
  dest[2] = (uint8_t)((value >> 16) & 0xff);
  dest[3] = (uint8_t)((value >> 24) & 0xff);
}

/****************************************************************************
 * Name: meadow_fat12_set_entry
 *
 * Description:
 *   Write one FAT12 entry, in every copy of the FAT.
 *
 *   FAT12 entries are 12 bits and share bytes with their neighbours: entry n
 *   starts at byte offset n * 3 / 2, occupying the low 12 bits of that
 *   halfword when n is even and the high 12 bits when n is odd.  The
 *   read-modify-write below is what preserves the neighbouring entry.
 *
 ****************************************************************************/

static void meadow_fat12_set_entry(uint16_t cluster, uint16_t value)
{
  FAR uint8_t *fat;
  unsigned int offset;
  int i;

  offset = cluster + (cluster / 2);

  for (i = 0; i < RD_NUMFATS; i++)
    {
      fat = &g_rdbuffer[(RD_RSVDSECCNT + (i * RD_SECPERFAT)) *
                        RD_SECTORSIZE];

      if ((cluster & 1) == 0)
        {
          fat[offset] = (uint8_t)(value & 0xff);
          fat[offset + 1] = (uint8_t)((fat[offset + 1] & 0xf0) |
                                     ((value >> 8) & 0x0f));
        }
      else
        {
          fat[offset] = (uint8_t)((fat[offset] & 0x0f) |
                                  ((value & 0x0f) << 4));
          fat[offset + 1] = (uint8_t)((value >> 4) & 0xff);
        }
    }
}

/****************************************************************************
 * Name: meadow_usbmsc_dirent
 *
 * Description:
 *   Return a pointer to one 32 byte slot in the root directory.
 *
 ****************************************************************************/

static FAR uint8_t *meadow_usbmsc_dirent(unsigned int slot)
{
  FAR uint8_t *rootdir;

  rootdir = &g_rdbuffer[(RD_RSVDSECCNT + (RD_NUMFATS * RD_SECPERFAT)) *
                        RD_SECTORSIZE];

  return &rootdir[slot * DIR_ENTRYSIZE];
}

/****************************************************************************
 * Name: meadow_usbmsc_clusterdata
 *
 * Description:
 *   Return a pointer to the start of a data cluster.  Cluster numbering
 *   starts at FAT_FIRSTCLUSTER, which maps to the first sector after the
 *   reserved, FAT and root directory areas.
 *
 ****************************************************************************/

static FAR uint8_t *meadow_usbmsc_clusterdata(uint16_t cluster)
{
  unsigned int sector;

  sector = RD_METASECS + ((cluster - FAT_FIRSTCLUSTER) * RD_SECPERCLUS);

  return &g_rdbuffer[sector * RD_SECTORSIZE];
}

/****************************************************************************
 * Name: meadow_usbmsc_addfile
 *
 * Description:
 *   Seed one file into the root directory: allocate a cluster run for it,
 *   chain those clusters in the FAT, copy the content into the data region
 *   and fill in the directory entry.
 *
 *   This is deliberately written against cluster runs rather than assuming a
 *   single cluster.  The synthetic FAT view of /meadow0 in stage 3 has to
 *   assign a run per file, so the arithmetic here is the same shape.
 *
 * Input Parameters:
 *   slot    - Root directory slot to use.
 *   name11  - Name in 8.3 form, space padded, exactly 11 characters.
 *   data    - File content.
 *   len     - Content length in bytes.
 *   cluster - First cluster of the run to allocate.
 *
 * Returned Value:
 *   The next free cluster after the run just allocated.
 *
 ****************************************************************************/

static uint16_t meadow_usbmsc_addfile(unsigned int slot,
                                      FAR const char *name11,
                                      FAR const char *data, size_t len,
                                      uint16_t cluster)
{
  const size_t clustersize = RD_SECPERCLUS * RD_SECTORSIZE;
  FAR uint8_t *dirent;
  size_t remaining;
  uint16_t current;
  size_t chunk;

  /* Copy the content out across as many clusters as it needs, chaining each
   * to its successor and terminating the last one.
   */

  current   = cluster;
  remaining = len;

  while (remaining > 0)
    {
      chunk = remaining > clustersize ? clustersize : remaining;

      memcpy(meadow_usbmsc_clusterdata(current), data, chunk);

      data      += chunk;
      remaining -= chunk;

      if (remaining > 0)
        {
          meadow_fat12_set_entry(current, current + 1);
          current++;
        }
      else
        {
          meadow_fat12_set_entry(current, FAT12_EOC);
        }
    }

  /* Now the directory entry */

  dirent = meadow_usbmsc_dirent(slot);

  memcpy(&dirent[DIR_NAME], name11, 11);
  dirent[DIR_ATTR]  = FAT_ATTR_ARCHIVE;
  dirent[DIR_NTRES] = 0;

  meadow_put_le16(&dirent[DIR_CRTTIME], RD_FILETIME);
  meadow_put_le16(&dirent[DIR_CRTDATE], RD_FILEDATE);
  meadow_put_le16(&dirent[DIR_LSTACCDATE], RD_FILEDATE);
  meadow_put_le16(&dirent[DIR_FSTCLUSHI], 0);
  meadow_put_le16(&dirent[DIR_WRTTIME], RD_FILETIME);
  meadow_put_le16(&dirent[DIR_WRTDATE], RD_FILEDATE);
  meadow_put_le16(&dirent[DIR_FSTCLUSLO], cluster);
  meadow_put_le32(&dirent[DIR_FILESIZE], (uint32_t)len);

  return current + 1;
}

/****************************************************************************
 * Name: meadow_usbmsc_mkfat12
 *
 * Description:
 *   Lay a bare FAT12 file system into the RAM disk buffer.
 *
 *   mkfatfs() is not usable here.  It lives in apps/fsutils/mkfatfs,
 *   which is user space, and this runs in the kernel half of a PROTECTED
 *   build.  The volume is small and fixed, so the boot sector, both FATs
 *   and the volume label are simply written out directly.
 *
 ****************************************************************************/

static void meadow_usbmsc_mkfat12(void)
{
  FAR uint8_t *bootsec = g_rdbuffer;
  FAR uint8_t *fat;
  FAR uint8_t *rootdir;
  int i;

  memset(g_rdbuffer, 0, sizeof(g_rdbuffer));

  /* Boot sector.  The jump instruction and "MSWIN4.1" OEM name are what
   * Windows, macOS and Linux all expect to find; some hosts are fussy about
   * their absence even though nothing ever executes this sector.
   */

  bootsec[0] = 0xeb;                  /* jmp short +0x3c */
  bootsec[1] = 0x3c;
  bootsec[2] = 0x90;                  /* nop */
  memcpy(&bootsec[3], "MSWIN4.1", 8);

  meadow_put_le16(&bootsec[BPB_BYTESPERSEC], RD_SECTORSIZE);
  bootsec[BPB_SECPERCLUS] = RD_SECPERCLUS;
  meadow_put_le16(&bootsec[BPB_RSVDSECCNT], RD_RSVDSECCNT);
  bootsec[BPB_NUMFATS] = RD_NUMFATS;
  meadow_put_le16(&bootsec[BPB_ROOTENTCNT], RD_ROOTENTCNT);
  meadow_put_le16(&bootsec[BPB_TOTSEC16], RD_NSECTORS);
  bootsec[BPB_MEDIA] = RD_MEDIA;
  meadow_put_le16(&bootsec[BPB_FATSZ16], RD_SECPERFAT);
  meadow_put_le16(&bootsec[BPB_SECPERTRK], 32);
  meadow_put_le16(&bootsec[BPB_NUMHEADS], 2);
  meadow_put_le32(&bootsec[BPB_HIDDSEC], 0);
  meadow_put_le32(&bootsec[BPB_TOTSEC32], 0);

  bootsec[BS_DRVNUM]    = 0x80;
  bootsec[BS_RESERVED1] = 0;
  bootsec[BS_BOOTSIG]   = 0x29;       /* Volume ID and label follow */
  meadow_put_le32(&bootsec[BS_VOLID], 0x4d454144);
  memcpy(&bootsec[BS_VOLLAB], RD_VOLUMELABEL, 11);
  memcpy(&bootsec[BS_FILSYSTYPE], "FAT12   ", 8);

  bootsec[RD_SECTORSIZE - 2] = 0x55;
  bootsec[RD_SECTORSIZE - 1] = 0xaa;

  /* Both copies of the FAT.  Entry 0 carries the media descriptor and
   * entry 1 is the end of chain marker, which in FAT12 is three bytes in
   * total.  Every remaining entry is already zero, meaning free.
   */

  for (i = 0; i < RD_NUMFATS; i++)
    {
      fat = &g_rdbuffer[(RD_RSVDSECCNT + (i * RD_SECPERFAT)) *
                        RD_SECTORSIZE];

      fat[0] = RD_MEDIA;
      fat[1] = 0xff;
      fat[2] = 0xff;
    }

  /* A volume label entry in the root directory, so the drive is named on the
   * host rather than showing up as "NO NAME" or "Untitled".
   */

  rootdir = meadow_usbmsc_dirent(RD_SLOT_VOLLABEL);

  memcpy(&rootdir[DIR_NAME], RD_VOLUMELABEL, 11);
  rootdir[DIR_ATTR] = FAT_ATTR_VOLUMEID;

  /* Seed one real file.  Without it the volume mounts but the data region is
   * never touched, so a successful mount would say nothing about whether
   * file content reads work through the mass storage path.
   */

  meadow_usbmsc_addfile(RD_SLOT_README, "README  TXT", g_readme,
                        RD_README_LEN, FAT_FIRSTCLUSTER);
}

/****************************************************************************
 * Block driver for the bring-up RAM disk
 *
 * This replaces the ramdisk_register() call Stage 1 used.  The medium is the
 * same buffer, but the mass storage LUN now has to be backed by a driver
 * we own, because the LUN is only bound while the board is in storage mode
 * and the driver is the thing that reports the geometry the bind checks.
 *
 * The synthetic FAT view of /meadow0 will replace the read and write bodies
 * below; the registration and geometry plumbing is meant to survive that.
 *
 ****************************************************************************/

static int meadow_rd_open(FAR struct inode *inode)
{
  UNUSED(inode);
  return OK;
}

static int meadow_rd_close(FAR struct inode *inode)
{
  UNUSED(inode);
  return OK;
}

static ssize_t meadow_rd_read(FAR struct inode *inode,
                              FAR unsigned char *buffer,
                              blkcnt_t start_sector, unsigned int nsectors)
{
  UNUSED(inode);

  if (start_sector + nsectors > RD_NSECTORS)
    {
      return -EINVAL;
    }

  memcpy(buffer, &g_rdbuffer[start_sector * RD_SECTORSIZE],
         nsectors * RD_SECTORSIZE);

  return nsectors;
}

static ssize_t meadow_rd_write(FAR struct inode *inode,
                               FAR const unsigned char *buffer,
                               blkcnt_t start_sector, unsigned int nsectors)
{
  UNUSED(inode);

  if (start_sector + nsectors > RD_NSECTORS)
    {
      return -EINVAL;
    }

  memcpy(&g_rdbuffer[start_sector * RD_SECTORSIZE], buffer,
         nsectors * RD_SECTORSIZE);

  return nsectors;
}

static int meadow_rd_geometry(FAR struct inode *inode,
                              FAR struct geometry *geometry)
{
  UNUSED(inode);

  if (geometry == NULL)
    {
      return -EINVAL;
    }

  memset(geometry, 0, sizeof(struct geometry));

  /* Note that geo_available is reported true unconditionally, even outside
   * storage mode.  Media presence is NOT expressed here: usbmsc_bindlun()
   * refuses a LUN whose geometry reports unavailable, so a false here would
   * mean the LUN could never be bound at all.  Absence of media is instead
   * expressed by leaving the LUN unbound, which makes the SCSI layer answer
   * MEDIA NOT PRESENT.  See board_mscclassobject().
   */

  geometry->geo_available     = true;
  geometry->geo_mediachanged  = false;
  geometry->geo_writeenabled  = true;
  geometry->geo_nsectors      = RD_NSECTORS;
  geometry->geo_sectorsize    = RD_SECTORSIZE;

  strlcpy(geometry->geo_model, "Meadow RAM disk",
          sizeof(geometry->geo_model));

  return OK;
}

static const struct block_operations g_meadow_rd_bops =
{
  .open     = meadow_rd_open,
  .close    = meadow_rd_close,
  .read     = meadow_rd_read,
  .write    = meadow_rd_write,
  .geometry = meadow_rd_geometry,
  .ioctl    = NULL,
};

/****************************************************************************
 * Name: meadow_usbmsc_ramdisk_initialize
 *
 * Description:
 *   Format the RAM disk and register the block driver over it.
 *
 * Returned Value:
 *   Zero on success; a negated errno on failure.
 *
 ****************************************************************************/

static int meadow_usbmsc_ramdisk_initialize(void)
{
  int ret;

  meadow_usbmsc_mkfat12();

  ret = register_blockdriver(MEADOW_USBMSC_LUN0_DEVPATH, &g_meadow_rd_bops,
                             0666, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: register_blockdriver(%s) failed: %d\n",
             MEADOW_USBMSC_LUN0_DEVPATH, ret);
      return ret;
    }

  syslog(LOG_NOTICE, "USB MSC: %s is a %d KB FAT12 RAM disk, %d clusters\n",
         MEADOW_USBMSC_LUN0_DEVPATH,
         (RD_NSECTORS * RD_SECTORSIZE) / 1024, RD_DATASECS);

  return OK;
}

#endif /* CONFIG_MEADOW_USBMSC_RAMDISK */

/****************************************************************************
 * Name: meadow_usbmsc_isblockdev
 *
 * Description:
 *   Confirm that a path names a registered block device.
 *
 *   nx_stat() is used rather than opening the node.  Opening a block device
 *   through the file interface needs the BCH proxy layer, which this
 *   configuration does not necessarily include, whereas a stat of the pseudo
 *   file system inode is always available and is all that is needed here.
 *
 ****************************************************************************/

static bool meadow_usbmsc_isblockdev(FAR const char *devpath)
{
  struct stat buf;
  int ret;

  ret = nx_stat(devpath, &buf, 1);
  if (ret < 0)
    {
      return false;
    }

  return S_ISBLK(buf.st_mode);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_usbmsc_initialize
 *
 * Description:
 *   Prepare the block device that backs USB mass storage LUN 0.
 *
 *   This is called from board_composite_initialize() before the composite
 *   device is connected.  Failure is deliberately not fatal to the caller:
 *   board_composite0_connect() consults meadow_usbmsc_lun_available() and
 *   drops the mass storage interface if the medium did not come up, leaving
 *   the CDC/ACM interface that HCOM depends on intact.
 *
 * Returned Value:
 *   Zero on success; a negated errno on failure.
 *
 ****************************************************************************/

int board_usbmsc_initialize(int port)
{
  UNUSED(port);

  if (g_lunavailable)
    {
      /* Already prepared.  Registering the RAM disk twice would fail. */

      return OK;
    }

#ifdef CONFIG_MEADOW_USBMSC_RAMDISK
  int ret = meadow_usbmsc_ramdisk_initialize();

  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_MEADOW_USBMSC_FATSHIM
  /* Registers the block device only.  The scan that builds the view is
   * deferred until the first sector is read, because /meadow0 is not
   * mounted yet at this point in the boot: board_init_usbdev() runs well
   * before hcom_nx_setup_mgr() brings the file system up.
   */

  int ret = hcom_nx_fatshim_initialize(MEADOW_USBMSC_LUN0_DEVPATH);

  if (ret < 0)
    {
      return ret;
    }
#endif

  if (!meadow_usbmsc_isblockdev(MEADOW_USBMSC_LUN0_DEVPATH))
    {
      syslog(LOG_WARNING,
             "USB MSC: %s absent, mass storage will not be advertised\n",
             MEADOW_USBMSC_LUN0_DEVPATH);
      return -ENODEV;
    }

  g_lunavailable = true;
  return OK;
}

/****************************************************************************
 * Name: meadow_usbmsc_lun_available
 *
 * Description:
 *   Report whether the block device backing mass storage LUN 0 came up.
 *
 ****************************************************************************/

bool meadow_usbmsc_lun_available(void)
{
  return g_lunavailable;
}

#endif /* CONFIG_USBMSC */
