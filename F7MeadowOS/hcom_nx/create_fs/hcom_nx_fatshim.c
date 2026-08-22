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
#  include <unistd.h>

#  include <nuttx/fs/fs.h>
#  include <nuttx/fs/ioctl.h>
#  include <nuttx/clock.h>
#  include <nuttx/kmalloc.h>
#  include <nuttx/mutex.h>
#  include <nuttx/wqueue.h>

#  include <meadow/hcom_shared_common.h>
#  include <meadow/hcom_nx_fatshim.h>
#  include <meadow/meadow_storage_mode.h>
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
  uint8_t  gone;          // Applied a deletion; kept only so indices stay put
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
// Shadow state for host writes
//
// A host emits data clusters, FAT sectors and directory entries in whatever
// order suits it, so none of it can be applied to /meadow0 until it has
// finished. Until then the changes live here: the FAT and the fixed root
// directory area are shadowed in RAM, and cluster payloads are staged into a
// temporary file because their total size is not bounded by anything we
// control.
//
// The shadow also serves reads, and goes on doing so after the commit for the
// rest of the session. That is not laziness. Committing a new file to LittleFS
// says nothing about which clusters it would land on if the view were rebuilt,
// so a rebuild would move the file out from under a host that is still reading
// from where it put it. The host's own layout stays authoritative until the
// mode ends and a fresh view is built from scratch.
//===========================================================================

#ifdef CONFIG_MEADOW_USBMSC_FATSHIM_WRITE

// Raw FAT bytes as the host has them. Filled from the synthesized FAT the first
// time anything is written and authoritative from then on, which is what avoids
// having to reason about a partially written FAT.
static uint8_t _shadowfat[FS_SECPERFAT * FS_SECTORSIZE] locate_data(".dtcm");
static bool _shadowfatvalid;

// The fixed root directory area, with one dirty bit per sector. Subdirectories
// live in clusters and are staged like any other cluster; only the root needs
// its own store because it is not part of the data region.
static uint8_t _shadowroot[FS_ROOTDIRSECS * FS_SECTORSIZE] locate_data(".dtcm");
static uint32_t _shadowrootdirty;

// Assembly area for a cluster the host did not write, which has to be gathered
// from the synthesized view a sector at a time. Static rather than automatic
// because it is 4 KB and the commit already runs serialized under _lock.
static uint8_t _clusterbuf[FS_CLUSTERSIZE] locate_data(".dtcm");

// Slot number plus one of each cluster's staged copy; zero means not staged.
static uint16_t _stageslot[FS_FIRSTCLUSTER + FS_DATACLUSTERS]
                          locate_data(".dtcm");
static uint16_t _stagenext;

// One bit per cluster, set when the host writes to it and cleared by a commit.
//
// This is what tells a rename from a new file, and "has it ever been staged"
// will not do. A rename rewrites only a directory entry, so the data clusters
// are untouched - but if the file was created earlier in the same session its
// clusters are still in the staging buffer from that copy. Asking whether they
// were written *since the last commit* is the question that distinguishes the
// two, and getting it wrong costs a full rewrite of the file instead of a
// rename.
static uint8_t _stagewritten[(FS_FIRSTCLUSTER + FS_DATACLUSTERS + 7) / 8]
                            locate_data(".dtcm");

// Incoming cluster payloads. Taken from the user heap rather than staged to a
// file, which is where the external SDRAM lives: writing every host byte to
// flash twice, once to a staging file and once to its destination, costs both
// wear and time for no benefit. The runtime owns that memory when it is
// running, but storage mode is the runtime being disabled, so it is idle
// exactly when this is wanted.
//
// Allocated on the first write and held for the boot, since the shadow it
// belongs to keeps serving reads after the commit.
static uint8_t *_stagebuf;
static uint16_t _stagemaxslots;

static bool _hostwrote;

// Cleared by a commit, set by any write. A host that touches the volume without
// changing anything still moves the settling deadline, so this keeps a commit
// from re-running over a shadow nothing has altered.
static bool _writesincecommit;

static struct work_s _settlework;

// A staging file was used before this was moved into RAM. Any left behind by
// older firmware is removed rather than shown to a host.
#define FS_STAGEPATH  MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME "/.usbwrite.tmp"

#if FS_ROOTDIRSECS > 32
#  error "_shadowrootdirty has one bit per root directory sector"
#endif

// Defined with the rest of the staging code, but needed by model_build().
static void stage_release(void);

#endif  // CONFIG_MEADOW_USBMSC_FATSHIM_WRITE

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

