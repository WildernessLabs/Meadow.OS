/****************************************************************************
 * apps/database/sqlite/sqlite_meadow.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <nuttx/config.h>

#include "sqlite3.h"
#include "sqlite_meadow.h"

#include <syslog.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sqlite3_os_init
 *
 * Description:
 *  Called once by SQLite during sqlite3_initialize(), before any other
 *  SQLite API use. Building with SQLITE_OS_OTHER means neither the unix
 *  nor win32 VFS backends are compiled in (NuttX's LittleFS/QSPI storage
 *  doesn't support the POSIX file locking the unix backend assumes), so
 *  this application-supplied replacement must register the Meadow VFS
 *  itself.
 *
 ****************************************************************************/
SQLITE_API int sqlite3_os_init(void)
{
    sqlite3_vfs *pVfs = sqlite3_demovfs();
    if (pVfs == 0)
    {
        syslog(LOG_ERR, "Failed to create sqlite3_demovfs\n");
        return SQLITE_ERROR;
    }

    int ret = sqlite3_vfs_register(pVfs, 1);
    if (ret)
    {
        syslog(LOG_ERR, "Failed to register sqlite3_demovfs\n");
        return SQLITE_ERROR;
    }

    return SQLITE_OK;
}

SQLITE_API int sqlite3_os_end(void)
{
    return SQLITE_OK;
}
