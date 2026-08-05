/****************************************************************************
 * Meadow.OS/kerneltests/meadow_kt_log.c
 *
 *   Copyright (C) 2026 Wilderness Labs. All rights reserved.
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

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include <meadow/hcom_shared_common.h>
#include <meadow/hcom_traverse_ku.h>

#include "meadow_kt_log.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

//===============================================================
// Send a syslog-like message directly to the CLI that initiated the test,
// instead of through syslog(). This is needed by tests that use pins
// otherwise reserved for syslog UART output (e.g. PB14/PB15).
void syslogToHost(int priority, FAR const IPTR char *fmt, ...)
{
    size_t maxStringLen = 256;
    char *finalString = malloc(maxStringLen);

    va_list args;
    va_start(args, fmt);

    // The Nuttx version of snprintf will truncate the string based on the
    // buffer size but will always place a terminating NULL at the end.
    int stringLen = vsnprintf(finalString, maxStringLen - 1, fmt, args);

    hcom_traverse_ku_text_to_host(HCOM_HOST_REQUEST_TEXT_INFORMATION, finalString, stringLen);

    va_end(args);

    free(finalString);
}
