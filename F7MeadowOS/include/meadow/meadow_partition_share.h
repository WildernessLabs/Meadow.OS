/****************************************************************************
 * /Meadow.OS/include/meadow/meadow_partition_share.h
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

#ifndef __INCLUDE_MEADOWOS_PARTITION_SHARE_H
#define __INCLUDE_MEADOWOS_PARTITION_SHARE_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

// This enables the ouput of partition information at startup
#define HCOM_NX_CREATE_FS_DISPLAY_PARTITION_AT_STARTUP (1)

// The HCOM_NX_EX_FLASH_SHOW_FLASH_STATS define is in hcom_nx_common.h@175.
// Couldn't be here because it needed to control the build of more global
// objects.

//-------------------------------------------------------------------------
// Note: The naming and definitions of the flash segments in the Original
// Meadow OS evolved over several years. In the Original Meadow, pieces
// and parts of each flash segment has been spread in different places.
// Mark Stevens did a great job of consolidating the definitions in
// 'include/meadow/hcom_shared_common.h'.
//
// Using partitions should make it much cleaner and easier to modify the flash
// layout if this ever becomes necessary. It also protects each segment from
// being overwritten.

// 3MB is for the Meadow OS
#define MEADOW_FLASH_PART_SIZE_MONO_RUNTIME   (HCOM_NX_FS_MONO_RAW_PARTITION_SIZE)

// 2MB is reserved for other uses
#define MEADOW_FLASH_PART_SIZE_OTHER_RESERVED (HCOM_NX_FS_OTA_RESERVED_SPACE)

// The remaining for OS Update is 2MB - 256kB = 0x1C0000
#define MEADOW_FLASH_PART_SIZE_OS_UPDATE      (HCOM_NX_FS_NUTTX_UPDATE_SIZE)

// All reserved totals 3MB + 2MB = 5MB
#define MEADOW_FLASH_PART_SIZE_ALL_RESERVED   (MEADOW_FLASH_PART_SIZE_MONO_RUNTIME + \
                                               MEADOW_FLASH_PART_SIZE_OTHER_RESERVED)

// The following define the flash within the additional 256kB of data.
// Comments relate to how it was was defined in Classic Meadow OS.
#define MEADOW_FLASH_PART_SIZE_OTA_STATE      (4096)  // From sizeof(OTAState)
#define MEADOW_FLASH_PART_SIZE_OS_PERSIST     (4096)  // From geo.erasesize
#define MEADOW_FLASH_PART_SIZE_ASSERT_DATA    (HCOM_NX_MAXIMUM_ASSERTION_DATA_SIZE)

// How much space is currently unused of the 3MB reserved?
#define MEADOW_FLASH_PART_SIZE_UNUSED_SPACE (MEADOW_FLASH_PART_SIZE_ALL_RESERVED - \
                                             (MEADOW_FLASH_PART_SIZE_MONO_RUNTIME + \
                                              MEADOW_FLASH_PART_SIZE_OS_UPDATE + \
                                              MEADOW_FLASH_PART_SIZE_OTA_STATE + \
                                              MEADOW_FLASH_PART_SIZE_OS_PERSIST + \
                                              MEADOW_FLASH_PART_SIZE_ASSERT_DATA))

#if MEADOW_FLASH_PART_SIZE_UNUSED_SPACE < 0
#error "MEADOW_FLASH_PART_SIZE_UNUSED_SPACE went negative -- check the flash \
partition size constants in hcom_shared_common.h"
#endif

// Partition assignments
#define MEADOW_FLASH_PART_OFFSET_RUNTIME        (0)
#define MEADOW_FLASH_PART_OFFSET_NUTTX_UPDATE   (1)
#define MEADOW_FLASH_PART_OFFSET_OTA_SPACE      (2)
#define MEADOW_FLASH_PART_OFFSET_OS_PERSIST     (3)
#define MEADOW_FLASH_PART_OFFSET_ASSERT_DATA    (4)
#define MEADOW_FLASH_PART_OFFSET_UNUSED_SPACE   (5)
#define MEADOW_FLASH_PART_OFFSET_FILE_SYS       (6)
#define MEADOW_FLASH_PART_OFFSET_PARENT_PART    (7) // Offset to QSPI parent
#define MEADOW_FLASH_PART_TOTAL_PARTITIONS      (7) // Total partitions (0-6)
#define MEADOW_FLASH_PART_TOTAL_ELEMENTS        (8) // Total number of elements

// Memory address offsets to each partition
#define MEADOW_PART_ADDR_OFFSET_RUNTIME      (0)
#define MEADOW_PART_ADDR_OFFSET_NUTTX_UPDATE (MEADOW_FLASH_PART_SIZE_MONO_RUNTIME)
#define MEADOW_PART_ADDR_OFFSET_OTA_SPACE    (MEADOW_FLASH_PART_SIZE_MONO_RUNTIME + \
                                              MEADOW_FLASH_PART_SIZE_OS_UPDATE)
#define MEADOW_PART_ADDR_OFFSET_OS_PERSIST   (MEADOW_FLASH_PART_SIZE_MONO_RUNTIME + \
                                              MEADOW_FLASH_PART_SIZE_OS_UPDATE + \
                                              MEADOW_FLASH_PART_SIZE_OTA_STATE)
#define MEADOW_PART_ADDR_OFFSET_ASSERT_DATA  (MEADOW_FLASH_PART_SIZE_MONO_RUNTIME + \
                                              MEADOW_FLASH_PART_SIZE_OS_UPDATE + \
                                              MEADOW_FLASH_PART_SIZE_OTA_STATE + \
                                              MEADOW_FLASH_PART_SIZE_OS_PERSIST)
#define MEADOW_PART_ADDR_OFFSET_UNUSED_SPACE (MEADOW_FLASH_PART_SIZE_MONO_RUNTIME + \
                                              MEADOW_FLASH_PART_SIZE_OS_UPDATE + \
                                              MEADOW_FLASH_PART_SIZE_OTA_STATE + \
                                              MEADOW_FLASH_PART_SIZE_OS_PERSIST + \
                                              MEADOW_FLASH_PART_SIZE_ASSERT_DATA)
#define MEADOW_PART_ADDR_OFFSET_FILE_SYS     (MEADOW_FLASH_PART_SIZE_MONO_RUNTIME + \
                                              MEADOW_FLASH_PART_SIZE_OS_UPDATE + \
                                              MEADOW_FLASH_PART_SIZE_OTA_STATE + \
                                              MEADOW_FLASH_PART_SIZE_OS_PERSIST + \
                                              MEADOW_FLASH_PART_SIZE_ASSERT_DATA + \
                                              MEADOW_FLASH_PART_SIZE_UNUSED_SPACE)

// The final partition is the File System, it can only be determined after
// initializing Meadow. This is because we support different flash chips with
// different sizes. The final File System partition size is all remaining
// flash space.
#define MEADOW_FLASH_PART_SIZE_UNKNOWN      (0xffffffff)

// This structure defines the information about each external flash partition
// maintained by Meadow.
struct meadow_part_info_s
{
    struct mtd_dev_s *mtdPart;
    off_t byteOffset;
    off_t blockOffset;
    bool isPartMounted;
    size_t partSize;
};
typedef struct meadow_part_info_s meadow_part_info_t;

// Obtains the flash partition information for a single partition
int hcom_nx_fs_get_partition_info(uint32_t partId,
          meadow_part_info_t *partInfo);

// Return pointer to the partition information array
uint32_t *hcom_nx_fs_get_partition_offsets(void);
uint32_t *hcom_nx_fs_get_partition_sizes(void);

#if ((HCOM_NX_CREATE_FS_INCLUDE_DEBUG_LOGGING > 0) || \
          (HCOM_NX_CREATE_FS_DISPLAY_PARTITION_AT_STARTUP > 0))
char **hcom_nx_fs_get_partition_names(void);
#endif
// Return pointer to the partition offset array
uint32_t *hcom_nx_fs_get_partition_offsets(void);

// Expects absolute byte starting address
int hcom_nx_flash_block_offset_to_part_offset(uint32_t flashBlockNum,
          struct mtd_dev_s **partMtd, uint32_t *partNumb,
          uint32_t *addrOfBlock);

// Partition aware MTD read function
int partMtdBRead(uint32_t startBlock, uint32_t numberOfBlocks,
        uint8_t *destBuff);

// Partition aware MTD write function
int partMtdBWrite(uint32_t startBlock, uint32_t numberOfBlocks,
        uint8_t *sourceBuff);

// Partition aware MTD erase function
int partMtdErase(uint32_t startBlock, uint32_t numberOfBlocks);

#endif // __INCLUDE_MEADOWOS_PARTITION_SHARE_H
