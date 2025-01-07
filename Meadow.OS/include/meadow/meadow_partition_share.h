/****************************************************************************
 * Meadow.OS/Meadow.OS/include/meadow/meadow_partition_share.h
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

// Note: The naming and definitions for the flash segments in the Original
// Meadow OS evolved over several years. Therefore, in Original Meadow,
// pieces and parts of each flash region have been spread in different
// places. Mark Stevens did a great job of consolidating the definitions
// in 'include/meadow/hcom_shared_common.h'. Three of these definitions are
// used below. Other definitions where not defined until now.
//
// Using partitions should make it much easier to modify the flash layout if
// this ever becomes necessary.

// 3MB is for the Meadow OS
#define MEADOW_FLASH_PART_SIZE_MONO_RUNTIME   (HCOM_NX_FS_MONO_RAW_PARTITION_SIZE)

// 2MB is reserved for other uses
#define MEADOW_FLASH_PART_SIZE_OTHER_RESERVED (HCOM_NX_FS_OTA_RESERVED_SPACE)

// The remaining for OS Update is 2MB - 256kB = 0x1C0000
#define MEADOW_FLASH_PART_SIZE_OS_UPDATE      (HCOM_NX_FS_NUTTX_UPDATE_SIZE)

// All reserved totals 3MB + 2MB = 5MB
#define MEADOW_FLASH_PART_SIZE_ALL_RESERVED   (MEADOW_FLASH_PART_SIZE_MONO_RUNTIME + \
                                               MEADOW_FLASH_PART_SIZE_OTHER_RESERVED)

// The following define flash within the additional 256kB of data
#define MEADOW_FLASH_PART_SIZE_OTA_STATE      (4096)
#define MEADOW_FLASH_PART_SIZE_OS_PERSIST     (4096)
#define MEADOW_FLASH_PART_SIZE_ASSERT_DATA    (HCOM_NX_MAXIMUM_ASSERTION_DATA_SIZE)

// How much space is currently unused?
#define MEADOW_FLASH_PART_SIZE_UNUSED_SPACE  (MEADOW_FLASH_PART_SIZE_ALL_RESERVED - \
                                                (MEADOW_FLASH_PART_SIZE_MONO_RUNTIME + \
                                                 MEADOW_FLASH_PART_SIZE_OS_UPDATE + \
                                                 MEADOW_FLASH_PART_SIZE_OTA_STATE + \
                                                 MEADOW_FLASH_PART_SIZE_OS_PERSIST + \
                                                 MEADOW_FLASH_PART_SIZE_ASSERT_DATA))

// The final partition (the File System) size, can only be determined after
// starting Meadow. This is because it supports different flash chips with
// different sizes. The File System partition size is all remaining space.
#define MEADOW_FLASH_PART_SIZE_UNKNOWN        (0xffffffff)

// Partition assignment numbers
#define MEADOW_FLASH_PART_NUMB_RUNTIME      (0)
#define MEADOW_FLASH_PART_NUMB_NUTTX_UPDATE (1)
#define MEADOW_FLASH_PART_NUMB_OTA_SPACE    (2)
#define MEADOW_FLASH_PART_NUMB_OS_PERSIST   (3)
#define MEADOW_FLASH_PART_NUMB_ASSERT_DATA  (4)
#define MEADOW_FLASH_PART_NUMB_UNUSED_SPACE (5)
#define MEADOW_FLASH_PART_NUMB_FILE_SYS     (6)
#define MEADOW_FLASH_PART_TOTAL_PARTITIONS  (7) // Total number of partitions
#define MEADOW_FLASH_PART_NUMB_PARENT_PART  (7) // Represents QSPI flash space

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
int hcom_nx_fs_get_partition_info(int partNumb,
                    meadow_part_info_t *partInfo,
                    struct hcom_nx_cmd_data *cmdData);

#endif // __INCLUDE_MEADOWOS_PARTITION_SHARE_H
