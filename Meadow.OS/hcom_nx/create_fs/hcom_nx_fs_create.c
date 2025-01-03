/****************************************************************************
 * \configs\stm32f777zit6-meadow\src\hcom_nx\create_fs\hcom_nx_fs_create.c
 * 
 *   Copyright (C) 2019 - 2025 Wilderness Labs. All rights reserved.
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
#include <errno.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <nuttx/mtd/mtd.h>
#include <dirent.h>
#include <meadow/meadow_hw_version.h>
#include <meadow/meadow_partition_share.h>

#if !defined CONFIG_FS_LITTLEFS
#warning "LittleFS is not configured. No file system can exist"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
// To show debugging information set to 1
#define HCOM_NX_FS_INCLUDE_DEBUG_LOGGING 1

#if (HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0)
#pragma message "(--) hcom_nx_fs_create.c"
#endif

#define MEADOW_FS_PARTITION_OF_FS (0)

/****************************************************************************
 * Private Data
 ****************************************************************************/
static char *thisFile = __FILE__;

static FAR struct mtd_dev_s *_extFlashMtd;
static uint32_t _totalFlashSize;
static uint32_t _pagesPerEraSector;
static FAR struct meadow_part_info_s _partInfo[MEADOW_FLASH_PART_NUMB_TOTAL];

// Taken from Meadow repo via syslog 5Oct24. This defines the partitioning
// layout that Meadow.OS must initially match.
// Runtime       - Size: 3145728 ( 3072 KB), Offset:0x00000000
// Meadow update - Size: 1835008 ( 1792 KB), Offset:0x00300000
// OTA State     - Size:    4096 (    4 KB), Offset:0x004c0000
// OS Persisted  - Size:    4096 (    4 KB), Offset:0x004c1000
// Assert Data   - Size:   32768 (   32 KB), Offset:0x004c2000
// Unused        - Size:  221184 (  216 KB), Offset:0x004ca000
// File System   - Size:61865984 (60416 KB), Offset:0x00500000

#if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
// Since they are created starting at offset 0 (Runtime), this array must
// be backwards
static char *_partitionNames[] = {"File System  ",
                                  "Unused       ",
                                  "Assert Data  ",
                                  "OS Persisted ",
                                  "OTA State    ",
                                  "Meadow update",
                                  "Runtime      "};
#endif

// The order in this array determines to partition numbers. The last entry
// will be at offset 0 in the flash chip's addressing.
// However, the startup code, places these in flash memory layout, starting
// with the last element, to the first to match the legacy flash layout.
static uint32_t _partitionSizes[] = {MEADOW_FLASH_PART_SIZE_UNKNOWN,    // File System
                                     MEADOW_FLASH_PART_SIZE_UNUSED_SPACE,
                                     MEADOW_FLASH_PART_SIZE_ASSERT_DATA,
                                     MEADOW_FLASH_PART_SIZE_OS_PERSIST,
                                     MEADOW_FLASH_PART_SIZE_OTA_STATE,
                                     MEADOW_FLASH_PART_SIZE_OS_UPDATE,
                                     MEADOW_FLASH_PART_SIZE_OS_RUNTIME};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int hcom_fs_create_partitions_initialize_and_mount_fs(FAR struct mtd_dev_s *mtd);
static int hcom_fs_init_partitions(FAR struct mtd_dev_s *mtd);
static int hcom_nx_register_external_flash_mtd(FAR struct mtd_dev_s *master_flash_mtd);

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// This function accepts a Partition define and resolves it to a specific
// partition and returns an instance of struct meadow_part_info_s.
// Note: the partNumb of MEADOW_FLASH_PART_NUMB_PARENT_PART will return the
// flash flash chip's MTD and the flash chip's size.
int hcom_nx_fs_get_partition_info(int partNumb,
          struct meadow_part_info_s *partInfo,
          struct hcom_nx_cmd_data *cmdData)
{
  if(partNumb > MEADOW_FLASH_PART_NUMB_PARENT_PART | partNumb < 0)
  {
    cmdData->logLevel = LOG_ERR;
    cmdData->logLen = snprintf(cmdData->logMsg, HCOM_NX_CMD_LOG_MSG_SIZE,
            "%s@%d-Partition number out of range", thisFile, __LINE__);
    return -EINVAL;     // Invalid argument
  }

