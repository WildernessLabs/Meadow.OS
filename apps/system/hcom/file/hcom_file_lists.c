/****************************************************************************
 * \apps\examples\hcom\file\hcom_file_lists.c
 * 
 *   Copyright (C) 2019 - 2023 Wilderness Labs. All rights reserved.
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

// This file contains the old file list scheme that not able to display
// subdirectories. The newer version is in hcom_file_list_subdir.c and
// supports  CLIv2 users. This file should be removed when CLIv1 is no
// longer supported.

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_common.h"
#include <meadow/hcom_protocol.h>
#include <meadow/hcom_shared_common.h>

#include <nuttx/config.h>
#include <dirent.h>
#include <sys/stat.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/
static char *thisFile = __FILE__;


#if defined (CONFIG_DIR_MGMT_TESTS)
#pragma message "(--) hcom_file_lists.c"
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/
static int hcom_file_lists_all_dev_dir_and_files(const char *name, int indent, uint32_t userData);

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// This undocumented feature is being used by some part of CLI, unsure of
// usage. Another /Meadow.OS/apps/system/hcom/tests/hcom_dir_mgmt_tests.c
int hcom_file_lists_all_dev_dir_and_files_start(uint32_t userData)
{
  // Changing "/" to "meadow0" will only show meadow files
  return hcom_file_lists_all_dev_dir_and_files("/", 0, userData);
}

//----------------------------------------------
// NOTE - Recursive function
int hcom_file_lists_all_dev_dir_and_files(const char *name, int indent, uint32_t userData)
{
  char hostMsg[HCOM_SHORT_HOST_STRING_BUFF_LENGTH];

  DIR *dir;
  struct dirent *entry;

  if (!(dir = opendir(name)))
  {
    syslog(LOG_ERR, "%s@%d-Could not open:%s as a directory\n",
              thisFile, __LINE__, name);
    return -1;
  }

  while ((entry = readdir(dir)) != NULL)
  {
    if (DIRENT_ISDIRECTORY(entry->d_type))
    {
      // Only show procfs information if userData == 1234
      if(userData != 1234 && strcmp(entry->d_name, "proc") == 0)
        return OK; // ignore procfs information

      // Don't display /. and /..
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;

      snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH,
              "%*s%s/\n", indent, "", entry->d_name);
      hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION,
                0, hostMsg, thisFile, __LINE__);

      char path[256];
      snprintf_chk(path, sizeof(path), "%s/%s", name, entry->d_name);
      
      // Recursion is here
      hcom_file_lists_all_dev_dir_and_files(path, indent + 3, userData);
    }
    else
    {
      // All non-directory types
      char *entryType;
      if(DIRENT_ISFILE(entry->d_type)) {entryType = "file";}
      else if(DIRENT_ISCHR(entry->d_type)) {entryType = "char";}
      else if(DIRENT_ISBLK(entry->d_type)) {entryType = "block";}
      else if(DIRENT_ISLINK(entry->d_type)) {entryType = "link";}
      else {entryType = "????";}
      snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH,
              "%*s%s [%s]\n",indent, "", entry->d_name, entryType);
      hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION,
                0, hostMsg, thisFile, __LINE__);
    }
  }

  closedir(dir);
  return OK;
}
