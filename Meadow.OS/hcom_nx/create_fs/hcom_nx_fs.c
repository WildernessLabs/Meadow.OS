/****************************************************************************
 * \configs\stm32f777zit6-meadow\src\hcom_nx\create_fs\hcom_nx_fs.c
 * 
 *   Copyright (C) 2019 - 2024 Wilderness Labs. All rights reserved.
 *   Author:  Wilderness Labs
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

// This modules contains code to create the files system, partitions and all

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_nx_common.h"

#if !defined CONFIG_FS_LITTLEFS
#warning "LittleFS is not configured. No file system can exist"
#endif

#include <errno.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <nuttx/mtd/mtd.h>
#include <dirent.h>
#include <meadow/meadow_hw_version.h>

#pragma message "(--) hcom_nx_fs.c"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
// Show debugging information set to 1
#define HCOM_NX_FS_INCLUDE_DEBUG_LOGGING 1

// The Primary partition size can only be determined after starting Meadow.
// This is because we must support 2 different flash chips with different
// sizes.
#define MEADOW_FS_PARTITION_INIT_SIZE_FLAG (0xffffffff) // Used to identify
#define MEADOW_FS_PARTITION_OF_FS (0)

// Already defined in /Meadow.OS/Meadow.OS/include/meadow/hcom_shared_common.h
// Here for reference
// #define HCOM_NX_FS_MONO_RAW_PARTITION_SIZE 0x300000  // 3MB Mono partition
// #define HCOM_NX_FS_OTA_RESERVED_SPACE 0x200000       // 2MB update space
// #define HCOM_NX_FS_NUTTX_UPDATE_SIZE  0x1C0000       // 2MB-256KB=1,835,008
#define MEADOW_FS_PARTITION_256K_PART_SIZE (256 * 1024) // 0x40000 (262,144)
#define MEADOW_FS_PARTITION_TOTAL_COUNT (5)


// This structure stores the information about each partition
struct partition_info
{
  struct mtd_dev_s *mtdPart;
  bool isPartMounted;
  off_t partPageOffset;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/
static char *thisFile = __FILE__;

static FAR struct partition_info _partInfo[MEADOW_FS_PARTITION_TOTAL_COUNT];
static uint32_t _pagesPerEraSector;

// The order in this array determines to partition number. With partition the
// partition number (0-n) is the same as the index in this array.
// However, the code will place these in flash memory starting with the last
// entry, this is to match the legacy flash layout.
static uint32_t _partitionSizes[] = { MEADOW_FS_PARTITION_INIT_SIZE_FLAG,  // 0
                                      HCOM_NX_FS_MONO_RAW_PARTITION_SIZE,  // 1
                                      HCOM_NX_FS_NUTTX_UPDATE_SIZE,        // 2
                                      HCOM_NX_FS_OTA_RESERVED_SPACE,       // 3
                                      MEADOW_FS_PARTITION_256K_PART_SIZE}; // 4

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int hcom_fs_create_partitions_initialize_and_mount_fs(FAR struct mtd_dev_s *mtd);
static int hcom_fs_init_partitions(FAR struct mtd_dev_s *mtd);
static int hcom_nx_register_external_flash_mtd(FAR struct mtd_dev_s *master_flash_mtd);

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// Entry point for initialization of file system.
// Called by hcom_nx_startup_mgr.c at startup to create low-level partitioning
// and file system.
int hcom_nx_create_fs_initialize(FAR struct mtd_dev_s *mtd)
{
  int ret;

  _pagesPerEraSector = 0;

  for(int i = 0; i < MEADOW_FS_PARTITION_TOTAL_COUNT; i++)
  {
    _partInfo[i].mtdPart = NULL;
    _partInfo[i].isPartMounted = false;
    _partInfo[i].partPageOffset = 0;
  }

#ifdef CONFIG_FS_LITTLEFS
  // This call registers the mtd
  ret = hcom_nx_register_external_flash_mtd(mtd);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-ERROR:LittleFS F/S init error:%d\n",
              thisFile, __LINE__, ret);
    return ret;
  }
#endif

#if !defined(CONFIG_HCOM_MTD_STRESS_TEST)
  ret = hcom_fs_create_partitions_initialize_and_mount_fs(mtd);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-F/S creation, error:%d\n", thisFile, __LINE__, ret);
    return ret;
  }
#endif

  return OK;
}

//=====================================================================
// This function will create partitions, initialize the file system, mount
// the partitions and if necessary format the partitions.
int hcom_fs_create_partitions_initialize_and_mount_fs(FAR struct mtd_dev_s *mtd)
{
  int ret;

  //------------------------------------------------------------------------
  syslog(LOG_INFO, "F/S creating %d partition%s\n",
            MEADOW_FS_PARTITION_TOTAL_COUNT, MEADOW_FS_PARTITION_TOTAL_COUNT > 1 ? "s" : "");

  // Create the partitions in the external flash
  ret = hcom_fs_init_partitions(mtd);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-partitioning FS:%d\n", thisFile, __LINE__, ret);
    return ret;
  }

  //------------------------------------------------------------------------
  syslog(LOG_DEBUG, "Partitioning complete. Init file system\n");

  // Initialize the file system
#if (defined CONFIG_FS_LITTLEFS && defined CONFIG_MTD_PARTITION)

  // File System only on 1 partition
  ret = hcom_nx_create_one_littlefs_file_dev(MEADOW_FS_PARTITION_OF_FS,
                    _partInfo[MEADOW_FS_PARTITION_OF_FS].mtdPart);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-LittleFS error:%d part:%d\n",
              thisFile, __LINE__, ret, MEADOW_FS_PARTITION_OF_FS);
    return ret;
  }
#endif

  //------------------------------------------------------------------------
  syslog(LOG_DEBUG, "F/S Part Init complete, mount & format\n");

#if defined(CONFIG_FS_LITTLEFS)
  ret = hcom_nx_create_littlefs_mount_format_file_dev(MEADOW_FS_PARTITION_OF_FS);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-fs mount failed '%s' to '%s' for type '%s', Part %d error:%d\n",
          thisFile, __LINE__, HCOM_NX_FILE_MOUNT_POINT_SOURCE, HCOM_FILE_MOUNT_POINT_TARGET,
          HCOM_NX_FILE_MOUNT_FILE_SYS_TYPE, MEADOW_FS_PARTITION_OF_FS, ret);
  }
#endif

  syslog(LOG_NOTICE, "F/S creation complete\n");
  return OK;
}

//==============================================================================
// This method is called from littlefs to mount either a partition or the
// only partition in the file system 
int hcom_nx_create_fs_mount(const char *sourceDevice, const char *targetDevice,
                                        const char *fileSystemType, uint32_t partitionId,
                                        const char *mountCommand)
{
  int ret;
  char *finalSourceName = malloc(HCOM_NX_MAX_PATH_AND_FILE_BUFF_LENGTH);
  if(finalSourceName == NULL)
  {
    syslog(LOG_ERR, "%s@%d-malloc returned NULL\n", thisFile, __LINE__);
    return -ENOMEM;
  }
  
  char *fullMountPtName = malloc(HCOM_NX_MAX_PATH_AND_FILE_BUFF_LENGTH);
  if(fullMountPtName == NULL)
  {
    syslog(LOG_ERR, "%s@%d-malloc returned NULL\n", thisFile, __LINE__);
    free(finalSourceName);
    return -ENOMEM;
  }

#ifdef CONFIG_MTD_PARTITION
  // e.g. /dev/smart0 or dev/little0
  snprintf_chk(finalSourceName, HCOM_NX_MAX_PATH_AND_FILE_BUFF_LENGTH, "%s0p%d", sourceDevice, partitionId);

  // e.g. /meadow0
  snprintf_chk(fullMountPtName, HCOM_NX_MAX_PATH_AND_FILE_BUFF_LENGTH, "%s%d", targetDevice, partitionId);

 #if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "Attempt to mount partition %ld as '%s' to '%s' type '%s'\n",
           partitionId, finalSourceName, fullMountPtName, fileSystemType);
 #endif

#else  // #ifdef CONFIG_MTD_PARTITION

  // e.g. mount("/dev/little", "/meadow", "littlefs", 0, NULL);
  strcpy(finalSourceName, sourceDevice);
  strcpy(fullMountPtName, targetDevice);

  syslog(LOG_INFO, "Mounting '%s' to '%s' type '%s'\n",
           finalSourceName, fullMountPtName, fileSystemType);
#endif  // #ifdef CONFIG_MTD_PARTITION

 #if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "%s@%d-Mounting part:%ld as '%s' to '%s' type '%s', cmd '%s'\n",
        thisFile, __LINE__, partitionId, finalSourceName,
        fullMountPtName, fileSystemType, mountCommand);
 #endif

  // e.g. mount("/dev/ram0", "/mnt", "vfat", 0, NULL);  // Needs backing block device
  // e.g. mount(NULL, "/mnt", "nxffs", 0, NULL);        // When no backing block device
  ret = mount(finalSourceName, fullMountPtName, fileSystemType, 0, mountCommand);
  if (ret < 0)
  {
    // The mount function puts the error code into errno
    int errnumb = get_errno();
    // Return to the specific file system code to determine if formatting needed
    free(finalSourceName);
    free(fullMountPtName);
    return -errnumb;
  }

  _partInfo[partitionId].isPartMounted = true;
  
 #if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "%s@%d-Mount successful\n", thisFile, __LINE__);
 #endif
        
  free(finalSourceName);
  free(fullMountPtName);

  return OK;
}

//=====================================================================
//
bool hcom_nx_fs_is_mounted(uint32_t partitionId)
{
  return (_partInfo[partitionId].isPartMounted);
}

//=====================================================================
// Used to implement RenewFileSys
int hcom_nx_fs_1st_erase_sector_of_partition(uint32_t partitionId)
{
  return (_partInfo[partitionId].partPageOffset / _pagesPerEraSector);
}

//=====================================================================
// It will register the mtd driver.
int hcom_nx_register_external_flash_mtd(FAR struct mtd_dev_s *master_flash_mtd)
{
  int ret;

#if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "%s@%d-Register master mtd\n", thisFile, __LINE__);
#endif

  char *finalSourceName = malloc(HCOM_NX_MAX_PATH_AND_FILE_BUFF_LENGTH);
  if(finalSourceName == NULL)
  {
    syslog(LOG_ERR, "%s@%d-malloc returned NULL\n", thisFile, __LINE__);
    return -ENOMEM;
  }

#ifdef CONFIG_MTD_PARTITION
  // Register the MTD driver so that it can be accessed from the VFS
  // master mtd becomes '/dev/extflash
  snprintf_chk(finalSourceName, HCOM_NX_MAX_PATH_AND_FILE_BUFF_LENGTH,
            "%s0", HCOM_NX_FILE_MOUNT_POINT_SOURCE);
#else
  // Since there are no partitions we register as '/dev/little'
  strncpy(finalSourceName, HCOM_NX_FILE_MOUNT_POINT_SOURCE, HCOM_NX_MAX_PATH_AND_FILE_BUFF_LENGTH);
#endif

  ret = register_mtddriver(finalSourceName, master_flash_mtd, 0755, NULL);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-register_mtddriver() ret:%d, errno:%d\n",
            thisFile, __LINE__, ret, errno);
    free(finalSourceName);
    return ret;
  }

  free(finalSourceName);
  return OK;
}


//======================================================================
// Partitions the entire flash chip with the defined partition boundaries.
int hcom_fs_init_partitions(FAR struct mtd_dev_s *mtd)
{
  // The Nuttx build configuration 'CONFIG_MTD_PARTITION' is ignored here

  FAR struct mtd_geometry_s geo;
  int partitionId;

  // Get geometry of QSPI Flash
  int ret = mtd->ioctl(mtd, MTDIOC_GEOMETRY, (unsigned long)((uintptr_t)&geo));
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Read MTD geometry:%d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "%s@%d- MTD Geo info - numb erase sectors %lu, eraseSize %lu page size %lu\n",
          thisFile, __LINE__, geo.neraseblocks, geo.erasesize, geo.blocksize);
#endif

  // Create a mtd object for each partition
  _pagesPerEraSector = geo.erasesize / geo.blocksize;

  uint32_t totalBytesInExtFlash = meadow_hw_version_flash_size();
  uint32_t primaryPartSize = totalBytesInExtFlash;
  int partBlockOff = 0;      // Offset increases with each partition

  // Since the space available for the primary partition is the total flash
  // size - all the smaller partitions, find value.
  for (partitionId = 0; partitionId < MEADOW_FS_PARTITION_TOTAL_COUNT; partitionId++)
  {
    if(partitionId == MEADOW_FS_PARTITION_OF_FS)
      continue;   // Can't use this partition's size yet

    // Verify that each partition's size is valid
    if(_partitionSizes[partitionId] % geo.erasesize != 0)
    {
      syslog(LOG_ERR, "%s@%d-Partition %d, size (%lu) not multiple of erase size (%lu)\n",
              thisFile, __LINE__, partitionId,
              _partitionSizes[partitionId], geo.erasesize);
      return -EINVAL;     // Invalid argument
    }

    primaryPartSize -= _partitionSizes[partitionId];
  }

  // Create each partition
#if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
  int totalBytes = 0;
#endif

  for(partitionId = MEADOW_FS_PARTITION_TOTAL_COUNT - 1;
      partitionId >= 0;
      partitionId--)
  {
    // Get the number of bytes in each partition starting with the last one.
    // This is to match legacy Meadow.
    uint32_t partSizeInBytes = _partitionSizes[partitionId];

    if(partSizeInBytes == MEADOW_FS_PARTITION_INIT_SIZE_FLAG)
      partSizeInBytes = primaryPartSize;

    // Find the number of blocks in this partition
    int nBlocksInPart = partSizeInBytes / geo.blocksize;

#if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
    size_t partSize = nBlocksInPart * geo.blocksize;
    totalBytes += partSize;
    syslog(LOG_DEBUG, "%s@%d-Creating part:%d, Offset:%u size:%d (%d Kb)\n",
             thisFile, __LINE__,
             partitionId, partBlockOff, partSize, partSize/1024);
#endif

    _partInfo[partitionId].partPageOffset = partBlockOff;
    _partInfo[partitionId].mtdPart = mtd_partition(mtd, partBlockOff, nBlocksInPart);
    if (_partInfo[partitionId].mtdPart == NULL)
    {
      syslog(LOG_ERR, "%s@%d-Error:mtd_partition:%d, offset=%d nPages=%d\n",
               thisFile, __LINE__, partitionId, partBlockOff, nBlocksInPart);
      break;
    }

    partBlockOff += nBlocksInPart;
  }

#if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "%s@%d-Created %d partitions, Total size:%d bytes (%d Kb)\n",
            thisFile, __LINE__,
            MEADOW_FS_PARTITION_TOTAL_COUNT, totalBytes, totalBytes/1024);
#endif

  return OK;
}
