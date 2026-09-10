/****************************************************************************
 * Meadow.OS/hcom_nx/create_fs/hcom_nx_fs_create.c
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

#if (HCOM_NX_CREATE_FS_INCLUDE_DEBUG_LOGGING > 0)
#pragma message "(--) hcom_nx_fs_create.c"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/
static char *thisFile = __FILE__;

static uint32_t _totalFlashSize;

/*
 * Not lock-protected: safe only because every current caller funnels
 * through a single serialized task and partition setup runs once at boot
 * before any task starts. This assumption would need revisiting if the
 * currently-unwired /dev/upd ioctl entry points (HCOM_NX_UPD_UPDATE_OS1/OS2,
 * HCOM_NX_UPD_COPY_RUNTIME_TO_RAM) are ever reactivated from a second task.
 * See _mtd in hcom_nx_fs_flash.c for the same assumption.
 */
static FAR meadow_part_info_t _partInfoArray[MEADOW_FLASH_PART_TOTAL_ELEMENTS];

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int hcom_fs_create_partitions_initialize_and_mount_fs(FAR struct mtd_dev_s *mtd);
static int hcom_fs_init_partitions(FAR struct mtd_dev_s *mtd);
static int hcom_nx_register_external_flash_mtd(FAR struct mtd_dev_s *master_flash_mtd);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

//===========================================================================
// This function accepts a Partition ID 0 and resolves it to a specific
// partition and returns a populated meadow_part_info_t.
// Note: the partNumb of MEADOW_FLASH_PART_OFFSET_PARENT_PART returns the
// meadow_part_info_t for the entire flash flash chip.
int hcom_nx_fs_get_partition_info(uint32_t partId,
          meadow_part_info_t *partInfo)
{
  if((partId > MEADOW_FLASH_PART_OFFSET_PARENT_PART) | (partId < 0))
  {
    syslog(LOG_ERR, "%s@%d-Partition:%lu out of range\n",
              thisFile, __LINE__, partId);
    return -EINVAL;
  }

  partInfo->isPartMounted   = _partInfoArray[partId].isPartMounted;
  partInfo->mtdPart         = _partInfoArray[partId].mtdPart;
  partInfo->byteOffset      = _partInfoArray[partId].byteOffset;
  partInfo->blockOffset     = _partInfoArray[partId].blockOffset;
  partInfo->partSize        = _partInfoArray[partId].partSize;

  return OK;
}

//===========================================================================
// Entry point for initialization of file system.
// Called by hcom_nx_startup_mgr.c at startup to create low-level partitioning
// and file system.
int hcom_nx_create_fs_initialize(FAR struct mtd_dev_s *mtd)
{
  int ret;

  // Initialize the partition information of all elements
  for(int i = 0; i < MEADOW_FLASH_PART_TOTAL_ELEMENTS; i++)
  {
    _partInfoArray[i].mtdPart = NULL;
    _partInfoArray[i].isPartMounted = false;
    _partInfoArray[i].byteOffset = 0;
    _partInfoArray[i].blockOffset = 0;
    _partInfoArray[i].partSize = 0;
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
// This function will create all the partitions, initialize the file system,
// mount the file system partition and if necessary format the partition.
int hcom_fs_create_partitions_initialize_and_mount_fs(FAR struct mtd_dev_s *mtd)
{
  int ret;

  // syslog(LOG_INFO, "F/S creating %d partition%s\n",
  //           MEADOW_FLASH_PART_TOTAL_PARTITIONS,
  //           MEADOW_FLASH_PART_TOTAL_PARTITIONS > 1 ? "s" : "");

  // Create the partitions in the external flash
  ret = hcom_fs_init_partitions(mtd);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-partitioning FS:%d\n", thisFile, __LINE__, ret);
    return ret;
  }

  //------------------------------------------------------------------------
  // syslog(LOG_DEBUG, "Partitioning complete. Init file system\n");

  // Initialize the file system
#if defined CONFIG_FS_LITTLEFS

  // File System only takes one partition. This call will create the Meadow
  // file system on partition MEADOW_FLASH_PART_OFFSET_FILE_SYS as '/meadow0'.
  ret = hcom_nx_create_one_littlefs_file_dev(MEADOW_FLASH_PART_OFFSET_FILE_SYS,
                    _partInfoArray[MEADOW_FLASH_PART_OFFSET_FILE_SYS].mtdPart);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-LittleFS error:%d part:%d\n",
              thisFile, __LINE__, ret, MEADOW_FLASH_PART_OFFSET_FILE_SYS);
    return ret;
  }
