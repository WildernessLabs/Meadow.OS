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

// This module contains code that access the external QSPI flash memory

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_nx_common.h"
#include <meadow/hcom_protocol.h>
#include <meadow/hcom_upd_shared.h>
#include <meadow/hcom_shared_common.h>
#include <meadow/hcom_bbreg_defn.h>
#include <meadow/meadow_partition_share.h>
#include <meadow/meadow_hw_version.h>

#include <nuttx/arch.h>
#include <nuttx/mtd/mtd.h>
#include "stm32_uid.h"          // stm32_get_uniqueid()

#include <sys/boardctl.h>

#include <sys/stat.h>


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
// This function will take a block number and return the partition number
// along with the offset into the partition.
int hcom_nx_flash_get_part_offset(uint32_t blockNum, uint32_t *partNumb,
          uint32_t *offset)
{
    int ret;

    uint32_t *partitionSizes = hcom_nx_fs_get_partition_sizes();

    return OK;
}
 
//   }
//   // Try using partitions instead of low-level MTD access
// #if (DEV_USE_PARTITIONS_FOR_REFACTORING > 0)
//   int ret;
//   meadow_part_info_t partInfo;
//   uint32_t partStartAddr = 0;
//   uint32_t blockSize = hcom_nx_exec_ex_flash_get_block_size();
  
  
// Taken from Meadow repo via syslog 5Oct24. This defines the partitioning
// layout that Meadow.OS must match.
// Runtime       - Size: 3145728 ( 3072 KB), Offset:0x00000000
// Meadow update - Size: 1835008 ( 1792 KB), Offset:0x00300000
// OTA State     - Size:    4096 (    4 KB), Offset:0x004c0000
// OS Persisted  - Size:    4096 (    4 KB), Offset:0x004c1000
// Assert Data   - Size:   32768 (   32 KB), Offset:0x004c2000
// Unused        - Size:  221184 (  216 KB), Offset:0x004ca000
// File System   - Size:61865984 (60416 KB), Offset:0x00500000


// static uint32_t _partitionSizes[] = {MEADOW_FLASH_PART_SIZE_MONO_RUNTIME,
//                                      MEADOW_FLASH_PART_SIZE_OS_UPDATE,
//                                      MEADOW_FLASH_PART_SIZE_OTA_STATE,
//                                      MEADOW_FLASH_PART_SIZE_OS_PERSIST,
//                                      MEADOW_FLASH_PART_SIZE_ASSERT_DATA,
//                                      MEADOW_FLASH_PART_SIZE_UNUSED_SPACE,
//                                      MEADOW_FLASH_PART_SIZE_UNKNOWN};

  // Need to get the partition number from the startBlock. Assume that the
  // number of blocks doesn't spill into another partition.

//   // What is the starting memory address of this block?
//   partStartAddr = startBlock * blockSize;

//   for(int partNumb = 0; partNumb < MEADOW_FLASH_PART_TOTAL_PARTITIONS; partNumb++)
//   {
//     if(partStartAddr >= _partitionSizes[partNumb] && partStartAddr < _partitionSizes[partNumb + 1])
//     {
//       //  Found the partition block is in. Is there any offset?
//       if(partStartAddr != _partitionSizes[partNumb])
//       {
//         // How far into the partition is the block?
//         uint32_t offsetIn = partStartAddr - _partitionSizes[partNumb];
//       }
//     }
//   }


// The order in this array determines to partition numbers. At this time
// partition 6 will be mounted as the file system /meadow0.
// MEADOW_FLASH_PART_SIZE_MONO_RUNTIME
// MEADOW_FLASH_PART_SIZE_OS_UPDATE
// MEADOW_FLASH_PART_SIZE_OTA_STATE
// MEADOW_FLASH_PART_SIZE_OS_PERSIST
// MEADOW_FLASH_PART_SIZE_ASSERT_DATA
// MEADOW_FLASH_PART_SIZE_UNUSED_SPACE
// MEADOW_FLASH_PART_SIZE_UNKNOWN}



//   // Get the partition information
//   ret = hcom_nx_fs_get_partition_info(MEADOW_FLASH_PART_NUMB_RUNTIME,
//             &partInfo, NULL);
//   if(ret < 0)
//   {
//     syslog(1, "%s@%d-Error getting partition information\n", thisFile, __LINE__);
//     return ret;
//   }

//   // Since the runtime partition start at offset 0
//   MTD_BREAD(partInfo.mtdPart, startBlock, numberOfBlocks, destinationAddress);
//   return OK;
// #endif
// }
