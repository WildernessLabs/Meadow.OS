/****************************************************************************
 * /Meadow.OS/hcom_nx/create_fs/hcom_nx_fs_flash_tests.c
 *
 *  Copyright (C) 2019 - 2025 Wilderness Labs. All rights reserved.
 *   Author: Peter Moody
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

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include "../hcom_nx_common.h"
#include <meadow/hcom_shared_common.h>
#include <meadow/meadow_partition_share.h>

#if MEADOW_INCLUDE_CODE_FOR_TESTING_5MB_OF_FLASH > 0
#pragma message "(--) hcom_nx_fs_flash_tests.c"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static char *thisFile = __FILE__;

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEADOW_FLASH_PART_TESTS_USE_PARTITIONS (1)

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

// Verifies specific pattern has been written in the first 5 MB of flash
// correctly. It uses syslog to display the data on both side of the flash
// boundaries. It is called using 'developer -p 10 -v 2'.
void hcom_nx_flash_testing_verify_regions()
{
#if HCOM_INCLUDE_DIAG_PRINT_BUFFER_CODE > 0
  #define HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY (2)

  int ret;
  uint32_t segmentLen;
  uint32_t prevUseSpace = 0;
  uint32_t full5MBFlash = MEADOW_FLASH_PART_SIZE_ALL_RESERVED;
  uint32_t startBlock;

  // Find the boundary address and dump 512 bytes (256 before to 256 after).
  uint32_t block_size = hcom_nx_exec_ex_flash_get_block_size();

  uint8_t *flashBuffer = zalloc(HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY * block_size);
  if(flashBuffer == NULL)
  {
    syslog(2, "%s@%d-Error zalloc == NULL\n", thisFile, __LINE__);
    usleep(3000 * 1000);
    return;
  }

  // The segments all begin and end on block boundaries, therefore at least
  // 2 blocks must normally be read and the last bit of the first and the
  // first bit of the second are output separately. 
  //----------------------------------------------------------------------
  // 1 - Special case starts at offset = 0, so only 1 block shown, the first
  startBlock = prevUseSpace / block_size;
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock,
            (void *) flashBuffer, HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY / 2);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret);
    usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }
  syslog(2, "\nTop of 1st (Runtime)\n");
  hcom_nx_diag_print_buffer(flashBuffer, block_size, 1);

  prevUseSpace = MEADOW_FLASH_PART_SIZE_MONO_RUNTIME;

  //----------------------------------------------------------------------
  // 2 - 
  startBlock = prevUseSpace / block_size;
  // Start reading 1 block before the beginning of the segment and end after
  // 2 blocks have been displayed.
  startBlock -= HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY / 2; // 1 block before, 1 after

#if MEADOW_FLASH_PART_TESTS_USE_PARTITIONS > 0
  // Read the block before boundary into start of buffer
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock,
            (void *) flashBuffer, 1);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }

  // Read the block after boundary into end of buffer
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock + 1,
            (void *) flashBuffer + block_size, 1);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }
#else
  // Read the block before and the block after
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock,
            (void *) flashBuffer, HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }
#endif

  syslog(2, "\nBottom of 1st and Top of 2nd (Meadow Update)\n");
  hcom_nx_diag_print_buffer(flashBuffer,
            block_size * HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY, 1);

  prevUseSpace += MEADOW_FLASH_PART_SIZE_OS_UPDATE;

  //----------------------------------------------------------------------
  // 3
  segmentLen = MEADOW_FLASH_PART_SIZE_OTA_STATE;

  // Backup 1/2 256 so boundary is at the center of the dump
  startBlock = prevUseSpace / block_size;
  startBlock -= HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY / 2; // 1 block before, 1 after

#if MEADOW_FLASH_PART_TESTS_USE_PARTITIONS > 0
  // Read the block before boundary into start of buffer
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock,
            (void *) flashBuffer, 1);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }

  // Read the block after boundary into end of buffer
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock + 1,
            (void *) flashBuffer + block_size, 1);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }
#else
  // Read the block before and the block after
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock,
            (void *) flashBuffer, HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }
#endif

  syslog(2, "\nBottom of 2nd and Top of 3rd (OTA State)\n");
  hcom_nx_diag_print_buffer(flashBuffer,
            block_size * HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY, 1);

  prevUseSpace += segmentLen;

  //----------------------------------------------------------------------
  // 4 -Persisted data
  segmentLen = hcom_nx_exec_ex_flash_get_erase_block_size();

  // Backup 1/2 256 so boundary is at the center of the dump
  startBlock = prevUseSpace / block_size;
  startBlock -= HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY / 2; // 1 block before, 1 after

#if MEADOW_FLASH_PART_TESTS_USE_PARTITIONS > 0
  // Read the block before boundary into start of buffer
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock,
            (void *) flashBuffer, 1);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }

  // Read the block after boundary into end of buffer
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock + 1,
            (void *) flashBuffer + block_size, 1);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }
#else
  // Read the block before and the block after
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock,
            (void *) flashBuffer, HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }
#endif

  syslog(2, "\nBottom of 3rd and Top of 4th (OS Persisted)\n");
  hcom_nx_diag_print_buffer(flashBuffer,
            block_size * HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY, 1);

  prevUseSpace += segmentLen;

  //----------------------------------------------------------------------
  // 5 - Assert
  segmentLen = MEADOW_FLASH_PART_SIZE_ASSERT_DATA;

  // Backup 1/2 256 so boundary is at the center of the dump
  startBlock = prevUseSpace / block_size;
  startBlock -= HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY / 2; // 1 block before, 1 after

#if MEADOW_FLASH_PART_TESTS_USE_PARTITIONS > 0
  // Read the block before boundary into start of buffer
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock,
            (void *) flashBuffer, 1);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }

  // Read the block after boundary into end of buffer
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock + 1,
            (void *) flashBuffer + block_size, 1);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }
#else
  // Read the block before and the block after
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock,
            (void *) flashBuffer, HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }
#endif

  syslog(2, "\nBottom of 4th and Top of 5th (Assert Data)\n");
  hcom_nx_diag_print_buffer(flashBuffer,
            block_size * HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY, 1);

  prevUseSpace += segmentLen;

  //----------------------------------------------------------------------
  // 6 - Unused before file system
  segmentLen = full5MBFlash - prevUseSpace;

  // Backup 1/2 256 so boundary is at the center of the dump
  startBlock = prevUseSpace / block_size;
  startBlock -= HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY / 2;   // 1 block before, 1 after

#if MEADOW_FLASH_PART_TESTS_USE_PARTITIONS > 0
  // Read the block before boundary into start of buffer
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock,
            (void *) flashBuffer, 1);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }

  // Read the block after boundary into end of buffer
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock + 1,
            (void *) flashBuffer + block_size, 1);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }
#else
  // Read the block before and the block after
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock,
            (void *) flashBuffer, HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }
#endif

  syslog(2, "\nBottom of 5th and Top of 6th (Reserved)\n");
  hcom_nx_diag_print_buffer(flashBuffer,
            block_size * HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY, 1);

  prevUseSpace += segmentLen;   // Now offset is start of file system

  // ----------------------------------------------------------------
  // Show the end too + 256 of file system
  startBlock = prevUseSpace / block_size;
  startBlock -= HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY / 2;   // 1 block before, 1 after

#if MEADOW_FLASH_PART_TESTS_USE_PARTITIONS > 0
  // Read the block before boundary into start of buffer
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock,
            (void *) flashBuffer, 1);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }
  
  syslog(2, "\nBottom of 6th partition\n");
  hcom_nx_diag_print_buffer(flashBuffer, block_size, 1);

  // Attempt to read the block after boundary, which is the file system.
  // This read should FAIL.
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock + 1,
            (void *) flashBuffer + block_size, 1);
  if(ret < 0)
  {
    syslog(2, "%s@%d-This \"successfully\" failed:%d\n",
              thisFile, __LINE__, ret); usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }
  syslog(2, "\nStart of File System\n");
  hcom_nx_diag_print_buffer(flashBuffer+ block_size , block_size, 1);
#else
  // Read the final 2 blocks this will have no problem reading the file system
  ret = hcom_nx_exec_ex_flash_copy_blocks_to_memory(startBlock,
            (void *) flashBuffer, HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret);
    usleep(3000 * 1000);
    free(flashBuffer);
    return;
  }

  syslog(2, "\nBottom of 6th and Start of File System\n");
  hcom_nx_diag_print_buffer(flashBuffer,
            block_size * HCOM_NX_EX_FLASH_BLOCKS_TO_VERIFY, 1);

  free(flashBuffer);
#endif
#endif    // #if HCOM_INCLUDE_DIAG_PRINT_BUFFER_CODE > 0
}

//===============================================================
// Populates buffer for flashing. Used for testing below
static void hcom_nx_exec_ex_fill_layout_buffer(uint32_t writeBufLen,
          char *writeBuf, char *titleMsg, int fillChar)
{
  int offset;
  uint32_t titleLen = strlen(titleMsg);

  for(offset = 0; offset < titleLen; offset++)
    writeBuf[offset] = titleMsg[offset];

  ASSERT(offset == titleLen);

  for(; offset < writeBufLen; offset++)
    writeBuf[offset] = fillChar;
}

//=====================================================================
// Fills all 6 regions of the 5MB space in the external flash with easily
// identifiable data.
//
// This assumes Fake files for Meadow.OS.Runtime.bin and
// Meadow.OS.Update.bin. are ready to be read from the Meadow file system
// and used to populate the related spaces.
// It is called using 'developer -p 10 -v 1'.
void hcom_nx_flash_testing_fill_5mb_of_flash()
{  
  int ret;
  char *titleMsg;
  char *writeBuf;
  uint32_t writeBufLen;
  uint32_t prevUseSpace = 0;

  syslog(2, "%s@%d-**>> Please wait, 5 MB of flash being populated <<**\n", thisFile, __LINE__);

  // Fake files generated by /Meadow.OS/alphaTooling/create_mock_sysfiles.py
  // Use something like the following to download the fake files to Meadow.
  // meadow file write -f C:\HcomFiles\Write\Text\fake\Meadow.OS.Runtime.bin.fake -t /meadow0/update/os/Meadow.OS.Runtime.bin
  // meadow file write -f C:\HcomFiles\Write\Text\fake\Meadow.OS.Update.bin.fake -t /meadow0/update/os/Meadow.OS.Update.bin

  //----------------------------------------------------------------
  // First is 3MB for runtime. Assumes Meadow.OS.Runtime.bin has been
  // replaced with a fake version in Meadow.
  // File is filled with a title string then filled with the 'A' character
  syslog(2, "%s@%d-Region 1 Writing %7u bytes at offset 0x%08lx\n",
            thisFile, __LINE__,
            MEADOW_FLASH_PART_SIZE_MONO_RUNTIME, prevUseSpace);
  usleep(100 * 1000);

  // Can't use hcom_nx_exec_ex_flash_OS_update_flash2 it deletes the file.
  // Looks for '/meadow0/update/os/Meadow.OS.Runtime.bin'
  ret = flash_file(UPDATE_OS_DIR HCOM_NX_FS_MONO_RUNTIME_FILENAME,
            MEADOW_FLASH_PART_SIZE_MONO_RUNTIME, 0x0);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); 
    return;
  }
  prevUseSpace = MEADOW_FLASH_PART_SIZE_MONO_RUNTIME;

  //----------------------------------------------------------------
  // Second is a 1835008 byte file Meadow.OS.Update.bin
  // File is filled with 'B' characters after title string
  syslog(2, "%s@%d-Region 2 Writing %7u bytes at offset 0x%08lx\n",
            thisFile, __LINE__,
            MEADOW_FLASH_PART_SIZE_OS_UPDATE, prevUseSpace);
  usleep(100 * 1000);

  // Can't use hcom_nx_exec_ex_flash_OS_update_flash1 it deletes the file.
  // Looks for '/meadow0/update/os/Meadow.OS.Update.bin'
  ret = flash_file(UPDATE_OS_DIR HCOM_NX_FS_NUTTX_UPDATE_FILENAME,
            MEADOW_FLASH_PART_SIZE_OS_UPDATE,
            MEADOW_FLASH_PART_SIZE_MONO_RUNTIME);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    return;
  }

  prevUseSpace += MEADOW_FLASH_PART_SIZE_OS_UPDATE;

  //----------------------------------------------------------------
  // Third is the OTAState
  // 
  titleMsg = "This is Segment 3 used for OTAState, fill with D.";
  writeBufLen = MEADOW_FLASH_PART_SIZE_OTA_STATE;
  syslog(2, "%s@%d-Region 3 Writing %7lu bytes at offset 0x%08lx\n", thisFile, __LINE__,
            writeBufLen, prevUseSpace);
  usleep(100 * 1000);

  writeBuf = zalloc(writeBufLen);
  // Write the title message to the buffer, then the fill character
  hcom_nx_exec_ex_fill_layout_buffer(writeBufLen, writeBuf, titleMsg, 'D');

  // Write the writeBuf to the flash
  // See hcom_nx_exec_ex_flash_OS_update_flash1() for rational
  ASSERT(MEADOW_FLASH_PART_SIZE_MONO_RUNTIME + MEADOW_FLASH_PART_SIZE_OS_UPDATE == prevUseSpace);
  ret = hcom_nx_exec_ex_flash_write_buffer_to_flash((uint8_t*) writeBuf, writeBufLen,
            MEADOW_FLASH_PART_SIZE_MONO_RUNTIME + MEADOW_FLASH_PART_SIZE_OS_UPDATE);

  free(writeBuf);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    return;
  }

  prevUseSpace += writeBufLen;

  //----------------------------------------------------------------
  // Fourth is a region for persisted data.
  titleMsg = "This is Segment 4 used for persisted data, fill with H.";
  writeBufLen = MEADOW_FLASH_PART_SIZE_OS_PERSIST;
  syslog(2, "%s@%d-Region 4 Writing %7lu bytes at offset 0x%08lx\n", thisFile, __LINE__,
            writeBufLen, prevUseSpace);
  usleep(100 * 1000);

  writeBuf = zalloc(writeBufLen);
  hcom_nx_exec_ex_fill_layout_buffer(writeBufLen, writeBuf, titleMsg, 'H');

  uint32_t fieldLocation = hcom_nx_exec_ex_flash_persistent_data_location();
  ret = hcom_nx_exec_ex_flash_write_buffer_to_flash((uint8_t *)writeBuf, writeBufLen, fieldLocation);
  free(writeBuf);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    return;
  }
  prevUseSpace += writeBufLen;

  //----------------------------------------------------------------
  // Fifth is a 32KB region for assert data
  titleMsg = "This is Segment 5 used for assertion data, fill with P.";
  writeBufLen = MEADOW_FLASH_PART_SIZE_ASSERT_DATA;
  syslog(2, "%s@%d-Region 5 Writing %7lu bytes at offset 0x%08lx\n", thisFile, __LINE__,
            writeBufLen, prevUseSpace);
  usleep(100 * 1000);

  writeBuf = zalloc(writeBufLen);
  hcom_nx_exec_ex_fill_layout_buffer(writeBufLen, writeBuf, titleMsg, 'P');

  ret = hcom_nx_exec_ex_flash_write_assertion_data(writeBuf, writeBufLen);
  free(writeBuf);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    return;
  }

  prevUseSpace += writeBufLen;

  //----------------------------------------------------------------
  // 6th is all remaining space in the 5MB flash
  // All the remaining flash space within the 5MB allocation
  titleMsg = "This is Segment 6 currently not used, fill with Q.";
  uint32_t reservedFlashLen = MEADOW_FLASH_PART_SIZE_MONO_RUNTIME + \
            MEADOW_FLASH_PART_SIZE_OTHER_RESERVED;
  writeBufLen = reservedFlashLen - prevUseSpace;
  syslog(2, "%s@%d-Region 6 Writing %7lu bytes at offset 0x%08lx\n", thisFile, __LINE__,
              writeBufLen, prevUseSpace);
  usleep(100 * 1000);

  writeBuf = zalloc(writeBufLen);
  hcom_nx_exec_ex_fill_layout_buffer(writeBufLen, writeBuf, titleMsg, 'Q');

  // offset is the offset after the previous segment.
  ret = hcom_nx_exec_ex_flash_write_buffer_to_flash((uint8_t *)writeBuf, writeBufLen,
            prevUseSpace);
  free(writeBuf);
  if(ret < 0)
  {
    syslog(2, "%s@%d-Error:%d\n", thisFile, __LINE__, ret); usleep(3000 * 1000);
    return;
  }

  syslog(2, "%s@%d-First 5 MB flashed ready for testing\n", thisFile, __LINE__);

  // Test flash. It should all be filled
  ASSERT((prevUseSpace + writeBufLen) == reservedFlashLen);
}
#endif  // #if MEADOW_INCLUDE_CODE_FOR_TESTING_5MB_OF_FLASH > 0