#endif

  //------------------------------------------------------------------------
  // syslog(LOG_DEBUG, "F/S Part Init complete, mount & format if needed\n");

#if defined(CONFIG_FS_LITTLEFS)
  // Mount the file system from partition 6 to /meadow0
  ret = hcom_nx_create_littlefs_mount_format_file_dev(
            MEADOW_FLASH_PART_OFFSET_FILE_SYS,
            MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-fs mount failed '%s' to '%s' for type '%s', Part %d error:%d\n",
          thisFile, __LINE__, HCOM_NX_FILE_MOUNT_POINT_SOURCE,
          MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME,
          HCOM_NX_FILE_MOUNT_FILE_SYS_TYPE,
          MEADOW_FLASH_PART_OFFSET_FILE_SYS, ret);
    return ret;
  }
#endif

  _partInfoArray[MEADOW_FLASH_PART_OFFSET_FILE_SYS].isPartMounted = true;
  syslog(LOG_NOTICE, "F/S creation complete.\n");

  return OK;
}

//=====================================================================
// (--)Ignores provided partition number, the file system's partition is always
// MEADOW_FLASH_PART_OFFSET_FILE_SYS.
bool hcom_nx_fs_is_mounted(uint32_t partitionNumber)
{
  return (_partInfoArray[MEADOW_FLASH_PART_OFFSET_FILE_SYS].isPartMounted);
}

//=====================================================================
// Used to implement RenewFileSys and maybe others
// (--) Ignore provided partition number, the file system's partition is always
// MEADOW_FLASH_PART_OFFSET_FILE_SYS.
// (--) FIX THIS NO NEED TO PASS IN PARTITION NUMBER
int hcom_nx_fs_1st_erase_sector_of_partition(uint32_t partitionNumber)
{
  return (_partInfoArray[MEADOW_FLASH_PART_OFFSET_FILE_SYS].byteOffset /
            hcom_nx_exec_ex_flash_get_erase_block_size());
}

