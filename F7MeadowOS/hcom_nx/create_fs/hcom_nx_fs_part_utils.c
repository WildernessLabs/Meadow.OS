/****************************************************************************
 * /Meadow.OS/hcom_nx/create_fs/hcom_nx_fs_part_utils.c
 * 
 *   Copyright (C) 2025 Wilderness Labs. All rights reserved.
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

// This module contains utility code for use with partitioned flash memory.
// Some similar functions are in hcom_nx_fs_create.c.

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_nx_common.h"
#include <meadow/hcom_shared_common.h>
#include <meadow/meadow_partition_share.h>
#include <nuttx/mtd/mtd.h>
#include <assert.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
/* Configuration ************************************************************/

// Show syslog messages, good for debugging?
#define MEADOW_FLASH_PART_UTILS_SHOW_SYSLOG (0)

#if (MEADOW_INCLUDE_CODE_FOR_TESTING_5MB_OF_FLASH)
#pragma message "(--) hcom_nx_ex_part_utils.c"
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/
static char *thisFile = __FILE__;

// One-entry cache of the last partition resolved by
// hcom_nx_flash_block_offset_to_part_offset(), so a run of sequential
// blocks within one bulk flash operation (the actual real-world access
// pattern for OTA/mono-flash reads/writes/erases) can skip the linear
// partition scan on every call after the first. Partition boundaries are
// immutable after hcom_fs_init_partitions() runs at boot, so this never
// needs invalidating. Both fields default to 0 (empty range), so the very
// first call always falls through to the full scan.
static uint32_t _cachedPart;
static uint32_t _cachedPartStart;
static uint32_t _cachedPartEnd;

// Taken from Meadow repo via syslog 5Oct24. This defines the partitioning
// layout that Meadow.OS must match.
// Runtime       - Size: 3145728 ( 3072 KB), Offset:0x00000000
// Meadow update - Size: 1835008 ( 1792 KB), Offset:0x00300000
// OTA State     - Size:    4096 (    4 KB), Offset:0x004c0000
// OS Persisted  - Size:    4096 (    4 KB), Offset:0x004c1000
// Assert Data   - Size:   32768 (   32 KB), Offset:0x004c2000
// Unused        - Size:  221184 (  216 KB), Offset:0x004ca000
// File System   - Size:61865984 (60416 KB), Offset:0x00500000

// The order in this array determines the partition numbers.
static uint32_t _partitionSizes[] = 
{
  MEADOW_FLASH_PART_SIZE_MONO_RUNTIME,
  MEADOW_FLASH_PART_SIZE_OS_UPDATE,
  MEADOW_FLASH_PART_SIZE_OTA_STATE,
  MEADOW_FLASH_PART_SIZE_OS_PERSIST,
  MEADOW_FLASH_PART_SIZE_ASSERT_DATA,
  MEADOW_FLASH_PART_SIZE_UNUSED_SPACE,
  MEADOW_FLASH_PART_SIZE_UNKNOWN
};

// Partition memory offsets
static uint32_t _partitionOffsets[] =
{
  MEADOW_PART_ADDR_OFFSET_RUNTIME,
  MEADOW_PART_ADDR_OFFSET_NUTTX_UPDATE,
  MEADOW_PART_ADDR_OFFSET_OTA_SPACE,
  MEADOW_PART_ADDR_OFFSET_OS_PERSIST,
  MEADOW_PART_ADDR_OFFSET_ASSERT_DATA,
  MEADOW_PART_ADDR_OFFSET_UNUSED_SPACE,
  MEADOW_PART_ADDR_OFFSET_FILE_SYS
};

#if ((HCOM_NX_CREATE_FS_INCLUDE_DEBUG_LOGGING > 0) || \
          (HCOM_NX_CREATE_FS_DISPLAY_PARTITION_AT_STARTUP > 0))
static char *_partitionNames[] =
{
  "Runtime      ",
  "Meadow update",
  "OTA State    ",
  "OS Persisted ",
  "Assert Data  ",
  "Unused       ",
  "File System  "
};
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/
#if ((HCOM_NX_CREATE_FS_INCLUDE_DEBUG_LOGGING > 0) || \
          (HCOM_NX_CREATE_FS_DISPLAY_PARTITION_AT_STARTUP > 0))

// Returns pointer to the partition name array. Only used for diagnostics.
char **hcom_nx_fs_get_partition_names(void)
{
  return _partitionNames;
}
#endif

//===========================================================================
// Returns pointer to the partition size array
uint32_t *hcom_nx_fs_get_partition_sizes(void)
{
  return _partitionSizes;
}

//===========================================================================
// Return the array of offset of each partition
uint32_t *hcom_nx_fs_get_partition_offsets(void)
{
  return _partitionOffsets;
}

