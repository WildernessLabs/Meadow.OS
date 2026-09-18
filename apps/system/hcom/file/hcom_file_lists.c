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
// Initial capacity of the explicit directory-walk stack (see
// hcom_file_lists_stack_frame_s below) used by
// hcom_file_lists_all_dev_dir_and_files(). Grows via realloc() as needed --
// this is just a starting point, not a depth limit.
#define HCOM_FILE_LISTS_INITIAL_STACK_CAPACITY  (8)

/****************************************************************************
 * Private Types
 ****************************************************************************/
// One entry per currently-open directory level of the iterative walk in
// hcom_file_lists_all_dev_dir_and_files(). The array of these lives on the
// heap, not the hcom task's own (small, fixed) stack, so directory nesting
// depth is limited only by available heap, not by risking a task-stack
// overflow (see Documentation/CodeReviewTracking.md F037).
struct hcom_file_lists_stack_frame_s
{
  DIR *dir;
  char path[256];
  int indent;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/


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
// Walks the directory tree rooted at 'name', listing every file and
// subdirectory to the CLI. Iterative -- uses an explicit, heap-allocated
// stack of open directory levels (growing via realloc() as needed) instead
// of recursing, so it can walk directory trees of any depth without risking
// the hcom task's own stack.
int hcom_file_lists_all_dev_dir_and_files(const char *name, int indent, uint32_t userData)
{
  char hostMsg[HCOM_SHORT_HOST_STRING_BUFF_LENGTH];
  struct hcom_file_lists_stack_frame_s *stack;
  int stackCapacity = HCOM_FILE_LISTS_INITIAL_STACK_CAPACITY;
  int stackTop;

  stack = malloc(stackCapacity * sizeof(*stack));
  if(stack == NULL)
  {
    syslog(LOG_ERR, "%s@%d-malloc returned NULL\n", __FILE__, __LINE__);
    return -ENOMEM;
  }

  // Push the root directory to begin the walk.
  stack[0].dir = opendir(name);
  if(stack[0].dir == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Could not open:%s as a directory\n",
              __FILE__, __LINE__, name);
    free(stack);
    return -1;
  }
  snprintf_chk(stack[0].path, sizeof(stack[0].path), "%s", name);
  stack[0].indent = indent;
  stackTop = 1;

  while(stackTop > 0)
  {
    struct hcom_file_lists_stack_frame_s *top = &stack[stackTop - 1];
    struct dirent *entry = readdir(top->dir);

    if(entry == NULL)
    {
      // Fully enumerated this level -- close it and resume the parent
      // level (if any) right where its own readdir() left off.
      closedir(top->dir);
      stackTop--;
      continue;
    }

    if(DIRENT_ISDIRECTORY(entry->d_type))
    {
      // Only show procfs information if userData == 1234
      if(userData != 1234 && strcmp(entry->d_name, "proc") == 0)
      {
        // Stop the whole walk here; close every level still open first.
        while(stackTop > 0)
        {
          closedir(stack[stackTop - 1].dir);
          stackTop--;
        }
        free(stack);
        return OK; // ignore procfs information
      }

      // Don't display /. and /..
      if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;

      snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH,
              "%*s%s/\n", top->indent, "", entry->d_name);
      hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION,
                0, hostMsg, __FILE__, __LINE__);

      char childPath[256];
      snprintf_chk(childPath, sizeof(childPath), "%s/%s", top->path, entry->d_name);
      int childIndent = top->indent + 3;

      DIR *childDir = opendir(childPath);
      if(childDir == NULL)
      {
        syslog(LOG_ERR, "%s@%d-Could not open:%s as a directory\n",
                  __FILE__, __LINE__, childPath);
        continue; // Skip this subdirectory, keep listing siblings
      }

      // Grow the stack if the newly-discovered subdirectory doesn't fit.
      if(stackTop == stackCapacity)
      {
        int newCapacity = stackCapacity * 2;
        struct hcom_file_lists_stack_frame_s *newStack =
            realloc(stack, newCapacity * sizeof(*stack));
        if(newStack == NULL)
        {
          syslog(LOG_ERR, "%s@%d-realloc returned NULL\n", __FILE__, __LINE__);
          closedir(childDir);
          while(stackTop > 0)
          {
            closedir(stack[stackTop - 1].dir);
            stackTop--;
          }
          free(stack);
          return -ENOMEM;
        }
        stack = newStack;
        stackCapacity = newCapacity;
      }

      stack[stackTop].dir = childDir;
      snprintf_chk(stack[stackTop].path, sizeof(stack[stackTop].path), "%s", childPath);
      stack[stackTop].indent = childIndent;
      stackTop++;
    }
    else
    {
      // All non-directory types
      char *entryType;
      if(DIRENT_ISFILE(entry->d_type)) {entryType = "file";}
      else if(DIRENT_ISCHR(entry->d_type)) {entryType = "char";}
      else if(DIRENT_ISBLK(entry->d_type)) {entryType = "block";}
      else if(DIRENT_ISLINK(entry->d_type)) {entryType = "link";}
      else if(DIRENT_ISFIFO(entry->d_type)) {entryType = "fifo";}
      else if(DIRENT_ISSEM(entry->d_type)) {entryType = "sem";}
      else if(DIRENT_ISMQ(entry->d_type)) {entryType = "mq";}
      else if(DIRENT_ISSHM(entry->d_type)) {entryType = "shm";}
      else if(DIRENT_ISMTD(entry->d_type)) {entryType = "mtd";}
      else if(DIRENT_ISSOCK(entry->d_type)) {entryType = "sock";}
      else if(DIRENT_ISUNKNOWN(entry->d_type)) {entryType = "unknown";}
      else {entryType = "";}

      if(strlen(entryType) > 0)
      {
        snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH,
                "%*s%s [%s]\n", top->indent, "", entry->d_name, entryType);
      }
      else
      {
        snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH,
                "%*s%s\n", top->indent, "", entry->d_name);
      }
      hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION,
                0, hostMsg, __FILE__, __LINE__);
    }
  }

  free(stack);
  return OK;
}
