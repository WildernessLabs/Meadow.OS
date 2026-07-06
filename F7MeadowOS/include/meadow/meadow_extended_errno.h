/****************************************************************************
 * /Meadow.OS/include/meadow/meadow_extended_errno.h
 * 
 *   Copyright (C) 2025 Wilderness Labs. All rights reserved.
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
#ifndef __MEADOW_EXTENDED_ERRNO_H
#define __MEADOW_EXTENDED_ERRNO_H

// In nuttx/include/errno.h we find:
// "Users can add values starting here"
// #define __ELASTERROR        2000

// Something is wrong with the path name
#define EMEADOW_PATH_INVALID            (__ELASTERROR + 1)

// A problem with the path name slashes
#define EMEADOW_PATH_SLASHS             (EMEADOW_PATH_INVALID + 1)

// Don't know what the path problem is
#define EMEADOW_PATH_UNKNOWN            (EMEADOW_PATH_SLASHS + 1)

// The subdirectory depth is too deep
#define EMEADOW_PATH_SUBDIR_DEPTH       (EMEADOW_PATH_UNKNOWN + 1)

// The path is too long
#define EMEADOW_PATH_INVALID_CONFIG     (EMEADOW_PATH_SUBDIR_DEPTH + 1)

// The Caller is not in the required task group
#define EMEADOW_TASK_GRP_MISMATCH       (EMEADOW_PATH_INVALID_CONFIG + 1)

#endif /* __MEADOW_EXTENDED_ERRNO_H */