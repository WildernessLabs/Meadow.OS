/****************************************************************************
 * \configs\stm32f777zit6-meadow\src\hcom_nx\create_fs\hcom_nx_fs_littlefs.c
 * 
 *   Copyright (C) 2019 - 2020 Wilderness Labs. All rights reserved.
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

// This module contains code to support LittleFS

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_nx_common.h"

#ifdef CONFIG_FS_LITTLEFS

#include <errno.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <nuttx/mtd/mtd.h>
#include <fs/littlefs/littlefs/lfs.h>   // There must be a better way!
#include <dirent.h>
#include <errno.h>
#include <meadow/meadow_partition_share.h>

#if (HCOM_NX_CREATE_FS_INCLUDE_DEBUG_LOGGING > 0)
#pragma message "(--) hcom_nx_fs_littlefs.c"
#endif

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
// Individual partitions have already been created
int hcom_nx_create_one_littlefs_file_dev(uint32_t partNumb,
          struct mtd_dev_s *partitionMtd)
{
  int ret;
  char *partName = malloc(HCOM_NX_MAX_PATH_AND_FILE_BUFF_LENGTH);
  if(partName == NULL)
  {
    syslog(LOG_ERR, "%s@%d-malloc returned NULL\n", thisFile, __LINE__);
    return -ENOMEM;
  }

#if HCOM_NX_CREATE_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "%s@%d-Registering part %ld\n", thisFile, __LINE__, partNumb);
#endif

  if (partitionMtd == NULL)
  {
    syslog(LOG_ERR, "%s@%d-mtd is NULL, part %" PRId32 "",
             thisFile, __LINE__, partNumb);
    free(partName);
    return -1;
  }

  // result "/dev/little0p0", "/dev/little0p1", "/dev/little0p2"...
  snprintf_chk(partName, HCOM_NX_MAX_PATH_AND_FILE_BUFF_LENGTH, "%s0p%d",
          HCOM_NX_FILE_MOUNT_POINT_SOURCE, partNumb);

#if HCOM_NX_CREATE_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "%s@%d-Register part %ld as '%s'. MTD:%p\n",
           thisFile, __LINE__, partNumb, partName, partitionMtd);
#endif

  // Register the MTD driver so that it can be accessed from the VFS
  // Nuttx virtual file system
  ret = register_mtddriver(partName, partitionMtd, 0755, partitionMtd);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-register_mtddriver() ret:%d, errno:%d\n",
            thisFile, __LINE__, ret, errno);
    free(partName);
    return ret;
  }

  free(partName);
  return OK;
}
#endif

//==============================================================================
// This method is called to mount a partition
static int hcom_nx_create_littlefs_low_level_mount(const char *sourceDevice,
          const char *targetDevice, const char *fileSystemType,
          uint32_t partitionNumber, const char *mountCommand)
{
  int ret;

  char *finalSourceName = malloc(HCOM_NX_MAX_PATH_AND_FILE_BUFF_LENGTH);
  if(finalSourceName == NULL)
  {
    syslog(LOG_ERR, "%s@%d-malloc returned NULL\n", thisFile, __LINE__);
    return -ENOMEM;
  }
  
  // 'dev/little0p6'
  snprintf_chk(finalSourceName, HCOM_NX_MAX_PATH_AND_FILE_BUFF_LENGTH,
            "%s0p%d", sourceDevice, partitionNumber);

 #if HCOM_NX_CREATE_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "%s@%d-Mounting part:%ld as '%s' to '%s' type '%s', cmd '%s'\n",
        thisFile, __LINE__, partitionNumber, finalSourceName,
        MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME,
        fileSystemType, mountCommand);
 #endif

  // e.g. mount("/dev/littlefs0p6", "meadowOS"", "vfat", 0, NULL);  // Needs backing block device
  ret = mount(finalSourceName, MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME,
            fileSystemType, 0, mountCommand);
  if (ret < 0)
  {
    // The mount function puts the error code into errno
    // Return to the specific file system code to determine if formatting needed
    free(finalSourceName);
    return -errno;
  }
  
 #if HCOM_NX_CREATE_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "%s@%d-Mount successful\n", thisFile, __LINE__);
 #endif

  free(finalSourceName);

  return OK;
}

//=====================================================================================
// Mount each LittleFS partition and format, if needed
int hcom_nx_create_littlefs_mount_format_file_dev(uint32_t partNumb, const char *targetDevice)
{
  int ret;

#if HCOM_NX_CREATE_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "%s@%d-LittleFS mount part %ld\n", thisFile, __LINE__, partNumb);
#endif

  // First we'll attempt to mount the device. A mount failure with a specific
  // error return indicates formatting is needed. If this happens we'll call
  // again but provide the 'force mount' command.
  ret = hcom_nx_create_littlefs_low_level_mount
            (HCOM_NX_FILE_MOUNT_POINT_SOURCE, targetDevice,
            HCOM_NX_FILE_MOUNT_FILE_SYS_TYPE, partNumb, NULL);
  if(ret >= 0)
    return OK;   // Mount successful, exit

  // In Legacy Meadow the return value for a corrupt file system
  // was LFS_ERR_CORRUPT, which had a value of -52. A function has been added
  // to lfs_vfs.c [littlefs_convert_result()] that converts the LittleFS error
  // values to ones that match Nuttx, -52 is now -EFAULT.
  // 
  // While moving to Nutts V12.x I found that without erasing the flash chip
  // the error returned was -EINVAL. If this error was encountered formatting
  // this partition also resolved the file system differences.
  if(ret != -EFAULT && ret != -EINVAL)
  {
    syslog(LOG_ERR, "-->%s@%d-LittleFS mount as '%s' type '%s' for Part %lu, err:%d failed\n",
              thisFile, __LINE__, MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME,
              HCOM_NX_FILE_MOUNT_FILE_SYS_TYPE, partNumb, ret);
    return ret;
  }

  // Detected an error that indicates that LittleFS's file system needs to
  // be formatted.
#if HCOM_NX_CREATE_FS_INCLUDE_DEBUG_LOGGING > 0
  syslog(LOG_DEBUG, "Part %lu format required\n", partNumb);
#endif

  // This call will format then mount. Note: the last argument causes LittleFS
  // to format and then mount the partition. This is the 'forcemount' command.
  ret = hcom_nx_create_littlefs_low_level_mount(
            HCOM_NX_FILE_MOUNT_POINT_SOURCE,
            MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME, HCOM_NX_FILE_MOUNT_FILE_SYS_TYPE,
            partNumb, HCOM_NX_FILE_MOUNT_FORCE_FORMAT);
  if (ret < 0)
  {
    syslog(LOG_ERR, "--> %s@%d-Format and remount '%s' to '%s' for type '%s' on Part %lu, err:%d\n",
              thisFile, __LINE__, HCOM_NX_FILE_MOUNT_POINT_SOURCE,
              targetDevice, HCOM_NX_FILE_MOUNT_FILE_SYS_TYPE, partNumb, ret);
  }

#if HCOM_NX_CREATE_FS_INCLUDE_DEBUG_LOGGING > 0
  else
  {
    syslog(LOG_DEBUG, "Part %ld needed formatting\n", partNumb);
  }
#endif

  return ret;
}