  partInfo->isPartMounted   = _partInfo[partNumb].isPartMounted;
  partInfo->mtdPart         = _partInfo[partNumb].mtdPart;
  partInfo->byteOffset      = _partInfo[partNumb].byteOffset;
  partInfo->blockOffset     = _partInfo[partNumb].blockOffset;
  partInfo->partSize        = _partInfo[partNumb].partSize;
  return OK;
}

//===========================================================================
// Entry point for initialization of file system.
// Called by hcom_nx_startup_mgr.c at startup to create low-level partitioning
// and file system.
int hcom_nx_create_fs_initialize(FAR struct mtd_dev_s *mtd)
{
  int ret;

  _extFlashMtd = mtd;

  _pagesPerEraSector = 0;

  for(int i = 0; i < MEADOW_FLASH_PART_NUMB_TOTAL; i++)
  {
    _partInfo[i].mtdPart = NULL;
    _partInfo[i].isPartMounted = false;
    _partInfo[i].byteOffset = 0;
    _partInfo[i].blockOffset = 0;
    _partInfo[i].partSize = 0;
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
            MEADOW_FLASH_PART_NUMB_TOTAL,
            MEADOW_FLASH_PART_NUMB_TOTAL > 1 ? "s" : "");

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
#if defined CONFIG_FS_LITTLEFS
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
                                        const char *fileSystemType, uint32_t partitionNumber,
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

  // e.g. /dev/smart0 or dev/little0
  snprintf_chk(finalSourceName, HCOM_NX_MAX_PATH_AND_FILE_BUFF_LENGTH, "%s0p%d", sourceDevice, partitionNumber);

  // e.g. /meadow0
  snprintf_chk(fullMountPtName, HCOM_NX_MAX_PATH_AND_FILE_BUFF_LENGTH, "%s%d", targetDevice, partitionNumber);

 #if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "Attempt to mount partition %ld as '%s' to '%s' type '%s'\n",
           partitionNumber, finalSourceName, fullMountPtName, fileSystemType);
 #endif

 #if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "%s@%d-Mounting part:%ld as '%s' to '%s' type '%s', cmd '%s'\n",
        thisFile, __LINE__, partitionNumber, finalSourceName,
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

  _partInfo[partitionNumber].isPartMounted = true;
  
 #if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "%s@%d-Mount successful\n", thisFile, __LINE__);
 #endif
        
  free(finalSourceName);
  free(fullMountPtName);

  return OK;
}

//=====================================================================
// (--) Check this for usage
bool hcom_nx_fs_is_mounted(uint32_t partitionNumber)
{
  return (_partInfo[partitionNumber].isPartMounted);
}

//=====================================================================
// (--) Check this for usage
// Used to implement RenewFileSys and maybe others
int hcom_nx_fs_1st_erase_sector_of_partition(uint32_t partitionNumber)
{
  return (_partInfo[partitionNumber].byteOffset / _pagesPerEraSector);
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

  // Register the MTD driver so that it can be accessed from the VFS
  // master mtd becomes '/dev/extflash
  snprintf_chk(finalSourceName, HCOM_NX_MAX_PATH_AND_FILE_BUFF_LENGTH,
            "%s0", HCOM_NX_FILE_MOUNT_POINT_SOURCE);

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
  FAR struct mtd_geometry_s geo;
  int partitionNumber;

  // Get geometry of QSPI Flash
  int ret = mtd->ioctl(mtd, MTDIOC_GEOMETRY, (unsigned long)((uintptr_t)&geo));
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Read MTD geometry:%d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "%s@%d-MTD Geo info - total erase sectors %lu, eraseSize %lu block/page size %lu\n",
          thisFile, __LINE__, geo.neraseblocks, geo.erasesize, geo.blocksize);
