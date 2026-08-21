/****************************************************************************
 * /Meadow.OS/hcom_nx/create_fs/hcom_nx_fatshim.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

// A read-only FAT16 view of /meadow0. See meadow/hcom_nx_fatshim.h for what
// this is for; this file is the implementation.
//
// Everything below the "Synthesis" heading is pure computation over the model
// and touches no file system, which is what lets it be compiled and checked on
// a host. FATSHIM_HOST_TEST swaps the platform layer for stubs; nothing else in
// the file changes between the two builds.

#ifdef FATSHIM_HOST_TEST
#  include "fatshim_host_stubs.h"
#else
#  include <nuttx/config.h>

#  include <stdbool.h>
#  include <stdint.h>
#  include <stdio.h>
#  include <string.h>
#  include <dirent.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <syslog.h>
#  include <sys/stat.h>
#  include <sys/types.h>

#  include <nuttx/fs/fs.h>
#  include <nuttx/fs/ioctl.h>
#  include <nuttx/mutex.h>

#  include <meadow/hcom_shared_common.h>
#  include <meadow/hcom_nx_fatshim.h>
#endif

//===========================================================================
// Volume geometry
//===========================================================================

// Fixed by configuration rather than derived from the partition, because the
// mass storage LUN is bound early in the boot, before the partition table has
// been built. usbmsc_bindlun() reads the geometry at that moment and never
// again, so it has to be answerable before /meadow0 exists.
//
// Presenting a volume slightly larger than the partition would be harmless
// here: the view is read-only, and every sector in range is answerable because
// clusters no file owns simply read as zeros.

#define FS_SECTORSIZE       512
#define FS_SECPERCLUS       8                 // 4 KB clusters
#define FS_CLUSTERSIZE      (FS_SECPERCLUS * FS_SECTORSIZE)
#define FS_NUMFATS          2
#define FS_ROOTENTCNT       512
#define FS_DATACLUSTERS     6900
#define FS_RSVDSECCNT       1
#define FS_MEDIA            0xf8

#define FS_ROOTDIRSECS      ((FS_ROOTENTCNT * 32) / FS_SECTORSIZE)
#define FS_SECPERFAT        (((FS_DATACLUSTERS + 2) * 2 + FS_SECTORSIZE - 1) \
                             / FS_SECTORSIZE)
#define FS_FATSTART         FS_RSVDSECCNT
#define FS_ROOTSTART        (FS_FATSTART + (FS_NUMFATS * FS_SECPERFAT))
#define FS_DATASTART        (FS_ROOTSTART + FS_ROOTDIRSECS)
#define FS_TOTALSECTORS     (FS_DATASTART + (FS_DATACLUSTERS * FS_SECPERCLUS))

// A cluster count in [4085, 65525) is what makes a host read this as FAT16
// rather than FAT12 or FAT32. Getting it wrong does not fail loudly; the host
// simply interprets the FAT with the wrong entry width.

#if FS_DATACLUSTERS < 4085
#  error "FS_DATACLUSTERS too low - a host would read this volume as FAT12"
#endif

#if FS_DATACLUSTERS >= 65525
#  error "FS_DATACLUSTERS too high - a host would read this volume as FAT32"
#endif

#define FS_SLOTSPERSECTOR   (FS_SECTORSIZE / 32)
#define FS_FATPERSECTOR     (FS_SECTORSIZE / 2)

#define FS_EOC              0xffff
#define FS_FIRSTCLUSTER     2

//===========================================================================
// Model limits
//===========================================================================

#define FS_MAXENTRIES       512
#define FS_NAMEPOOL         (12 * 1024)
#define FS_MAXDEPTH         6
#define FS_MAXPATH          256
#define FS_MAXNAME          100

#define FS_ROOTPARENT       (-1)

#define FS_VOLUMELABEL      "MEADOW0"

//===========================================================================
// FAT on-disk field offsets
//===========================================================================

#define BPB_BYTESPERSEC     11
#define BPB_SECPERCLUS      13
#define BPB_RSVDSECCNT      14
#define BPB_NUMFATS         16
#define BPB_ROOTENTCNT      17
#define BPB_TOTSEC16        19
#define BPB_MEDIA           21
#define BPB_FATSZ16         22
#define BPB_SECPERTRK       24
#define BPB_NUMHEADS        26
#define BPB_HIDDSEC         28
#define BPB_TOTSEC32        32
#define BS_DRVNUM           36
#define BS_BOOTSIG          38
#define BS_VOLID            39
#define BS_VOLLAB           43
#define BS_FILSYSTYPE       54

#define DIR_NAME            0
#define DIR_ATTR            11
#define DIR_NTRES           12
#define DIR_CRTTIME         14
#define DIR_CRTDATE         16
#define DIR_LSTACCDATE      18
#define DIR_FSTCLUSHI       20
#define DIR_WRTTIME         22
#define DIR_WRTDATE         24
#define DIR_FSTCLUSLO       26
#define DIR_FILESIZE        28
#define DIR_ENTRYSIZE       32

#define ATTR_READONLY       0x01
#define ATTR_VOLUMEID       0x08
#define ATTR_DIRECTORY      0x10
#define ATTR_ARCHIVE        0x20
#define ATTR_LONGNAME       0x0f

#define LFN_SEQ             0
#define LFN_ATTR            11
#define LFN_TYPE            12
#define LFN_CHKSUM          13
#define LFN_FSTCLUSLO       26
#define LFN_LAST            0x40
#define LFN_CHARSPERSLOT    13

// A fixed timestamp keeps the view stable across reads, which matters because
// a host that sees a directory entry change underneath it may re-read and
// disagree with itself. The real modification times are not available cheaply
// and are not worth a stat() per entry.

#define FS_FILEDATE         (((2026 - 1980) << 9) | (1 << 5) | 1)
#define FS_FILETIME         0

//===========================================================================
// Model
//===========================================================================

struct fatshim_entry_s
{
  uint16_t nameoff;       // Offset of the name in _namepool
  int16_t  parent;        // Parent entry index, FS_ROOTPARENT for the root
  uint16_t firstcluster;  // 0 when the entry occupies no clusters
  uint16_t nclusters;
  uint32_t size;          // File length in bytes; 0 for a directory
  uint16_t dirslot;       // First 32 byte slot inside the parent directory
  uint16_t dirslots;      // Slots this directory's own listing occupies
  uint8_t  nslots;        // Slots this entry's name occupies in its parent
  uint8_t  isdir;
  uint8_t  shortname[11]; // Padded 8.3 alias, always present
};

// The two large tables go in DTCM rather than ksram. ksram is 128 KB shared
// with the whole kernel and has only about 50 KB spare, whereas DTCM is 128 KB
// that nothing else claims because CONFIG_STM32F7_DTCMEXCLUDE keeps it out of
// every heap. kernel-space.ld places the .dtcm section there.
//
// The section is NOLOAD, so nothing here is zeroed at startup. That is safe
// because model_build() resets the counts and every entry is memset before
// use; nothing below relies on these starting out zero.

static struct fatshim_entry_s _entries[FS_MAXENTRIES] locate_data(".dtcm");
static char _namepool[FS_NAMEPOOL] locate_data(".dtcm");
static int _entrycount;
static int _namepoolused;
static uint16_t _rootslots;
static uint16_t _nextcluster;
static bool _modelbuilt;
static bool _modelfailed;

// One cached file handle. The SCSI layer reads a single sector per call, so an
// uncached implementation would re-open and re-seek LittleFS every 512 bytes.
static struct file _cachedfile;
static int _cachedentry = -1;
static bool _cachedopen;

static mutex_t _lock;

//===========================================================================
// Small helpers
//===========================================================================

static void put_le16(uint8_t *dest, uint16_t value)
{
  dest[0] = (uint8_t)(value & 0xff);
  dest[1] = (uint8_t)((value >> 8) & 0xff);
}

static void put_le32(uint8_t *dest, uint32_t value)
{
  dest[0] = (uint8_t)(value & 0xff);
  dest[1] = (uint8_t)((value >> 8) & 0xff);
  dest[2] = (uint8_t)((value >> 16) & 0xff);
  dest[3] = (uint8_t)((value >> 24) & 0xff);
}

static const char *entry_name(int index)
{
  return &_namepool[_entries[index].nameoff];
}

static char fat_upper(char c)
{
  return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

// Characters a short FAT name may not contain. Anything else becomes '_'.
static bool fat_shortname_char_ok(char c)
{
  static const char *bad = "\"*+,./:;<=>?[\\]|";

  if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e)
    {
      return false;
    }

  return strchr(bad, c) == NULL;
}

// True when a name can be represented exactly by an 8.3 short entry, in which
// case no long name entries are needed for it.
static bool fat_fits_shortname(const char *name)
{
  const char *dot;
  size_t baselen;
  size_t extlen;
  size_t i;

  if (name[0] == '\0' || name[0] == '.')
    {
      return false;
    }

  dot = strrchr(name, '.');
  baselen = dot ? (size_t)(dot - name) : strlen(name);
  extlen = dot ? strlen(dot + 1) : 0;

  if (baselen == 0 || baselen > 8 || extlen > 3)
    {
      return false;
    }

  // A second dot cannot be represented at all.

  if (dot != NULL && strchr(name, '.') != dot)
    {
      return false;
    }

  for (i = 0; name[i] != '\0'; i++)
    {
      if (name[i] == '.')
        {
          continue;
        }

      // Lower case has to go through a long name, otherwise the host would
      // display the upper cased alias instead of the real name.

      if (name[i] >= 'a' && name[i] <= 'z')
        {
          return false;
        }

      if (!fat_shortname_char_ok(name[i]))
        {
          return false;
        }
    }

  return true;
}

// Builds the padded 11 byte 8.3 form. 'ordinal' disambiguates within a
// directory: names that needed shortening get a "~n" tail, which is what keeps
// two long names that share a prefix from colliding.
static void fat_make_shortname(const char *name, unsigned int ordinal,
                               uint8_t out[11])
{
  char tail[8];
  const char *dot;
  size_t baselen;
  size_t taillen;
  size_t keep;
  size_t o = 0;
  size_t i;

  memset(out, ' ', 11);

  if (fat_fits_shortname(name))
    {
      dot = strrchr(name, '.');
      baselen = dot ? (size_t)(dot - name) : strlen(name);

      for (i = 0; i < baselen; i++)
        {
          out[i] = (uint8_t)name[i];
        }

      if (dot != NULL)
        {
          for (i = 0; dot[1 + i] != '\0' && i < 3; i++)
            {
              out[8 + i] = (uint8_t)dot[1 + i];
            }
        }

      return;
    }

  snprintf(tail, sizeof(tail), "~%u", ordinal);
  taillen = strlen(tail);
  keep = (taillen >= 8) ? 0 : (8 - taillen);

  dot = strrchr(name, '.');
  baselen = dot ? (size_t)(dot - name) : strlen(name);

  for (i = 0; i < baselen && o < keep; i++)
    {
      char c = fat_upper(name[i]);

      if (c == ' ')
        {
          continue;
        }

      out[o++] = fat_shortname_char_ok(c) ? (uint8_t)c : (uint8_t)'_';
    }

  if (o == 0)
    {
      out[o++] = '_';
    }

  for (i = 0; i < taillen && o < 8; i++)
    {
      out[o++] = (uint8_t)tail[i];
    }

  if (dot != NULL)
    {
      for (i = 0, o = 0; dot[1 + i] != '\0' && o < 3; i++)
        {
          char c = fat_upper(dot[1 + i]);

          out[8 + o] = fat_shortname_char_ok(c) ? (uint8_t)c : (uint8_t)'_';
          o++;
        }
    }
}

// The checksum every long name slot carries, tying it to its short entry. A
// host that finds a mismatch discards the long name and shows the alias, which
// is the usual reason long file names silently fail.
static uint8_t fat_lfn_checksum(const uint8_t shortname[11])
{
  uint8_t sum = 0;
  int i;

  for (i = 0; i < 11; i++)
    {
      sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + shortname[i]);
    }

  return sum;
}

static uint8_t fat_lfn_slots(const char *name)
{
  size_t len = strlen(name);

  return (uint8_t)((len + LFN_CHARSPERSLOT - 1) / LFN_CHARSPERSLOT);
}

//===========================================================================
// Model construction
//===========================================================================

// Rebuilds the full path of an entry by walking back up to the mount point.
// Paths are not stored per entry because the name pool would then hold every
// ancestor again for every child.
static int entry_path(int index, char *buf, size_t buflen)
{
  int chain[FS_MAXDEPTH];
  int depth = 0;
  size_t used;
  int i;

  while (index >= 0 && depth < FS_MAXDEPTH)
  {
    chain[depth++] = index;
    index = _entries[index].parent;
  }

  if (index >= 0)
  {
    return -ENAMETOOLONG;   // Deeper than FS_MAXDEPTH
  }

  used = strlen(MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME);
  if (used >= buflen)
  {
    return -ENAMETOOLONG;
  }

  strcpy(buf, MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME);

  for (i = depth - 1; i >= 0; i--)
  {
    const char *name = entry_name(chain[i]);
    size_t namelen = strlen(name);

    if (used + 1 + namelen + 1 > buflen)
    {
      return -ENAMETOOLONG;
    }

    buf[used++] = '/';
    strcpy(&buf[used], name);
    used += namelen;
  }

  return OK;
}

static int model_add_entry(const char *name, int parent, bool isdir,
                           uint32_t size)
{
  struct fatshim_entry_s *e;
  size_t namelen = strlen(name);

  if (_entrycount >= FS_MAXENTRIES)
  {
    return -ENOSPC;
  }

  if (namelen == 0 || namelen > FS_MAXNAME)
  {
    return -ENAMETOOLONG;
  }

  if (_namepoolused + (int)namelen + 1 > FS_NAMEPOOL)
  {
    return -ENOSPC;
  }

  e = &_entries[_entrycount];
  memset(e, 0, sizeof(*e));

  e->nameoff = (uint16_t)_namepoolused;
  strcpy(&_namepool[_namepoolused], name);
  _namepoolused += (int)namelen + 1;

  e->parent = (int16_t)parent;
  e->isdir = isdir ? 1 : 0;
  e->size = isdir ? 0 : size;

  return _entrycount++;
}

// Depth first walk of /meadow0. Entries are created in the order they are
// found, which is what later makes the cluster runs monotonic in entry order.
static int model_scan_dir(const char *path, int parent, int depth)
{
  char childpath[FS_MAXPATH];
  DIR *dir;
  struct dirent *de;
  int index;
  int ret = OK;

  if (depth >= FS_MAXDEPTH)
  {
    return OK;      // Deeper than we present; not an error
  }

  dir = opendir(path);
  if (dir == NULL)
  {
    return -ENOENT;
  }

  while ((de = readdir(dir)) != NULL)
  {
    bool isdir = (de->d_type == DTYPE_DIRECTORY);
    uint32_t size = 0;

    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
    {
      continue;
    }

    if (snprintf(childpath, sizeof(childpath), "%s/%s", path,
                 de->d_name) >= (int)sizeof(childpath))
    {
      continue;   // Cannot address it, so do not advertise it
    }

    if (!isdir)
    {
      struct stat st;

      if (nx_stat(childpath, &st, 1) < 0)
      {
        continue;
      }

      size = (uint32_t)st.st_size;
    }

    index = model_add_entry(de->d_name, parent, isdir, size);
    if (index < 0)
    {
      // Out of model space. Stop adding, but keep what is already there:
      // a partial listing is more useful than none, and the host is not
      // being told the volume is complete.

      syslog(LOG_WARNING, "fatshim: model full, %s omitted\n", de->d_name);
      ret = OK;
      break;
    }

    if (isdir)
    {
      model_scan_dir(childpath, index, depth + 1);
    }
  }

  closedir(dir);
  return ret;
}

// Assigns each entry its slot range inside its parent's listing, and each
// directory the total number of slots its own listing occupies.
static void model_assign_slots(void)
{
  int parents[FS_MAXENTRIES + 1];
  unsigned int ordinal;
  uint16_t slot;
  int i;
  int p;
  int pi;

  // Root first, then each directory entry. Index FS_MAXENTRIES stands for the
  // root so that one loop covers both cases.

  for (pi = -1; pi < _entrycount; pi++)
  {
    if (pi >= 0 && !_entries[pi].isdir)
    {
      continue;
    }

    p = (pi < 0) ? FS_ROOTPARENT : pi;

    // Slot 0 of the root holds the volume label; a subdirectory starts with
    // its "." and ".." entries.

    slot = (p == FS_ROOTPARENT) ? 1 : 2;
    ordinal = 1;

    for (i = 0; i < _entrycount; i++)
    {
      struct fatshim_entry_s *e = &_entries[i];

      if (e->parent != p)
      {
        continue;
      }

      fat_make_shortname(entry_name(i), ordinal, e->shortname);

      if (fat_fits_shortname(entry_name(i)))
      {
        e->nslots = 1;
      }
      else
      {
        e->nslots = (uint8_t)(fat_lfn_slots(entry_name(i)) + 1);
        ordinal++;
      }

      e->dirslot = slot;
      slot = (uint16_t)(slot + e->nslots);
    }

    if (p == FS_ROOTPARENT)
    {
      _rootslots = slot;
    }
    else
    {
      _entries[p].dirslots = slot;
    }
  }

  (void)parents;
}

static void model_assign_clusters(void)
{
  int i;

  _nextcluster = FS_FIRSTCLUSTER;

  for (i = 0; i < _entrycount; i++)
  {
    struct fatshim_entry_s *e = &_entries[i];
    uint32_t bytes;
    uint32_t need;

    if (e->isdir)
    {
      bytes = (uint32_t)e->dirslots * DIR_ENTRYSIZE;
      need = (bytes + FS_CLUSTERSIZE - 1) / FS_CLUSTERSIZE;

      if (need == 0)
      {
        need = 1;     // A directory always occupies at least one cluster
      }
    }
    else
    {
      need = (e->size + FS_CLUSTERSIZE - 1) / FS_CLUSTERSIZE;
    }

    if (need == 0)
    {
      e->firstcluster = 0;    // Empty file: FAT says no chain
      e->nclusters = 0;
      continue;
    }

    if (_nextcluster + need > FS_FIRSTCLUSTER + FS_DATACLUSTERS)
    {
      // The volume cannot hold this entry. Present it as empty rather than
      // pointing at clusters that are not ours.

      syslog(LOG_WARNING, "fatshim: out of clusters at %s\n",
             entry_name(i));
      e->firstcluster = 0;
      e->nclusters = 0;
      e->size = 0;
      continue;
    }

    e->firstcluster = _nextcluster;
    e->nclusters = (uint16_t)need;
    _nextcluster = (uint16_t)(_nextcluster + need);
  }
}

// Builds the model. Called lazily, under _lock, on the first read.
static int model_build(void)
{
  int ret;

  _entrycount = 0;
  _namepoolused = 0;
  _rootslots = 1;
  _nextcluster = FS_FIRSTCLUSTER;

  ret = model_scan_dir(MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME,
                       FS_ROOTPARENT, 0);
  if (ret < 0)
  {
    syslog(LOG_ERR, "fatshim: cannot scan %s: %d\n",
           MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME, ret);
    return ret;
  }

  model_assign_slots();
  model_assign_clusters();

  if (_rootslots > FS_ROOTENTCNT)
  {
    // More root entries than the fixed root directory can hold. The excess
    // would be invisible, so say so rather than silently truncating.

    syslog(LOG_WARNING,
           "fatshim: root needs %u slots, only %d available\n",
           (unsigned int)_rootslots, FS_ROOTENTCNT);
  }

  syslog(LOG_NOTICE,
         "fatshim: %d entries, %u clusters used of %d, %u root slots\n",
         _entrycount, (unsigned int)(_nextcluster - FS_FIRSTCLUSTER),
         FS_DATACLUSTERS, (unsigned int)_rootslots);

  return OK;
}

//===========================================================================
// Synthesis
//
// Everything from here to the block operations is pure computation over the
// model. Nothing touches a file system except read_file_bytes().
//===========================================================================

static void synth_boot_sector(uint8_t *buf)
{
  memset(buf, 0, FS_SECTORSIZE);

  // The jump instruction and OEM name are never executed, but hosts do check
  // for them, so they are not optional.

  buf[0] = 0xeb;
  buf[1] = 0x3c;
  buf[2] = 0x90;
  memcpy(&buf[3], "MSWIN4.1", 8);

  put_le16(&buf[BPB_BYTESPERSEC], FS_SECTORSIZE);
  buf[BPB_SECPERCLUS] = FS_SECPERCLUS;
  put_le16(&buf[BPB_RSVDSECCNT], FS_RSVDSECCNT);
  buf[BPB_NUMFATS] = FS_NUMFATS;
  put_le16(&buf[BPB_ROOTENTCNT], FS_ROOTENTCNT);
  buf[BPB_MEDIA] = FS_MEDIA;
  put_le16(&buf[BPB_FATSZ16], FS_SECPERFAT);
  put_le16(&buf[BPB_SECPERTRK], 63);
  put_le16(&buf[BPB_NUMHEADS], 255);
  put_le32(&buf[BPB_HIDDSEC], 0);

  // A sector count that does not fit in 16 bits has to move to the 32 bit
  // field, with the 16 bit one zeroed.

  if (FS_TOTALSECTORS < 0x10000)
  {
    put_le16(&buf[BPB_TOTSEC16], FS_TOTALSECTORS);
    put_le32(&buf[BPB_TOTSEC32], 0);
  }
  else
  {
    put_le16(&buf[BPB_TOTSEC16], 0);
    put_le32(&buf[BPB_TOTSEC32], FS_TOTALSECTORS);
  }

  buf[BS_DRVNUM] = 0x80;
  buf[BS_BOOTSIG] = 0x29;
  put_le32(&buf[BS_VOLID], 0x4d454144);

  memset(&buf[BS_VOLLAB], ' ', 11);
  memcpy(&buf[BS_VOLLAB], FS_VOLUMELABEL, strlen(FS_VOLUMELABEL));
  memcpy(&buf[BS_FILSYSTYPE], "FAT16   ", 8);

  buf[FS_SECTORSIZE - 2] = 0x55;
  buf[FS_SECTORSIZE - 1] = 0xaa;
}

static void synth_fat_sector(unsigned int fatsector, uint8_t *buf)
{
  uint32_t base = (uint32_t)fatsector * FS_FATPERSECTOR;
  int i;

  memset(buf, 0, FS_SECTORSIZE);

  if (fatsector == 0)
  {
    put_le16(&buf[0], 0xff00 | FS_MEDIA);
    put_le16(&buf[2], FS_EOC);
  }

  for (i = 0; i < _entrycount; i++)
  {
    struct fatshim_entry_s *e = &_entries[i];
    uint32_t first;
    uint32_t last;
    uint32_t c;

    if (e->nclusters == 0)
    {
      continue;
    }

    first = e->firstcluster;
    last = first + e->nclusters - 1;

    if (last < base || first >= base + FS_FATPERSECTOR)
    {
      continue;
    }

    for (c = (first > base) ? first : base;
         c <= last && c < base + FS_FATPERSECTOR;
         c++)
    {
      uint16_t value = (c == last) ? FS_EOC : (uint16_t)(c + 1);

      put_le16(&buf[(c - base) * 2], value);
    }
  }
}

// Writes one 32 byte directory slot for an entry. 'sub' selects which slot of
// that entry's set: long name slots come first, in reverse order, and the short
// entry is last.
static void emit_entry_slot(int index, unsigned int sub, uint8_t *out)
{
  struct fatshim_entry_s *e = &_entries[index];
  unsigned int nlfn = e->nslots - 1u;

  memset(out, 0, DIR_ENTRYSIZE);

  if (sub < nlfn)
  {
    // A long name slot. Sequence numbers count up towards the short entry,
    // so the slot stored first carries the highest number and the LAST flag.

    const char *name = entry_name(index);
    size_t namelen = strlen(name);
    unsigned int seq = nlfn - sub;
    size_t start = (size_t)(seq - 1) * LFN_CHARSPERSLOT;
    static const uint8_t charpos[LFN_CHARSPERSLOT] =
      {
        1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
      };
    unsigned int i;

    out[LFN_SEQ] = (uint8_t)(seq | ((sub == 0) ? LFN_LAST : 0));
    out[LFN_ATTR] = ATTR_LONGNAME;
    out[LFN_TYPE] = 0;
    out[LFN_CHKSUM] = fat_lfn_checksum(e->shortname);
    put_le16(&out[LFN_FSTCLUSLO], 0);

    for (i = 0; i < LFN_CHARSPERSLOT; i++)
    {
      size_t at = start + i;
      uint16_t ch;

      if (at < namelen)
      {
        ch = (uint16_t)(unsigned char)name[at];
      }
      else if (at == namelen)
      {
        ch = 0x0000;      // Terminator
      }
      else
      {
        ch = 0xffff;      // Padding
      }

      put_le16(&out[charpos[i]], ch);
    }

    return;
  }

  // The short entry.

  memcpy(&out[DIR_NAME], e->shortname, 11);

  // Files are marked read only because this whole view is. Directories are
  // not, since a read only directory bit confuses some hosts.

  out[DIR_ATTR] = e->isdir ? ATTR_DIRECTORY
                           : (ATTR_READONLY | ATTR_ARCHIVE);
  out[DIR_NTRES] = 0;

  put_le16(&out[DIR_CRTTIME], FS_FILETIME);
  put_le16(&out[DIR_CRTDATE], FS_FILEDATE);
  put_le16(&out[DIR_LSTACCDATE], FS_FILEDATE);
  put_le16(&out[DIR_FSTCLUSHI], 0);
  put_le16(&out[DIR_WRTTIME], FS_FILETIME);
  put_le16(&out[DIR_WRTDATE], FS_FILEDATE);
  put_le16(&out[DIR_FSTCLUSLO], e->firstcluster);
  put_le32(&out[DIR_FILESIZE], e->isdir ? 0 : e->size);
}

static void emit_dot_entry(uint8_t *out, bool dotdot, uint16_t cluster)
{
  memset(out, 0, DIR_ENTRYSIZE);
  memset(&out[DIR_NAME], ' ', 11);

  out[DIR_NAME] = '.';
  if (dotdot)
  {
    out[DIR_NAME + 1] = '.';
  }

  out[DIR_ATTR] = ATTR_DIRECTORY;
  put_le16(&out[DIR_CRTTIME], FS_FILETIME);
  put_le16(&out[DIR_CRTDATE], FS_FILEDATE);
  put_le16(&out[DIR_WRTTIME], FS_FILETIME);
  put_le16(&out[DIR_WRTDATE], FS_FILEDATE);
  put_le16(&out[DIR_FSTCLUSLO], cluster);
  put_le32(&out[DIR_FILESIZE], 0);
}

static void emit_volume_label(uint8_t *out)
{
  memset(out, 0, DIR_ENTRYSIZE);
  memset(&out[DIR_NAME], ' ', 11);
  memcpy(&out[DIR_NAME], FS_VOLUMELABEL, strlen(FS_VOLUMELABEL));
  out[DIR_ATTR] = ATTR_VOLUMEID;
}

// Synthesizes one 512 byte window of a directory's listing. 'dir' is a model
// index, or FS_ROOTPARENT for the root directory.
static void synth_dir_sector(int dir, uint32_t slotbase, uint8_t *buf)
{
  unsigned int k;

  memset(buf, 0, FS_SECTORSIZE);

  for (k = 0; k < FS_SLOTSPERSECTOR; k++)
  {
    uint32_t slot = slotbase + k;
    uint8_t *out = &buf[k * DIR_ENTRYSIZE];
    int i;

    if (dir == FS_ROOTPARENT)
    {
      if (slot == 0)
      {
        emit_volume_label(out);
        continue;
      }
    }
    else
    {
      if (slot == 0)
      {
        emit_dot_entry(out, false, _entries[dir].firstcluster);
        continue;
      }

      if (slot == 1)
      {
        int16_t up = _entries[dir].parent;

        // ".." in a directory whose parent is the root points at cluster 0,
        // which is how FAT names the fixed root directory.

        emit_dot_entry(out, true,
                       (up == FS_ROOTPARENT) ? 0
                                             : _entries[up].firstcluster);
        continue;
      }
    }

    for (i = 0; i < _entrycount; i++)
    {
      struct fatshim_entry_s *e = &_entries[i];

      if (e->parent != dir || e->nslots == 0)
      {
        continue;
      }

      if (slot >= e->dirslot && slot < (uint32_t)e->dirslot + e->nslots)
      {
        emit_entry_slot(i, (unsigned int)(slot - e->dirslot), out);
        break;
      }
    }
  }
}

// The one function below the model that touches the file system.
static int read_file_bytes(int index, uint32_t offset, uint8_t *buf,
                           size_t len)
{
  char path[FS_MAXPATH];
  ssize_t nread;
  off_t pos;
  int ret;

  if (_cachedentry != index || !_cachedopen)
  {
    if (_cachedopen)
    {
      file_close(&_cachedfile);
      _cachedopen = false;
      _cachedentry = -1;
    }

    ret = entry_path(index, path, sizeof(path));
    if (ret < 0)
    {
      return ret;
    }

    ret = file_open(&_cachedfile, path, O_RDONLY);
    if (ret < 0)
    {
      return ret;
    }

    _cachedopen = true;
    _cachedentry = index;
  }

  pos = file_seek(&_cachedfile, (off_t)offset, SEEK_SET);
  if (pos < 0)
  {
    return (int)pos;
  }

  nread = file_read(&_cachedfile, buf, len);
  if (nread < 0)
  {
    return (int)nread;
  }

  // A short read is normal at the end of a file. The rest of the sector is
  // whatever FAT says it is, which is to say undefined, so zero it.

  if ((size_t)nread < len)
  {
    memset(&buf[nread], 0, len - (size_t)nread);
  }

  return OK;
}

static void synth_data_sector(uint32_t lba, uint8_t *buf)
{
  uint32_t rel = lba - FS_DATASTART;
  uint32_t cluster = FS_FIRSTCLUSTER + (rel / FS_SECPERCLUS);
  uint32_t inclus = (rel % FS_SECPERCLUS) * FS_SECTORSIZE;
  int i;

  for (i = 0; i < _entrycount; i++)
  {
    struct fatshim_entry_s *e = &_entries[i];
    uint32_t byteoff;

    if (e->nclusters == 0 || cluster < e->firstcluster ||
        cluster >= (uint32_t)e->firstcluster + e->nclusters)
    {
      continue;
    }

    byteoff = ((cluster - e->firstcluster) * FS_CLUSTERSIZE) + inclus;

    if (e->isdir)
    {
      synth_dir_sector(i, byteoff / DIR_ENTRYSIZE, buf);
    }
    else if (read_file_bytes(i, byteoff, buf, FS_SECTORSIZE) < 0)
    {
      memset(buf, 0, FS_SECTORSIZE);
    }

    return;
  }

  // A cluster no entry owns. The FAT marks it free, so a host should never
  // ask, but answering with zeros is better than failing the transfer.

  memset(buf, 0, FS_SECTORSIZE);
}

static void synth_sector(uint32_t lba, uint8_t *buf)
{
  if (lba == 0)
  {
    synth_boot_sector(buf);
  }
  else if (lba < FS_ROOTSTART)
  {
    // Both FAT copies are identical, so the same generator serves either.

    synth_fat_sector((lba - FS_FATSTART) % FS_SECPERFAT, buf);
  }
  else if (lba < FS_DATASTART)
  {
    synth_dir_sector(FS_ROOTPARENT,
                     (lba - FS_ROOTSTART) * FS_SLOTSPERSECTOR, buf);
  }
  else
  {
    synth_data_sector(lba, buf);
  }
}

//===========================================================================
// Block operations
//===========================================================================

#ifndef FATSHIM_HOST_TEST

// Builds the model if it has not been built yet. Deferred to the first read
// because the mass storage LUN is bound before /meadow0 is mounted, so there is
// nothing to scan at bind time. A failure is retried rather than latched: the
// file system may simply not have finished mounting.
static int model_build_if_needed(void)
{
  int ret;

  if (_modelbuilt)
  {
    return OK;
  }

  ret = model_build();
  if (ret < 0)
  {
    _modelfailed = true;
    return ret;
  }

  _modelbuilt = true;
  _modelfailed = false;
  return OK;
}

static int fatshim_open(FAR struct inode *inode)
{
  UNUSED(inode);
  return OK;
}

static int fatshim_close(FAR struct inode *inode)
{
  UNUSED(inode);
  return OK;
}

static ssize_t fatshim_read(FAR struct inode *inode,
                            FAR unsigned char *buffer,
                            blkcnt_t start_sector, unsigned int nsectors)
{
  unsigned int i;
  int ret;

  UNUSED(inode);

  if (start_sector + nsectors > FS_TOTALSECTORS)
  {
    return -EINVAL;
  }

  ret = nxmutex_lock(&_lock);
  if (ret < 0)
  {
    return ret;
  }

  ret = model_build_if_needed();
  if (ret < 0)
  {
    nxmutex_unlock(&_lock);
    return ret;
  }

  for (i = 0; i < nsectors; i++)
  {
    synth_sector((uint32_t)start_sector + i,
                 &buffer[i * FS_SECTORSIZE]);
  }

  nxmutex_unlock(&_lock);
  return nsectors;
}

static int fatshim_geometry(FAR struct inode *inode,
                            FAR struct geometry *geometry)
{
  UNUSED(inode);

  if (geometry == NULL)
  {
    return -EINVAL;
  }

  memset(geometry, 0, sizeof(struct geometry));

  // Reported available unconditionally, and answerable before /meadow0 is
  // mounted, because usbmsc_bindlun() reads this once at bind time and refuses
  // a LUN that says otherwise. Whether the drive has a medium is decided by
  // whether the LUN is bound at all; see board_mscclassobject().

  geometry->geo_available = true;
  geometry->geo_mediachanged = false;
  geometry->geo_writeenabled = false;
  geometry->geo_nsectors = FS_TOTALSECTORS;
  geometry->geo_sectorsize = FS_SECTORSIZE;

  strlcpy(geometry->geo_model, "Meadow /meadow0",
          sizeof(geometry->geo_model));

  return OK;
}

// A write method has to exist even though the view is read-only, and leaving
// it out is not a shortcut to read-only behaviour: find_blockdriver() refuses
// with -EACCES when a driver has no write method and MS_RDONLY was not passed,
// and usbmsc_bindlun() calls open_blockdriver() with flags of zero. Omitting it
// makes the LUN impossible to bind, which fails composite_initialize() and
// takes the CDC/ACM interface down with it.
//
// What actually makes the medium read-only is the 'readonly' argument to
// usbmsc_bindlun(), which the board passes as MEADOW_USBMSC_LUN0_READONLY.
// That sets lun->readonly, which raises the MODE SENSE write protect bit and
// makes the SCSI layer refuse write commands before they ever reach here. The
// method below is the backstop, not the mechanism.

static ssize_t fatshim_write(FAR struct inode *inode,
                             FAR const unsigned char *buffer,
                             blkcnt_t start_sector, unsigned int nsectors)
{
  UNUSED(inode);
  UNUSED(buffer);
  UNUSED(start_sector);
  UNUSED(nsectors);

  // Unreachable while the LUN is bound read-only. Stage 4 is where host writes
  // become LittleFS operations.

  return -EROFS;
}

static const struct block_operations g_fatshim_bops =
{
  .open     = fatshim_open,
  .close    = fatshim_close,
  .read     = fatshim_read,
  .write    = fatshim_write,
  .geometry = fatshim_geometry,
  .ioctl    = NULL,
};

//===========================================================================
// Public functions
//===========================================================================

int hcom_nx_fatshim_initialize(const char *devpath)
{
  int ret;

  nxmutex_init(&_lock);

  _modelbuilt = false;
  _modelfailed = false;
  _entrycount = 0;
  _cachedentry = -1;
  _cachedopen = false;

  ret = register_blockdriver(devpath, &g_fatshim_bops, 0444, NULL);
  if (ret < 0)
  {
    syslog(LOG_ERR, "fatshim: register_blockdriver(%s) failed: %d\n",
           devpath, ret);
    return ret;
  }

  syslog(LOG_NOTICE,
         "fatshim: %s, FAT16 read-only view of %s, %d sectors, %d KB\n",
         devpath, MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME,
         FS_TOTALSECTORS, (FS_TOTALSECTORS * FS_SECTORSIZE) / 1024);

  return OK;
}

#endif  // !FATSHIM_HOST_TEST