//================================================================================
// Erase blocks and Read/Write starting addresses are different! Erase blocks are
// 4096 bytes and Read/Write blocks are 256 bytes. Therefore, this functions
// uses the starting byte offset.
// What this function does:
// This function will take a byte address and return partition mtd,
// partition number and offset of the block within the partition
int hcom_nx_flash_block_offset_to_part_offset(uint32_t startByteAddr,
          struct mtd_dev_s **partMtd, uint32_t *partNumb,
          uint32_t *partBlockOff)
{
  meadow_part_info_t partInfo;    // Partition information including MTD
  uint32_t activePart;
  uint32_t *partitionOffsets = hcom_nx_fs_get_partition_offsets();

  // Fast path: skip the linear scan if this address falls in the same
  // partition as the last call (see _cachedPart's declaration comment).
  if((startByteAddr >= _cachedPartStart) && (startByteAddr < _cachedPartEnd))
  {
    activePart = _cachedPart;
  }
  else
  {
    // Loop through the partitions looking for the partition with this block
    for(activePart = 0; activePart < MEADOW_FLASH_PART_TOTAL_PARTITIONS; activePart++)
    {
      // The last partition (file system) has no fixed size known at compile
      // time (it's whatever remains of the flash chip, which varies by
      // hardware version) -- there's no _partitionOffsets[] entry past it to
      // read as an end bound, so treat it as extending to the top of the
      // address space. This partition is rejected below anyway.
      uint32_t partEnd = (activePart + 1 < MEADOW_FLASH_PART_TOTAL_PARTITIONS) ?
                partitionOffsets[activePart + 1] : UINT32_MAX;

#if MEADOW_FLASH_PART_UTILS_SHOW_SYSLOG > 0
      syslog(2, "-->%s@%d-Checking Partition:%lu, start:0x%08lx, end:0x%08lx,\n",
          thisFile, __LINE__, activePart,
          partitionOffsets[activePart], partEnd);
#endif

      // Look at the starting and ending offset of this partition and determine
      // if the requested block is here.
      if((startByteAddr >= partitionOffsets[activePart]) && \
         (startByteAddr < partEnd))
      {
#if MEADOW_FLASH_PART_UTILS_SHOW_SYSLOG > 0
        syslog(2, "-->%s@%d-Partition:%lu, matches. 0x%08lx between 0x%08lx and 0x%08lx\n",
                  thisFile, __LINE__,
                  activePart,
                  startByteAddr,
                  partitionOffsets[activePart],
                  partEnd);
        usleep(20 * 1000);    // Make sure this is seen
#endif
        _cachedPart = activePart;
        _cachedPartStart = partitionOffsets[activePart];
        _cachedPartEnd = partEnd;
        break;    // On success, break out of loop
      }
    }
  }

  // Was any partition found?
  if(activePart == MEADOW_FLASH_PART_TOTAL_PARTITIONS)
  {
    // Block is not in any partition
    return -EINVAL;     // Invalid argument
  }

  // However, the last partition, the file system is restricted
  if(activePart == MEADOW_FLASH_PART_OFFSET_FILE_SYS)
  {
    // File System is last partition and should not be accessed
    syslog(LOG_ERR, "%s@%d-Error:File System area in flash is restricted\n",
            thisFile, __LINE__);
    // _assert;
    usleep(20 * 1000); // Make sure this is seen
    return -EINVAL;     // Invalid argument
  }

  // Partition is good, if not at the start, an offset is needed
  if(startByteAddr != partitionOffsets[activePart])
  {
    // Need intra block partition offset
    uint32_t intraPartMemOffset = startByteAddr - partitionOffsets[activePart];

    // Is this flash address a multiple of the block (aka page) size?
    if(intraPartMemOffset % hcom_nx_exec_ex_flash_get_block_size() != 0)
    {
      syslog(LOG_ERR, "%s@%d-Error:Block %06ld, size (%ld) not multiple of erase size (%ld)\n",
              thisFile, __LINE__,
              startByteAddr,
              hcom_nx_exec_ex_flash_get_block_size(),
              hcom_nx_exec_ex_flash_get_erase_block_size());
      usleep(20 * 1000);  // Make sure this is seen
      return -EINVAL;     // Invalid argument
    }

    // Save the block offset
    *partBlockOff = intraPartMemOffset / hcom_nx_exec_ex_flash_get_block_size();

    // Check if offset is outside the range of the partition's size.
    uint32_t *partSizes = hcom_nx_fs_get_partition_sizes();
    uint32_t partSize = partSizes[activePart];

#if MEADOW_FLASH_PART_UTILS_SHOW_SYSLOG > 0
    syslog(2, "-->%s@%d-Partition size:%ld and offset:%lu \n",
              thisFile, __LINE__, intraPartMemOffset, partSize);
    usleep(20 * 1000);    // Make sure this is seen
#endif

    if(intraPartMemOffset >= partSize)
    {
      syslog(LOG_ERR, "%s@%d-Block offset:%lu is outside of the partition:%lu\n",
              thisFile, __LINE__, *partBlockOff, partSize);
      usleep(20 * 1000);  // Make sure this is seen
      return -EINVAL;
    }
  }
  else
  {
    *partBlockOff = 0;
  }

  if(partMtd != NULL)
  {
    int ret = hcom_nx_fs_get_partition_info(activePart, &partInfo);
    if(ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-Error:%d getting partition info\n",
                thisFile, __LINE__, ret);
      usleep(20 * 1000);  // Make sure this is seen
      return ret;
    }

    // Return the MTD device for this partition
    *partMtd = partInfo.mtdPart;
  }
  