//=====================================================================
// It will register the mtd driver.
int hcom_nx_register_external_flash_mtd(FAR struct mtd_dev_s *master_flash_mtd)
{
  int ret;

#if HCOM_NX_CREATE_FS_INCLUDE_DEBUG_LOGGING > 0
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

  uint32_t *partitionSizes = hcom_nx_fs_get_partition_sizes();
#if HCOM_NX_CREATE_FS_DISPLAY_PARTITION_AT_STARTUP > 0
  char **partitionNames = hcom_nx_fs_get_partition_names();
#endif

  // Get geometry of QSPI Flash
  int ret = mtd->ioctl(mtd, MTDIOC_GEOMETRY, (unsigned long)((uintptr_t)&geo));
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Read MTD geometry:%d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_NX_CREATE_FS_DISPLAY_PARTITION_AT_STARTUP > 0
  syslog(LOG_INFO, "MTD Geo info - total erase sectors %lu, eraseSize %lu block/page size %lu\n",
          geo.neraseblocks, geo.erasesize, geo.blocksize);
#endif

  // Need total flash size so we can find the primary partition size
  uint32_t primaryPartSize = geo.neraseblocks * geo.erasesize;
  // (--) While valid, this may no longer be the best way to
  // find _totalFlashSize
  _totalFlashSize = primaryPartSize;

  // This loop validates each partition and calculates the primary partition's
  // size used for the file system.
  for (partitionNumber = 0;
       partitionNumber < MEADOW_FLASH_PART_TOTAL_PARTITIONS;
       partitionNumber++)
  {

    if(partitionNumber == MEADOW_FLASH_PART_OFFSET_FILE_SYS)
      continue;   // Can't use partition of file system

    // Verify that each partition's size is valid
    if(partitionSizes[partitionNumber] % geo.erasesize != 0)
    {
      syslog(LOG_ERR, "%s@%d-Partition %d, size (%lu) not multiple of erase size (%lu)\n",
              thisFile, __LINE__, partitionNumber,
              partitionSizes[partitionNumber],
              geo.erasesize);
      return -EINVAL;     // Invalid argument
    }

    // Continue to calculate the primary partition's size
    primaryPartSize -= partitionSizes[partitionNumber];
  }

  // Create each partition
  uint32_t firstBlockOff = 0;      // Offset increases with each partition

#if HCOM_NX_CREATE_FS_DISPLAY_PARTITION_AT_STARTUP > 0
  int totalBytes = 0;
#endif

  // Creates multiple mtd objects, one for each partition.
  for (partitionNumber = 0;
       partitionNumber < MEADOW_FLASH_PART_TOTAL_PARTITIONS;
       partitionNumber++)
  {
    // Get the number of bytes in this partition.
    uint32_t partSizeInBytes = partitionSizes[partitionNumber];

    // Is this the File System partition? If so its size has been unknown.
    // Well, the size is known now.
    if(partitionNumber == MEADOW_FLASH_PART_OFFSET_FILE_SYS)
    {
      partSizeInBytes = primaryPartSize;    // The remaining space
      _partInfoArray[partitionNumber].partSize = partSizeInBytes;
    }

    _partInfoArray[partitionNumber].blockOffset = firstBlockOff;
    _partInfoArray[partitionNumber].byteOffset = firstBlockOff * geo.blocksize;

#if HCOM_NX_CREATE_FS_DISPLAY_PARTITION_AT_STARTUP > 0
    totalBytes += partSizeInBytes;
    syslog(LOG_INFO, "%s part:%d, size:%8lu (%5lu Kb), Byte Offset:0x%08lx, Block Offset %5lu\n",
             partitionNames[partitionNumber],
             partitionNumber,
             partSizeInBytes, partSizeInBytes/1024,
             firstBlockOff * geo.blocksize,
             firstBlockOff);
#endif

    // Find the number of blocks in this partition
    uint32_t totBlocksInPart = partSizeInBytes / geo.blocksize;

    // Create the partition
    _partInfoArray[partitionNumber].mtdPart = mtd_partition(mtd, firstBlockOff, totBlocksInPart);
    if (_partInfoArray[partitionNumber].mtdPart == NULL)
    {
      syslog(LOG_ERR, "%s@%d-Error:mtd_partition:%d, Block Offset=%lu nParts=%lu\n",
               thisFile, __LINE__, partitionNumber, firstBlockOff, totBlocksInPart);
      return -ENOMEM;
    }

    firstBlockOff += totBlocksInPart;
  }

  // After creating all the partitions, fill in the last array element
  // with the information for the entire flash drive.
  _partInfoArray[MEADOW_FLASH_PART_OFFSET_PARENT_PART].isPartMounted   = false;
  _partInfoArray[MEADOW_FLASH_PART_OFFSET_PARENT_PART].mtdPart         = mtd;
  _partInfoArray[MEADOW_FLASH_PART_OFFSET_PARENT_PART].byteOffset      = 0;
  _partInfoArray[MEADOW_FLASH_PART_OFFSET_PARENT_PART].blockOffset     = 0;
  _partInfoArray[MEADOW_FLASH_PART_OFFSET_PARENT_PART].partSize        = _totalFlashSize;

#if HCOM_NX_CREATE_FS_DISPLAY_PARTITION_AT_STARTUP > 0
  int actualPartCnt = partitionNumber == -1 ? MEADOW_FLASH_PART_TOTAL_PARTITIONS : partitionNumber;
  syslog(LOG_INFO, "Created %d partitions, Total size:%d bytes (%d Kb)\n",
            actualPartCnt, totalBytes, totalBytes/1024);
#endif

  return OK;
}
