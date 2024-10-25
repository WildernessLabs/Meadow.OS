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

// Partition definitions
#define MEADOW_FLASH_PARTITION_MONO_PARTITION   (0)
#define MEADOW_FLASH_PARTITION_NUTTX_UPDATE     (1)
#define MEADOW_FLASH_PARTITION_OTA_SPACE        (2)
#define MEADOW_FLASH_PARTITION_ASSERT_DATA      (3)
#define MEADOW_FLASH_PARTITION_FILE_SYS         (4)

#define MEADOW_FLASH_TOTAL_NUMB_PARTITIONS      (5)

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