#if MEADOW_FLASH_PART_UTILS_SHOW_SYSLOG > 0
  syslog(2, "-->%s@%d-Returning partition:%lu, partBlockOff:%lu\n",
          thisFile, __LINE__, activePart, *partBlockOff);

  syslog(2, "-->%s@%d-mtdPart:%p, byteOffset:%lu, blockOffset:%lu, isPartMounted:%u, partSize:%u \n",
          thisFile, __LINE__, partInfo.mtdPart, partInfo.byteOffset,
          partInfo.blockOffset, partInfo.isPartMounted, partInfo.partSize);
  usleep(20 * 1000);    // Make sure this is seen
#endif

  if(partNumb != NULL)
   *partNumb = activePart;

  return OK;
}

//====================================================================
// This function calls MTD_BREAD using the correct partition and offset for
// the partition
// MTD_BREAD uses the 256 byte "page" for reading
int partMtdBRead(uint32_t startBlock, uint32_t numbBlocks, uint8_t *destBuff)
{
  struct mtd_dev_s *partMtd;
  uint32_t partBlockOff;
  uint32_t startOffsetBytes;

  // From the starting block number, get the partition and block offset
  // Must convert startBlock offset to byte offset for finding partition etc.
  startOffsetBytes = startBlock * hcom_nx_exec_ex_flash_get_block_size();

  // Get the partition information
  int ret = hcom_nx_flash_block_offset_to_part_offset(startOffsetBytes, &partMtd,
           NULL, &partBlockOff);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error:%d converting block to partition\n",
               thisFile, __LINE__, ret);
    return ret;
  }

  return MTD_BREAD(partMtd, partBlockOff, numbBlocks, destBuff);
}

//====================================================================
// This function calls MTD_BWRITE using the correct partition and offset for
// the partition
// MTD_BWRITE uses the 256 byte "page" for writing 
int partMtdBWrite(uint32_t startBlock, uint32_t numbBlocks, uint8_t *sourceBuff)
{
  struct mtd_dev_s *partMtd;
  uint32_t partBlockOff;
  uint32_t startOffsetBytes;

  // From the starting block number, get the partition and block offset
  // Must convert startBlock offset to byte offset for finding partition etc.
  startOffsetBytes = startBlock * hcom_nx_exec_ex_flash_get_block_size();

  // Get the partition information
  int ret = hcom_nx_flash_block_offset_to_part_offset(startOffsetBytes, &partMtd,
           NULL, &partBlockOff);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error:%d converting block to partition\n",
              thisFile, __LINE__, ret);
    return ret;
  }

   return MTD_BWRITE(partMtd, partBlockOff, numbBlocks, sourceBuff);
}

//====================================================================
// This function calls MTD_ERASE using the partition corrected mtd and offset
// MTD_ERASE uses the 4096 byte "sector" for erasing
int partMtdErase(uint32_t startBlock, uint32_t numbBlocks)
{
  struct mtd_dev_s *partMtd;
  uint32_t partBlockOff;
  uint32_t startOffsetBytes;

  // From the starting block number, get the partition and block offset
  // Must convert startBlock offset to byte offset for finding partition etc.
  // Note: MTD_ERASE's startBlock is in 4096 byte erase blocks.
  startOffsetBytes = startBlock * hcom_nx_exec_ex_flash_get_erase_block_size();

  // Get the partition information
  int ret = hcom_nx_flash_block_offset_to_part_offset(startOffsetBytes, &partMtd,
           NULL, &partBlockOff);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error:%d converting block to partition\n",
              thisFile, __LINE__, ret);
    return ret;
  }

  return MTD_ERASE(partMtd, partBlockOff, numbBlocks);
}