#endif

  _pagesPerEraSector = geo.erasesize / geo.blocksize;

  // Need total flash size so we can find the primary partition size
  uint32_t primaryPartSize = geo.neraseblocks * geo.erasesize;
  // (--) While valid, this may no longer be the best way to
  // find _totalFlashSize
  _totalFlashSize = primaryPartSize;

  // This loop validates each partition and calculates the primary partition's
  // size used for the file system.
  for (partitionNumber = 0; partitionNumber < MEADOW_FLASH_PART_NUMB_TOTAL; partitionNumber++)
  {
    if(partitionNumber == MEADOW_FS_PARTITION_OF_FS)
      continue;   // Can't use partition of file system

    // Verify that each partition's size is valid
    if(_partitionSizes[partitionNumber] % geo.erasesize != 0)
    {
      syslog(LOG_ERR, "%s@%d-Partition %d, size (%lu) not multiple of erase size (%lu)\n",
              thisFile, __LINE__, partitionNumber,
              _partitionSizes[partitionNumber],
              geo.erasesize);
      return -EINVAL;     // Invalid argument
    }

    // Continue to calculate the primary partition's size
    primaryPartSize -= _partitionSizes[partitionNumber];
  }

  // Create each partition
  uint32_t firstBlockOff = 0;      // Offset increases with each partition

#if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
  int totalBytes = 0;
#endif

  // Create multiple mtd objects, one for each partition.
  // This loop runs backwards so all remaining space can be for file system
  for(partitionNumber = MEADOW_FLASH_PART_NUMB_TOTAL - 1;
      partitionNumber >= 0;
      partitionNumber--)
  {
    // Get the number of bytes in this partition.
    uint32_t partSizeInBytes = _partitionSizes[partitionNumber];

    // Is this the unknown partition (i.e. File System) whose size has been
    // unknown? Well, the size is known now.
    if(partSizeInBytes == MEADOW_FLASH_PART_SIZE_UNKNOWN)
    {
      partSizeInBytes = primaryPartSize;    // The remaining space
      _partInfo[partitionNumber].partSize = partSizeInBytes;
    }

    _partInfo[partitionNumber].blockOffset = firstBlockOff;
    _partInfo[partitionNumber].byteOffset = firstBlockOff * geo.blocksize;

#if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
    totalBytes += partSizeInBytes;
    syslog(LOG_DEBUG, "%s@%d-%s part:%d, size:%8lu (%5lu Kb), Byte Offset:0x%08lx, Block Offset %5lu\n",
             thisFile, __LINE__,
             _partitionNames[partitionNumber],
             partitionNumber,
             partSizeInBytes, partSizeInBytes/1024,
             firstBlockOff * geo.blocksize,
             firstBlockOff);
#endif

    // Find the number of blocks in this partition
    uint32_t totBlocksInPart = partSizeInBytes / geo.blocksize;

    // Create partition
    _partInfo[partitionNumber].mtdPart = mtd_partition(mtd, firstBlockOff, totBlocksInPart);
    if (_partInfo[partitionNumber].mtdPart == NULL)
    {
      syslog(LOG_ERR, "%s@%d-Error:mtd_partition:%d, Block Offset=%lu nParts=%lu\n",
               thisFile, __LINE__, partitionNumber, firstBlockOff, totBlocksInPart);
      break;
    }

    firstBlockOff += totBlocksInPart;
  }

  // After creating all the partitions, fill in the last array element
  // with the information for the entire file system.
  _partInfo[MEADOW_FLASH_PART_NUMB_PARENT_PART].isPartMounted   = false;
  _partInfo[MEADOW_FLASH_PART_NUMB_PARENT_PART].mtdPart         = _extFlashMtd;
  _partInfo[MEADOW_FLASH_PART_NUMB_PARENT_PART].byteOffset      = 0;
  _partInfo[MEADOW_FLASH_PART_NUMB_PARENT_PART].blockOffset     = 0;
  _partInfo[MEADOW_FLASH_PART_NUMB_PARENT_PART].partSize        = _totalFlashSize;

#if HCOM_NX_FS_INCLUDE_DEBUG_LOGGING > 0
  int actualPartCnt = partitionNumber == -1 ? MEADOW_FLASH_PART_NUMB_TOTAL : partitionNumber;
  syslog(LOG_DEBUG, "%s@%d-Created %d partitions, Total size:%d bytes (%d Kb)\n",
            thisFile, __LINE__,
            actualPartCnt, totalBytes, totalBytes/1024);
#endif

  return OK;
}
