/****************************************************************************
 * Meadow.OS/include/meadow/hcom_traverse_ku.h
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

/* Reliable kernel-to-user text transport via a named FIFO (pipe).
 *
 * Design notes:
 *   - A fixed-size envelope (hcom_traverse_ku_msg_t) is written atomically
 *     because sizeof(envelope) < PIPE_BUF (512 bytes on NuttX; not to be
 *     confused with CONFIG_DEV_PIPE_SIZE, the pipe's ring-buffer capacity,
 *     which defaults to 1024).  No framing or mutex is required.
 *   - Blocking write() on the kernel side provides natural back-pressure: the
 *     kernel caller stalls when the pipe buffer is full rather than dropping.
 *   - The user side opens the FIFO O_RDONLY | O_NONBLOCK so the open returns
 *     immediately without waiting for the kernel write end.  O_NONBLOCK is
 *     removed after the open so that read() in the receive thread blocks.
 *   - The kernel side opens O_WRONLY lazily on the first send() call.  By
 *     that time the user side is already open, so the open does not block.
 */

#ifndef __INCLUDE_HCOM_TRAVERSE_KU_H__
#define __INCLUDE_HCOM_TRAVERSE_KU_H__

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Maximum payload bytes per message.  The total envelope size must stay
 * below PIPE_BUF (512 bytes on NuttX) to guarantee an atomic write().
 */

#define HCOM_TRAVERSE_KU_PAYLOAD_MAX  256

/* Path of the named FIFO used to cross the kernel/user boundary. */

#define HCOM_TRAVERSE_KU_PIPE_PATH    "/dev/hcom_traverse_ku"

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Discriminator used to route each message to the correct handler. */

enum HcomTraverseKUType
{
  HCOM_TRAVERSE_KU_TYPE_UNKNOWN = 0,      /* Start of enum              */
  HCOM_TRAVERSE_KU_TYPE_SYSLOG_TEXT,      /* kernel syslog / trace text */
  HCOM_TRAVERSE_KU_TYPE_GENERIC_TEXT,     /* generic snapshot text      */
  HCOM_TRAVERSE_KU_TYPE_GENERIC_STRING,   /* NULL terminated char array */
  HCOM_TRAVERSE_KU_TYPE_COUNT             /* sentinel -- keep last      */
};

/* Fixed-size envelope transferred atomically through the FIFO.
 * Total size: 4 (TraverseType) + 2 (rqstType)  + 2 (length) + 256 (payload) = 264 bytes,
 * well within PIPE_BUF.
 */
struct hcom_traverse_ku_msg_t
{
  enum HcomTraverseKUType traverseType;
  uint16_t                  rqstType;
  uint16_t                  length;
  uint8_t                   payload[HCOM_TRAVERSE_KU_PAYLOAD_MAX];
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Kernel-side API -------------------------------------------------------- */

/* Create the named FIFO.  Call once from kernel init before user-space
 * starts.  Does not open the write end; that happens lazily on the first
 * hcom_traverse_ku_send() call once the user side is ready.
 *
 * Returns 0 on success, -errno on failure.
 */
int hcom_traverse_ku_kernel_setup(void);

// These allow access to hcom_traverse_ku_send

/* Not safe to call from ISR/interrupt context: the write path
 * (pipecommon_write) takes a blocking mutex and will assert or hang if
 * called from a real interrupt handler.
 */
int hcom_traverse_ku_syslog_to_host(char *syslogMsg, size_t syslogLen);
int hcom_traverse_ku_text_to_host(uint16_t requestType,
        char *textMsg, size_t textLen);
int hcom_traverse_ku_string_to_host(uint16_t requestType, char *stringMsg);
int hcom_traverse_ku_send_string(enum HcomTraverseKUType type,
  uint16_t requestType, const char *string);

int hcom_traverse_ku_send(enum HcomTraverseKUType type,
  uint16_t requestType, const void *data, uint16_t len);

/* User-side API ---------------------------------------------------------- */

/* Open the FIFO for reading and spawn the receive thread.  Call early in
 * hcom startup before any kernel code calls hcom_traverse_ku_send().
 *
 * Returns 0 on success, -errno on failure.
 */

int hcom_traverse_ku_init_user_setup(void);

#endif /* __INCLUDE_HCOM_TRAVERSE_KU_H__ */
