/****************************************************************************
 * meadow_watchdog.h
 *
 *   Copyright (C) 2024 Wilderness Labs. All rights reserved.
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

/* Interface to the software watchdog timers used to detect a wedged ESP32
 * coprocessor.  See meadow_watchdog.c for the implementation notes.
 */

#pragma once

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/compiler.h>
#include <nuttx/wdog.h>

#include <meadow/hcom_shared_common.h>
#include <meadow/meadow_os.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Watchdog timeouts, in milliseconds.  meadow_watchdog_activate() converts
 * these to clock ticks.
 */

#define WATCHDOG_DEFAULT_TIMEOUT_MILLISECONDS (60 * 1000)
#define WATCHDOG_POLL_TIMEOUT_MILLISECONDS    (WATCHDOG_DEFAULT_TIMEOUT_MILLISECONDS)
#define WATCHDOG_CLOSE_TIMEOUT_MILLISECONDS   (WATCHDOG_DEFAULT_TIMEOUT_MILLISECONDS)
#define WATCHDOG_SOCKET_TIMEOUT_MILLISECONDS  (WATCHDOG_DEFAULT_TIMEOUT_MILLISECONDS)

/* Returned by meadow_watchdog_activate() when no watchdog could be armed.
 * Passing it to meadow_watchdog_deactivate() is harmless, so callers do not
 * need to special case it.
 */

#define MEADOW_WATCHDOG_NO_SLOT  (-1)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Opaque handle to an armed watchdog. */

typedef int meadow_watchdog_t;

/* The operation being guarded.  The value is passed to the expiry callback
 * and selects the exception raised into the managed environment.
 */

typedef enum
{
  POLL_WATCHDOG = 0,
  CLOSE_WATCHDOG,
  SOCKET_WATCHDOG,
  MEADOW_WATCHDOG_METHOD_COUNT
} meadow_watchdog_methods_e;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_MEADOW_WATCHDOGS

meadow_watchdog_t meadow_watchdog_activate(uint32_t timeout,
                                    meadow_watchdog_methods_e method);

void meadow_watchdog_deactivate(meadow_watchdog_t watchdog);

#else

static inline meadow_watchdog_t
meadow_watchdog_activate(uint32_t timeout, meadow_watchdog_methods_e method)
{
  UNUSED(timeout);
  UNUSED(method);
  return MEADOW_WATCHDOG_NO_SLOT;
}

static inline void meadow_watchdog_deactivate(meadow_watchdog_t watchdog)
{
  UNUSED(watchdog);
}

#endif /* CONFIG_MEADOW_WATCHDOGS */
