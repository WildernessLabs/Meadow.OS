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

// Partition sizes. There are pieces and parts scattered around. This does not
// need to be the "Master" list, just a single location for all partition size
// information.
//
// The File System partition size can only be determined after starting
// Meadow. Because we support different flash chips with different sizes.
#define MEADOW_FLASH_PART_SIZE_UNKNOWN      (0xffffffff)  // Discover at start
#define MEADOW_FLASH_PART_SIZE_ALL_RESERVED (HCOM_NX_FS_MONO_RAW_PARTITION_SIZE + \
                                             HCOM_NX_FS_OTA_RESERVED_SPACE)
#define MEADOW_FLASH_PART_SIZE_OS_RUNTIME   (HCOM_NX_FS_MONO_RAW_PARTITION_SIZE)
#define MEADOW_FLASH_PART_SIZE_OS_UPDATE    (HCOM_NX_FS_NUTTX_UPDATE_SIZE)
#define MEADOW_FLASH_PART_SIZE_OTA_STATE    (4096)
#define MEADOW_FLASH_PART_SIZE_OS_PERSIST   (4096)
#define MEADOW_FLASH_PART_SIZE_ASSERT_DATA  (HCOM_NX_MAXIMUM_ASSERTION_DATA_SIZE)
#define MEADOW_FLASH_PART_SIZE_UNUSED_SPACE (MEADOW_FLASH_PART_SIZE_ALL_RESERVED - \
                                            (MEADOW_FLASH_PART_SIZE_OS_RUNTIME + \
                                             MEADOW_FLASH_PART_SIZE_OS_UPDATE + \
                                             MEADOW_FLASH_PART_SIZE_OTA_STATE + \
                                             MEADOW_FLASH_PART_SIZE_OS_PERSIST + \
                                             MEADOW_FLASH_PART_SIZE_ASSERT_DATA))

// Partition assignment numbers
#define MEADOW_FLASH_PART_INFO_RUNTIME      (0)
#define MEADOW_FLASH_PART_INFO_NUTTX_UPDATE (1)
#define MEADOW_FLASH_PART_INFO_OTA_SPACE    (2)
#define MEADOW_FLASH_PART_INFO_ASSERT_DATA  (3)
#define MEADOW_FLASH_PART_INFO_UNUSED_SPACE (4)
#define MEADOW_FLASH_PART_INFO_FILE_SYS     (5)
#define MEADOW_FLASH_PART_INFO_PARENT_PART  (6) // The entire flash space
#define MEADOW_FLASH_PART_INFO_TOTAL        (6)

// This structure defines the information about each external flash partition
struct meadow_part_info
{
    struct mtd_dev_s *mtdPart;
    off_t byteOffset;
    off_t blockOffset;
    bool isPartMounted;
    size_t partSize;
};

// Obtains the flash partition information for a single partition
int hcom_nx_fs_get_partition_info(int partNumb, struct meadow_part_info *partInfo);

#endif // __INCLUDE_MEADOWOS_PARTITION_SHARE_H
