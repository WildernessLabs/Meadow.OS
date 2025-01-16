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

// This module contains utility code for use with partitioned flash memory

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

/****************************************************************************
 * Private Data
 ****************************************************************************/
static char *thisFile = __FILE__;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// This function will take a block number and return the partition number and
// the block offset within the partition.
int hcom_nx_flash_conv_blk_to_part_offset(uint32_t blockNum, uint32_t *partNumb,
          uint32_t *blockOffset)
{
  uint32_t activePart;
  uint32_t *partitionOffsets = hcom_nx_fs_get_partition_offsets();
  uint32_t addrOfBlock = blockNum * hcom_nx_exec_ex_flash_get_block_size();

  *partNumb = 0xffffffff;
  *blockOffset = 0;

#if MEADOW_FLASH_PART_SHOW_SYSLOG > 0
  syslog(1, "-->%s@%d-Block Number:%06lu is Block Addr:0x%08lx\n",
          thisFile, __LINE__, blockNum, addrOfBlock);
#endif

  // Check partition numbers to see if the block is in one of them
  for(activePart = 0; activePart < MEADOW_FLASH_PART_TOTAL_PARTITIONS; activePart++)
  {
#if MEADOW_FLASH_PART_SHOW_SYSLOG > 0
        syslog(1, "%s@%d-Looking at Partition:%lu, start:0x%08lx, end:0x%08lx,\n",
            thisFile, __LINE__, activePart,
            partitionOffsets[activePart], partitionOffsets[activePart + 1]);
#endif

    if(activePart == MEADOW_FLASH_PART_NUMB_FILE_SYS)
    {
      // File System is last partition and should only be accessed via the
      // file system.
      syslog(LOG_ERR, "%s@%d-Direct access to file system is not allowed\n",
              thisFile, __LINE__);
      usleep(1000 * 1000);    // Make sure this is seen
      return -EINVAL;     // Invalid argument
      // _assert;
    }

    // Look at the starting and ending offset of this partition and determine
    // if the requested block is here
    if((addrOfBlock >= partitionOffsets[activePart]) && (addrOfBlock < partitionOffsets[activePart + 1]))
    {
#if MEADOW_FLASH_PART_SHOW_SYSLOG > 0
      syslog(1, "%s@%d-Partition:%lu, is correct, it's between 0x%08lx and 0x%08lx, now find 0x%08lx\n",
                thisFile, __LINE__, activePart,
                partitionOffsets[activePart], partitionOffsets[activePart + 1],
                addrOfBlock);
#endif

      // Right partition, now figure out if an offset is needed
      if(addrOfBlock != partitionOffsets[activePart])
      {
        // Need intra partition offset. How far into the partition is the block?
        uint32_t intraPartMemOffset = addrOfBlock - partitionOffsets[activePart];

        // Is this flash address a multiple of the block size?
        if(intraPartMemOffset % hcom_nx_exec_ex_flash_get_block_size() != 0)
        {
          syslog(LOG_ERR, "%s@%d-Block %06ld, size (%ld) not multiple of erase size (%ld)\n",
                  thisFile, __LINE__, blockNum, hcom_nx_exec_ex_flash_get_block_size(),
                  hcom_nx_exec_ex_flash_get_erase_block_size());
          return -EINVAL;     // Invalid argument
        }

        // Save the block offset
        *blockOffset = intraPartMemOffset / hcom_nx_exec_ex_flash_get_block_size();

        // Check if offset is outside the range of the partition's size.
        uint32_t *partSizes = hcom_nx_fs_get_partition_sizes();
        uint32_t partSize = partSizes[activePart];

#if MEADOW_FLASH_PART_SHOW_SYSLOG > 0
        syslog(1, "%s@%d-Partition size:%ld and offset is:%lu \n",
                  thisFile, __LINE__, intraPartMemOffset, partSize);
#endif
        if(intraPartMemOffset >= partSize)
        {
          syslog(LOG_ERR, "%s@%d-Block offset:%lu is outside of the partition:%lu\n",
                  thisFile, __LINE__, *blockOffset, partSize);
          return -EINVAL;
        }
      }
      else
      {
        // Start of partition so offset is 0
        *blockOffset = 0;
      }
      break;
    }
  }

  *partNumb = activePart;

#if MEADOW_FLASH_PART_SHOW_SYSLOG > 0
  syslog(1, "%s@%d-Returning partition:%lu, blockOffset:%lu\n",
          thisFile, __LINE__, *partNumb, *blockOffset);
#endif

  return OK;
}
