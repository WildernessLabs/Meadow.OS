/****************************************************************************
 * /Meadow.OS/meadow_os/meadow_storage_mode.c
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

#include <nuttx/config.h>

#include <stdbool.h>
#include <string.h>
#include <syslog.h>

#include "../hcom_nx/hcom_nx_common.h"

#include <meadow/hcom_bbreg_defn.h>
#include <meadow/hcom_shared_common.h>
#include <meadow/meadow_storage_mode.h>

//===========================================================================
// Private data
//===========================================================================

// Latched on first use. See the note in meadow_storage_mode.h for why this
// must not be re-read on every call.
static bool _storageModeLatched = false;
static bool _storageModeActive = false;

//===========================================================================
// Public functions
//===========================================================================

//===========================================================================
// Reports whether this boot is running in storage mode, which is to say
// whether the .NET runtime has been disabled.
//
// The battery backed bit is set when the runtime is disabled, so the sense of
// the test below is not inverted despite how it reads.
bool meadow_storage_mode_active(void)
{
  if (!_storageModeLatched)
  {
    _storageModeActive =
        hcom_nx_bbreg_is_bbr_bit_set(HCOM_BBREG_USER_RQST_MONO_ENABLE_BIT);
    _storageModeLatched = true;

    syslog(LOG_NOTICE, "Storage mode %s, runtime %s\n",
           _storageModeActive ? "ACTIVE" : "inactive",
           _storageModeActive ? "disabled" : "enabled");
  }

  return _storageModeActive;
}

//===========================================================================
// Reports whether board-side writes to a path are allowed right now. See the
// header for why storage mode has to freeze the file system.
bool meadow_storage_mode_writes_permitted(const char *path)
{
  if (path == NULL)
  {
    return true;
  }

  // Only the Meadow file system is handed to a host, so nothing else needs
  // to be held still.

  if (strncmp(path, MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME,
              strlen(MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME)) != 0)
  {
    return true;
  }

  return !meadow_storage_mode_active();
}