#ifdef CONFIG_MEADOW_USBMSC_FATSHIM_WRITE
  // A staging file left behind by an earlier session describes clusters that
  // no longer mean anything, since the view is about to be rebuilt from
  // scratch. Discard it before the scan so it is neither used nor listed.

  stage_release();
  memset(_stageslot, 0, sizeof(_stageslot));
  _stagenext = 0;
  _shadowfatvalid = false;
  _shadowrootdirty = 0;
  _hostwrote = false;
  _writesincecommit = false;
  memset(_stagewritten, 0, sizeof(_stagewritten));
#endif

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

    if (e->nclusters == 0 || e->gone)
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

  // Deliberately NOT setting ATTR_READONLY on files. That attribute describes
  // the file rather than the volume, so it travels with a copy: a host that
  // reads a file off this volume would land a locked copy on its own disk,
  // which the user then has to unlock one file at a time.
  //
  // Nothing is lost by leaving it off. The volume is read-only because
  // lun->readonly raises the MODE SENSE write protect bit, which is what makes
  // the host mount read-only and refuse writes. Directory entry attributes
  // have no part in that.

  out[DIR_ATTR] = e->isdir ? ATTR_DIRECTORY : ATTR_ARCHIVE;
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

      if (e->parent != dir || e->nslots == 0 || e->gone)
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

    if (e->nclusters == 0 || e->gone || cluster < e->firstcluster ||
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
// Staging and shadow access
//===========================================================================

#ifdef CONFIG_MEADOW_USBMSC_FATSHIM_WRITE

static void stage_release(void)
{
  if (_stagebuf != NULL)
  {
    kumm_free(_stagebuf);
    _stagebuf = NULL;
  }

  _stagemaxslots = 0;
  _stagenext = 0;

  // Remove a staging file from firmware that predates RAM staging, so it can
  // neither be listed nor waste space.

  nx_unlink(FS_STAGEPATH);
}

static int stage_alloc(void)
{
  size_t bytes;

  if (_stagebuf != NULL)
  {
    return OK;
  }

  _stagemaxslots = (uint16_t)((CONFIG_MEADOW_USBMSC_FATSHIM_STAGE_KB * 1024ul)
                              / FS_CLUSTERSIZE);

  if (_stagemaxslots > FS_DATACLUSTERS)
  {
    _stagemaxslots = FS_DATACLUSTERS;
  }

  bytes = (size_t)_stagemaxslots * FS_CLUSTERSIZE;

  // Zeroed, so a cluster whose sectors do not all arrive still reads as
  // something defined rather than as whatever was in the heap.

  _stagebuf = kumm_zalloc(bytes);
  if (_stagebuf == NULL)
  {
    syslog(LOG_ERR, "fatshim: no memory for a %u KB staging buffer\n",
           (unsigned int)(bytes / 1024));
    _stagemaxslots = 0;
    return -ENOMEM;
  }

  syslog(LOG_NOTICE, "fatshim: staging %u KB, %u clusters\n",
         (unsigned int)(bytes / 1024), (unsigned int)_stagemaxslots);

  _stagenext = 0;
  return OK;
}

// Offset of a cluster's staged copy within the buffer, staging it if asked to.
static int stage_offset(uint32_t cluster, bool create, off_t *offset)
{
  int ret;

  if (cluster < FS_FIRSTCLUSTER ||
      cluster >= FS_FIRSTCLUSTER + FS_DATACLUSTERS)
  {
    return -EINVAL;
  }

  if (_stageslot[cluster] == 0)
  {
    if (!create)
    {
      return -ENOENT;
    }

    ret = stage_alloc();
    if (ret < 0)
    {
      return ret;
    }

    if (_stagenext >= _stagemaxslots)
    {
      // More in flight than the budget allows. The host sees a write error,
      // which is at least honest; nothing is committed from a partial copy.

      syslog(LOG_ERR, "fatshim: staging full at %u clusters\n",
             (unsigned int)_stagemaxslots);
      return -ENOSPC;
    }

    _stageslot[cluster] = (uint16_t)(_stagenext + 1);
    _stagenext++;
  }

  // Callers that only want to know whether a cluster was staged pass NULL.

  if (offset != NULL)
  {
    *offset = (off_t)(_stageslot[cluster] - 1) * FS_CLUSTERSIZE;
  }

  return OK;
}

static int stage_rw(off_t offset, uint8_t *buf, size_t len, bool write)
{
  if (_stagebuf == NULL ||
      (size_t)offset + len > (size_t)_stagemaxslots * FS_CLUSTERSIZE)
  {
    return -EINVAL;
  }

  if (write)
  {
    memcpy(&_stagebuf[offset], buf, len);
  }
  else
  {
    memcpy(buf, &_stagebuf[offset], len);
  }

  return OK;
}

// Fills the shadow FAT from the synthesized one, once. Taking a complete copy
// up front is what removes any need to reason about a half written FAT: from
// here on the shadow is simply what the FAT is.
static void shadowfat_init(void)
{
  unsigned int i;

  if (_shadowfatvalid)
  {
    return;
  }

  for (i = 0; i < FS_SECPERFAT; i++)
  {
    synth_fat_sector(i, &_shadowfat[i * FS_SECTORSIZE]);
  }

  _shadowfatvalid = true;
}

static void cluster_mark_written(uint32_t cluster)
{
  if (cluster < FS_FIRSTCLUSTER + FS_DATACLUSTERS)
  {
    _stagewritten[cluster / 8] |= (uint8_t)(1u << (cluster % 8));
  }
}

static bool cluster_written_since_commit(uint32_t cluster)
{
  if (cluster >= FS_FIRSTCLUSTER + FS_DATACLUSTERS)
  {
    return false;
  }

  return (_stagewritten[cluster / 8] & (1u << (cluster % 8))) != 0;
}

static uint16_t shadowfat_get(uint32_t cluster)
{
  if (cluster >= FS_FIRSTCLUSTER + FS_DATACLUSTERS)
  {
    return FS_EOC;
  }

  return (uint16_t)(_shadowfat[cluster * 2] |
                    ((uint16_t)_shadowfat[cluster * 2 + 1] << 8));
}

#endif  // CONFIG_MEADOW_USBMSC_FATSHIM_WRITE

//===========================================================================
// Commit: turning what the host wrote back into LittleFS operations
//===========================================================================

#ifdef CONFIG_MEADOW_USBMSC_FATSHIM_WRITE

// Defined below, with the rest of the sector plumbing.
static int read_sector(uint32_t lba, uint8_t *buf);

// Names a host creates for its own bookkeeping. They are dropped rather than
// written through, because they say nothing about what the user asked for and
// would otherwise accumulate on the device.
static const char *g_hostjunk[] =
{
  ".fseventsd", ".Spotlight-V100", ".Trashes", ".TemporaryItems",
  ".DS_Store", ".apdisk", "System Volume Information", "$RECYCLE.BIN",
  "desktop.ini", "indexerVolumeGuid", "WPSettings.dat", NULL
};

static bool name_is_hostjunk(const char *name)
{
  int i;

  // Anything AppleDouble is a resource fork companion, never a real file.

  if (name[0] == '.' && name[1] == '_')
  {
    return true;
  }

  for (i = 0; g_hostjunk[i] != NULL; i++)
  {
    if (strcmp(name, g_hostjunk[i]) == 0)
    {
      return true;
    }
  }

  return false;
}

// Follows the shadow FAT 'steps' links along from 'cluster'.
static uint32_t chain_advance(uint32_t cluster, unsigned int steps)
{
  while (steps-- > 0)
  {
    if (cluster < FS_FIRSTCLUSTER ||
        cluster >= FS_FIRSTCLUSTER + FS_DATACLUSTERS)
    {
      return 0;
    }

    cluster = shadowfat_get(cluster);
  }

  return cluster;
}

// Reads one 32 byte directory slot. 'dircluster' is the first cluster of the
// directory, or 0 for the root, whose entries live in the fixed area rather
// than in the data region.
static int dir_slot(uint32_t dircluster, uint32_t index, uint8_t *out)
{
  uint8_t sector[FS_SECTORSIZE];
  uint32_t lba;
  int ret;

  if (dircluster == 0)
  {
    uint32_t sec = index / FS_SLOTSPERSECTOR;

    if (sec >= FS_ROOTDIRSECS)
    {
      return -ENOENT;
    }

    lba = FS_ROOTSTART + sec;
  }
  else
  {
    const uint32_t slotsperclus = FS_CLUSTERSIZE / DIR_ENTRYSIZE;
    uint32_t cluster = chain_advance(dircluster, index / slotsperclus);
    uint32_t within = index % slotsperclus;

    if (cluster < FS_FIRSTCLUSTER ||
        cluster >= FS_FIRSTCLUSTER + FS_DATACLUSTERS)
    {
      return -ENOENT;
    }

    lba = FS_DATASTART + ((cluster - FS_FIRSTCLUSTER) * FS_SECPERCLUS) +
          (within / FS_SLOTSPERSECTOR);
  }

  ret = read_sector(lba, sector);
  if (ret < 0)
  {
    return ret;
  }

  memcpy(out, &sector[(index % FS_SLOTSPERSECTOR) * DIR_ENTRYSIZE],
         DIR_ENTRYSIZE);
  return OK;
}

// Pulls the 13 characters a long name slot carries into their place in 'name'.
static void lfn_gather(const uint8_t *slot, char *name, size_t namelen)
{
  static const uint8_t charpos[LFN_CHARSPERSLOT] =
    {
      1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30
    };
  unsigned int seq = slot[LFN_SEQ] & 0x3f;
  size_t base;
  unsigned int i;

  if (seq == 0)
  {
    return;
  }

  base = (size_t)(seq - 1) * LFN_CHARSPERSLOT;

  for (i = 0; i < LFN_CHARSPERSLOT; i++)
  {
    uint16_t ch = (uint16_t)(slot[charpos[i]] |
                             ((uint16_t)slot[charpos[i] + 1] << 8));
    size_t at = base + i;

    if (ch == 0x0000 || ch == 0xffff || at >= namelen - 1)
    {
      continue;
    }

    // Anything outside 7 bit ASCII cannot be represented in the name pool, so
    // it is replaced rather than truncating the name at that point.

    name[at] = (ch < 0x80) ? (char)ch : '_';
  }
}

// Turns a padded 8.3 short entry back into a filename.
static void shortname_to_name(const uint8_t *slot, char *out)
{
  int o = 0;
  int i;

  for (i = 0; i < 8 && slot[i] != ' '; i++)
  {
    out[o++] = (char)slot[i];
  }

  if (slot[8] != ' ')
  {
    out[o++] = '.';

    for (i = 8; i < 11 && slot[i] != ' '; i++)
    {
      out[o++] = (char)slot[i];
    }
  }

  out[o] = '\0';
}

struct hostent_s
{
  char name[FS_MAXNAME + 2];
  uint32_t firstcluster;
  uint32_t size;
  bool isdir;
};

// Reads the next real entry out of a directory, starting at *index and
// advancing it. Long name slots are gathered onto the short entry that follows
// them, which is the order FAT stores them in.
static int dir_next(uint32_t dircluster, uint32_t *index,
                    struct hostent_s *out)
{
  char lfn[FS_MAXNAME + 2];
  bool havelfn = false;
  uint8_t slot[DIR_ENTRYSIZE];
  int ret;

  memset(lfn, 0, sizeof(lfn));

  for (; ; (*index)++)
  {
    ret = dir_slot(dircluster, *index, slot);
    if (ret < 0)
    {
      return ret;
    }

    if (slot[0] == 0x00)
    {
      return -ENOENT;       // End of the directory
    }

    if (slot[0] == 0xe5)
    {
      havelfn = false;      // Deleted; any long name before it is void
      memset(lfn, 0, sizeof(lfn));
      continue;
    }

    if ((slot[DIR_ATTR] & ATTR_LONGNAME) == ATTR_LONGNAME)
    {
      lfn_gather(slot, lfn, sizeof(lfn));
      havelfn = true;
      continue;
    }

    if ((slot[DIR_ATTR] & ATTR_VOLUMEID) != 0)
    {
      havelfn = false;
      continue;
    }

    if (slot[DIR_NAME] == '.')
    {
      havelfn = false;      // "." and ".."
      continue;
    }

    memset(out, 0, sizeof(*out));

    if (havelfn && lfn[0] != '\0')
    {
      strncpy(out->name, lfn, sizeof(out->name) - 1);
    }
    else
    {
      shortname_to_name(&slot[DIR_NAME], out->name);
    }

    out->firstcluster = (uint32_t)(slot[DIR_FSTCLUSLO] |
                                   ((uint32_t)slot[DIR_FSTCLUSLO + 1] << 8));
    out->size = (uint32_t)(slot[DIR_FILESIZE] |
                            ((uint32_t)slot[DIR_FILESIZE + 1] << 8) |
                            ((uint32_t)slot[DIR_FILESIZE + 2] << 16) |
                            ((uint32_t)slot[DIR_FILESIZE + 3] << 24));
    out->isdir = (slot[DIR_ATTR] & ATTR_DIRECTORY) != 0;

    (*index)++;
    return OK;
  }
}

#endif  // CONFIG_MEADOW_USBMSC_FATSHIM_WRITE

//===========================================================================
// Sector read and write
//===========================================================================

static int read_sector(uint32_t lba, uint8_t *buf)
{
#ifdef CONFIG_MEADOW_USBMSC_FATSHIM_WRITE
  // Before the host has written anything the synthesized view is the only
  // truth, and afterwards the shadow takes precedence wherever it has an
  // opinion.

  if (_hostwrote)
  {
    if (lba >= FS_FATSTART && lba < FS_ROOTSTART)
    {
      unsigned int fatsec = (lba - FS_FATSTART) % FS_SECPERFAT;

      if (_shadowfatvalid)
      {
        memcpy(buf, &_shadowfat[fatsec * FS_SECTORSIZE], FS_SECTORSIZE);
        return OK;
      }
    }
    else if (lba >= FS_ROOTSTART && lba < FS_DATASTART)
    {
      unsigned int rootsec = lba - FS_ROOTSTART;

      if ((_shadowrootdirty & (1u << rootsec)) != 0)
      {
        memcpy(buf, &_shadowroot[rootsec * FS_SECTORSIZE], FS_SECTORSIZE);
        return OK;
      }
    }
    else if (lba >= FS_DATASTART)
    {
      uint32_t rel = lba - FS_DATASTART;
      uint32_t cluster = FS_FIRSTCLUSTER + (rel / FS_SECPERCLUS);
      uint32_t inclus = (rel % FS_SECPERCLUS) * FS_SECTORSIZE;
      off_t offset;

      if (stage_offset(cluster, false, &offset) == OK)
      {
        return stage_rw(offset + inclus, buf, FS_SECTORSIZE, false);
      }
    }
  }
#endif

  synth_sector(lba, buf);
  return OK;
}

#ifdef CONFIG_MEADOW_USBMSC_FATSHIM_WRITE

static int write_sector(uint32_t lba, const uint8_t *buf)
{
  if (lba == 0)
  {
    // Nothing good can come of a host rewriting the boot sector, and the
    // geometry it describes is not ours to change. Accepted and dropped, so
    // the host does not see a write error.

    return OK;
  }

  _hostwrote = true;
  _writesincecommit = true;

  if (lba < FS_ROOTSTART)
  {
    unsigned int fatsec = (lba - FS_FATSTART) % FS_SECPERFAT;

    shadowfat_init();
    memcpy(&_shadowfat[fatsec * FS_SECTORSIZE], buf, FS_SECTORSIZE);
    return OK;
  }

  if (lba < FS_DATASTART)
  {
    unsigned int rootsec = lba - FS_ROOTSTART;

    // Whole sectors arrive, mass storage having no smaller unit, so there is
    // nothing to merge with the synthesized version underneath.

    memcpy(&_shadowroot[rootsec * FS_SECTORSIZE], buf, FS_SECTORSIZE);
    _shadowrootdirty |= (1u << rootsec);
    return OK;
  }

  {
    uint32_t rel = lba - FS_DATASTART;
    uint32_t cluster = FS_FIRSTCLUSTER + (rel / FS_SECPERCLUS);
    uint32_t inclus = (rel % FS_SECPERCLUS) * FS_SECTORSIZE;
    off_t offset;
    int ret;

    ret = stage_offset(cluster, true, &offset);
    if (ret < 0)
    {
      return ret;
    }

    cluster_mark_written(cluster);

    return stage_rw(offset + inclus, (uint8_t *)buf, FS_SECTORSIZE, true);
  }
}

#endif  // CONFIG_MEADOW_USBMSC_FATSHIM_WRITE

//===========================================================================
// Commit: applying the host's changes
//===========================================================================

#ifdef CONFIG_MEADOW_USBMSC_FATSHIM_WRITE

#define FS_MAXCOMMITDIRS 16

struct commitdir_s
{
  uint32_t cluster;     // First cluster, or 0 for the root directory
  int model;            // Model index, FS_ROOTPARENT for root, -2 if new
  char path[FS_MAXPATH];
};

static struct commitdir_s _commitdirs[FS_MAXCOMMITDIRS] locate_data(".dtcm");
static uint8_t _modelseen[(FS_MAXENTRIES + 7) / 8] locate_data(".dtcm");

#define FS_DIR_NEW (-2)

static void seen_mark(int index)
{
  _modelseen[index / 8] |= (uint8_t)(1u << (index % 8));
}

static bool seen_test(int index)
{
  return (_modelseen[index / 8] & (1u << (index % 8))) != 0;
}

// Finds a model entry by parent and name.
static int model_find(int parent, const char *name)
{
  int i;

  for (i = 0; i < _entrycount; i++)
  {
    if (_entries[i].parent == parent && !_entries[i].gone &&
        strcmp(entry_name(i), name) == 0)
    {
      return i;
    }
  }

  return -1;
}

// Finds a model entry occupying a given first cluster. Used to recognise a
// rename: the host rewrites the directory entry and touches no data, so the
// clusters are not staged and the bytes are still where they always were.
static int model_find_by_cluster(uint32_t cluster)
{
  int i;

  if (cluster == 0)
  {
    return -1;
  }

  for (i = 0; i < _entrycount; i++)
  {
    if (_entries[i].firstcluster == cluster && _entries[i].nclusters > 0 &&
        !_entries[i].gone)
    {
      return i;
    }
  }

  return -1;
}

// After a change has been applied, the model has to be brought into line with
// it. Without this the next commit diffs the same shadow against the same stale
// model and applies everything a second time.
//
// The updates are made in place, using the clusters the host chose. Nothing is
// reassigned and model_assign_clusters() is not re-run, because the host must go
// on seeing the layout it created.

static void model_note_created(int parent, const char *name, bool isdir,
                               uint32_t size, uint32_t firstcluster)
{
  int index = model_add_entry(name, parent, isdir, size);

  if (index < 0)
  {
    // No room to record it. The change itself succeeded; the cost is that a
    // later commit may repeat it.

    syslog(LOG_WARNING, "fatshim: cannot record %s, model full\n", name);
    return;
  }

  _entries[index].firstcluster = (uint16_t)firstcluster;
  _entries[index].nclusters =
      (uint16_t)((size + FS_CLUSTERSIZE - 1) / FS_CLUSTERSIZE);

  // nslots stays zero, which keeps this entry out of synthesized directory
  // listings. That is correct: the host wrote those directory sectors itself,
  // so they are served from the shadow.
}

static void model_note_replaced(int index, uint32_t size,
                                uint32_t firstcluster)
{
  _entries[index].size = size;
  _entries[index].firstcluster = (uint16_t)firstcluster;
  _entries[index].nclusters =
      (uint16_t)((size + FS_CLUSTERSIZE - 1) / FS_CLUSTERSIZE);
}

static void model_note_renamed(int index, const char *name)
{
  size_t namelen = strlen(name);

  if (_namepoolused + (int)namelen + 1 > FS_NAMEPOOL)
  {
    syslog(LOG_WARNING, "fatshim: cannot record rename to %s, pool full\n",
           name);
    return;
  }

  _entries[index].nameoff = (uint16_t)_namepoolused;
  strcpy(&_namepool[_namepoolused], name);
  _namepoolused += (int)namelen + 1;
}

static void model_note_gone(int index)
{
  // Retired rather than removed, so that every other entry's index stays
  // valid; parents are referenced by index.

  _entries[index].gone = 1;
}

// Writes a file to LittleFS by following its chain through the shadow FAT.
// Clusters the host wrote come from the staging file; clusters it left alone
// are still described by the model, so they are read back through the ordinary
// synthesized path.
static int commit_write_file(const char *path, uint32_t firstcluster,
                             uint32_t size)
{
  struct file f;
  uint32_t cluster = firstcluster;
  uint32_t remaining = size;
  int ret;

  // Logged before the open rather than after, so that a failure leaves a
  // "flushing" with no "complete" and an error between them, which is
  // unambiguous. The pair also brackets the write closely enough to measure
  // throughput against the byte count.

  syslog(LOG_NOTICE, "fatshim: flushing %s, %u bytes\n", path,
         (unsigned int)size);

  ret = file_open(&f, path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (ret < 0)
  {
    syslog(LOG_ERR, "fatshim: cannot create %s: %d\n", path, ret);
    return ret;
  }

  // A whole cluster per write, not a sector per write. The cost of a LittleFS
  // write turns out to be dominated by the call rather than by the byte count,
  // so 512 byte writes made a 3 MB file take 5926 of them at about 9 ms each.
  //
  // A staged cluster is already contiguous in RAM, so it is handed to the file
  // system directly and never copied.

  while (remaining > 0 && cluster >= FS_FIRSTCLUSTER &&
         cluster < FS_FIRSTCLUSTER + FS_DATACLUSTERS)
  {
    size_t n = (remaining > FS_CLUSTERSIZE) ? FS_CLUSTERSIZE : remaining;
    FAR const uint8_t *src;
    off_t offset;

    if (stage_offset(cluster, false, &offset) == OK)
    {
      src = &_stagebuf[offset];
    }
    else
    {
      // Not written by the host, so it still belongs to whatever the model
      // says and has to be gathered through the ordinary read path.

      int i;

      for (i = 0; i < FS_SECPERCLUS; i++)
      {
        ret = read_sector(FS_DATASTART +
                          ((cluster - FS_FIRSTCLUSTER) * FS_SECPERCLUS) + i,
                          &_clusterbuf[i * FS_SECTORSIZE]);
        if (ret < 0)
        {
          goto done;
        }
      }

      src = _clusterbuf;
    }

    if (file_write(&f, src, n) != (ssize_t)n)
    {
      ret = -EIO;
      goto done;
    }

    remaining -= n;
    cluster = shadowfat_get(cluster);
  }

done:
  file_close(&f);

  if (ret >= 0)
  {
    syslog(LOG_NOTICE, "fatshim: flushing %s complete\n", path);
  }

  if (ret >= 0 && remaining > 0)
  {
    // The chain ran out before the size was satisfied, which means the FAT and
    // the directory entry disagree. The file is left short rather than padded,
    // and said so, because guessing at the missing bytes would be worse.

    syslog(LOG_WARNING, "fatshim: %s short by %u bytes\n", path,
           (unsigned int)remaining);
  }

  return ret;
}

// Applies everything the host wrote. Called from the work queue once the host
// has been quiet, so the FAT and directory entries it left behind describe a
// complete file system rather than a half finished copy.
static int commit_apply(void)
{
  char childpath[FS_MAXPATH];
  struct hostent_s he;
  int ndirs = 1;
  int d;
  int i;
  int ret;
  int changes = 0;

  memset(_modelseen, 0, sizeof(_modelseen));

  _commitdirs[0].cluster = 0;
  _commitdirs[0].model = FS_ROOTPARENT;
  strlcpy(_commitdirs[0].path, MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME,
          sizeof(_commitdirs[0].path));

  for (d = 0; d < ndirs; d++)
  {
    struct commitdir_s *dir = &_commitdirs[d];
    uint32_t index = 0;

    while (dir_next(dir->cluster, &index, &he) == OK)
    {
      int found;

      if (name_is_hostjunk(he.name))
      {
        continue;
      }

      if (snprintf(childpath, sizeof(childpath), "%s/%s", dir->path,
                   he.name) >= (int)sizeof(childpath))
      {
        syslog(LOG_WARNING, "fatshim: path too long, %s skipped\n", he.name);
        continue;
      }

      found = (dir->model == FS_DIR_NEW) ? -1
                                         : model_find(dir->model, he.name);

      if (found >= 0)
      {
        seen_mark(found);
      }

      if (he.isdir)
      {
        if (found < 0)
        {
          ret = mkdir(childpath, 0777);
          if (ret < 0)
          {
            syslog(LOG_ERR, "fatshim: mkdir %s failed: %d\n", childpath,
                   ret);
            continue;
          }

          syslog(LOG_NOTICE, "fatshim: created directory %s\n", childpath);
          model_note_created(dir->model, he.name, true, 0, he.firstcluster);
          found = model_find(dir->model, he.name);
          if (found >= 0)
          {
            seen_mark(found);
          }

          changes++;
        }

        if (ndirs < FS_MAXCOMMITDIRS)
        {
          _commitdirs[ndirs].cluster = he.firstcluster;
          _commitdirs[ndirs].model = (found >= 0) ? found : FS_DIR_NEW;
          strlcpy(_commitdirs[ndirs].path, childpath,
                  sizeof(_commitdirs[ndirs].path));
          ndirs++;
        }
        else
        {
          syslog(LOG_WARNING, "fatshim: too many directories, %s not "
                              "descended into\n", childpath);
        }

        continue;
      }

      // An unchanged file: same name, same length, same starting cluster.

      if (found >= 0 && _entries[found].size == he.size &&
          _entries[found].firstcluster == he.firstcluster)
      {
        continue;
      }

      // A file whose clusters were never written is not new data. It is the
      // same bytes under a different name, so rename rather than copy; reading
      // it back through the staging file would find nothing there.

      // A new name whose data was not written in this round is the same bytes
      // under a different name. Copying it would be wrong as well as slow: for
      // a file created earlier in the session the clusters are still staged
      // from that copy, so "has it ever been staged" would misread this.

      if (found < 0 && he.firstcluster != 0 &&
          !cluster_written_since_commit(he.firstcluster))
      {
        int old = model_find_by_cluster(he.firstcluster);

        if (old >= 0 && !seen_test(old))
        {
          char oldpath[FS_MAXPATH];

          if (entry_path(old, oldpath, sizeof(oldpath)) == OK &&
              rename(oldpath, childpath) == 0)
          {
            seen_mark(old);
            model_note_renamed(old, he.name);
            syslog(LOG_NOTICE, "fatshim: renamed %s to %s\n", oldpath,
                   childpath);
            changes++;
            continue;
          }
        }
      }

      ret = commit_write_file(childpath, he.firstcluster, he.size);
      if (ret >= 0)
      {
        if (found >= 0)
        {
          model_note_replaced(found, he.size, he.firstcluster);
        }
        else
        {
          model_note_created(dir->model, he.name, false, he.size,
                             he.firstcluster);
          found = model_find(dir->model, he.name);
          if (found >= 0)
          {
            seen_mark(found);
          }
        }

        changes++;
      }
    }
  }

  // Anything the model had in a directory we walked, and the host no longer
  // lists there, has been deleted.

  for (i = 0; i < _entrycount; i++)
  {
    char path[FS_MAXPATH];
    bool walked = false;

    if (seen_test(i) || _entries[i].gone)
    {
      continue;
    }

    for (d = 0; d < ndirs; d++)
    {
      if (_commitdirs[d].model == _entries[i].parent)
      {
        walked = true;
        break;
      }
    }

    if (!walked || entry_path(i, path, sizeof(path)) < 0)
    {
      continue;
    }

    ret = _entries[i].isdir ? rmdir(path) : unlink(path);
    if (ret < 0)
    {
      syslog(LOG_WARNING, "fatshim: cannot remove %s: %d\n", path, ret);
      continue;
    }

    model_note_gone(i);
    syslog(LOG_NOTICE, "fatshim: removed %s\n", path);
    changes++;
  }

  // Start a new epoch. Everything staged so far has been accounted for, so a
  // cluster not written from here on is data that is already on the device.

  memset(_stagewritten, 0, sizeof(_stagewritten));

  syslog(LOG_NOTICE, "fatshim: commit applied %d changes\n", changes);
  return OK;
}

//===========================================================================
// Settling
//
// There is no point at which a host announces that it has finished writing, so
// the only available signal is that it has stopped. Every write pushes the
// deadline out; when it finally expires the FAT and directory entries left
// behind describe a complete file system and can be reconciled.
//===========================================================================

static void settle_worker(FAR void *arg)
{
  UNUSED(arg);

  if (nxmutex_lock(&_lock) < 0)
  {
    return;
  }

  if (_hostwrote && _modelbuilt && _writesincecommit)
  {
    _writesincecommit = false;
    commit_apply();

    // The staging file and the shadow are deliberately kept. Reads carry on
    // being served from them for the rest of the session, because committing
    // says nothing about where a rebuilt view would place these files and the
    // host has to go on seeing its own layout.
  }

  nxmutex_unlock(&_lock);
}

// Called with _lock held. work_queue only schedules, so there is no risk of
// the worker running inline and deadlocking on the lock we are holding.
static void settle_schedule(void)
{
  work_cancel(LPWORK, &_settlework);
  work_queue(LPWORK, &_settlework, settle_worker, NULL,
             MSEC2TICK(CONFIG_MEADOW_USBMSC_FATSHIM_SETTLE_MS));
}

#endif  // CONFIG_MEADOW_USBMSC_FATSHIM_WRITE

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

  // The model is only ever built because a host read from the volume, so this
  // is the moment a host takes an interest in the file system. HCOM uses it to
  // refuse writes that would invalidate the view the host has been given.

  meadow_storage_mode_set_host_engaged();

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
    ret = read_sector((uint32_t)start_sector + i,
                      &buffer[i * FS_SECTORSIZE]);
    if (ret < 0)
    {
      nxmutex_unlock(&_lock);
      return ret;
    }
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
#ifdef CONFIG_MEADOW_USBMSC_FATSHIM_WRITE
  geometry->geo_writeenabled = true;
#else
  geometry->geo_writeenabled = false;
#endif
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
#ifdef CONFIG_MEADOW_USBMSC_FATSHIM_WRITE
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

  // The model has to exist before a write can be interpreted, since the shadow
  // FAT is seeded from the synthesized one and the diff is against the model.

  ret = model_build_if_needed();
  if (ret < 0)
  {
    nxmutex_unlock(&_lock);
    return ret;
  }

  for (i = 0; i < nsectors; i++)
  {
    ret = write_sector((uint32_t)start_sector + i,
                       &buffer[i * FS_SECTORSIZE]);
    if (ret < 0)
    {
      break;
    }
  }

  if (ret >= 0)
  {
    settle_schedule();
  }

  nxmutex_unlock(&_lock);
  return (ret < 0) ? (ssize_t)ret : (ssize_t)nsectors;
#else
  UNUSED(inode);
  UNUSED(buffer);
  UNUSED(start_sector);
  UNUSED(nsectors);

  // Unreachable while the LUN is bound read-only, and the method exists only
  // because find_blockdriver() refuses a driver without one.

  return -EROFS;
#endif
}

// BIOC_FLUSH arrives when the host issues SYNCHRONIZE CACHE, which it does
// after significant writes and before an eject. Committing here rather than
// waiting out the settling window is what makes ejecting mean what a user
// expects: that the data is on the device.
//
// Without it a host write is durable only after the settling window plus
// however long the flash write takes, and unplugging inside that window loses
// the copy silently even though the host reported it finished.
static int fatshim_ioctl(FAR struct inode *inode, int cmd, unsigned long arg)
{
#ifdef CONFIG_MEADOW_USBMSC_FATSHIM_WRITE
  int ret;

  UNUSED(inode);
  UNUSED(arg);

  if (cmd != BIOC_FLUSH)
  {
    return -ENOTTY;
  }

  // Runs on the SCSI thread, so the host's command blocks until the commit
  // finishes. That is the point: it asked for durability.

  work_cancel(LPWORK, &_settlework);

  ret = nxmutex_lock(&_lock);
  if (ret < 0)
  {
    return ret;
  }

  if (_hostwrote && _modelbuilt && _writesincecommit)
  {
    _writesincecommit = false;
    syslog(LOG_NOTICE, "fatshim: host asked for a flush\n");
    commit_apply();
  }

  nxmutex_unlock(&_lock);
  return OK;
#else
  UNUSED(inode);
  UNUSED(cmd);
  UNUSED(arg);
  return -ENOTTY;
#endif
}

static const struct block_operations g_fatshim_bops =
{
  .open     = fatshim_open,
  .close    = fatshim_close,
  .read     = fatshim_read,
  .write    = fatshim_write,
  .geometry = fatshim_geometry,
  .ioctl    = fatshim_ioctl,
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
