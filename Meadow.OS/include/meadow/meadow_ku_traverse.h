/****************************************************************************
 * \include\meadow\meadow_ku_traverse.h
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
 *   - A fixed-size envelope (meadow_ku_traverse_msg_t) is written atomically
 *     because sizeof(envelope) < PIPE_BUF (1024 bytes on NuttX).  No framing
 *     or mutex is required.
 *   - Blocking write() on the kernel side provides natural back-pressure: the
 *     kernel caller stalls when the pipe buffer is full rather than dropping.
 *   - The user side opens the FIFO O_RDONLY | O_NONBLOCK so the open returns
 *     immediately without waiting for the kernel write end.  O_NONBLOCK is
 *     removed after the open so that read() in the receive thread blocks.
 *   - The kernel side opens O_WRONLY lazily on the first send() call.  By
 *     that time the user side is already open, so the open does not block.
 */

#ifndef __INCLUDE_MEADOW_KU_TRAVERSE_H
#define __INCLUDE_MEADOW_KU_TRAVERSE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Maximum payload bytes per message.  The total envelope size must stay
 * below PIPE_BUF (1024 bytes on NuttX) to guarantee an atomic write().
 */

#define MEADOW_KU_TRAVERSE_PAYLOAD_MAX  256

/* Path of the named FIFO used to cross the kernel/user boundary. */

#define MEADOW_KU_TRAVERSE_PIPE_PATH    "/dev/meadow_ku_traverse"

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Discriminator used to route each message to the correct handler. */

enum MeadowKUTraverseType
{
  MEADOW_KU_TRAVERSE_TYPE_UNKNOWN = 0,      /* Start of enum              */
  MEADOW_KU_TRAVERSE_TYPE_SYSLOG_TEXT,      /* kernel syslog / trace text */
  MEADOW_KU_TRAVERSE_TYPE_GENERIC_TEXT,     /* generic snapshot text      */
  MEADOW_KU_TRAVERSE_TYPE_GENERIC_STRING,   /* NULL terminated char array */
  MEADOW_KU_TRAVERSE_TYPE_COUNT             /* sentinel — keep last       */
};

/* Fixed-size envelope transferred atomically through the FIFO.
 * Total size: 4 (TraverseType) + 2 (rqstType)  + 2 (length)+ 2 (pad) + 256 (payload) = 264 bytes,
 * well within PIPE_BUF.
 */
struct meadow_ku_traverse_msg_t
{
  enum MeadowKUTraverseType traverseType;
  uint16_t                  rqstType;
  uint16_t                  length;
  uint8_t                   payload[MEADOW_KU_TRAVERSE_PAYLOAD_MAX];
};

/* Handler callback invoked on the user-side receive thread. */

typedef void (*meadow_ku_traverse_handler_t)
    (const struct meadow_ku_traverse_msg_t *msg);

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Kernel-side API -------------------------------------------------------- */

/* Create the named FIFO.  Call once from kernel init before user-space
 * starts.  Does not open the write end; that happens lazily on the first
 * meadow_ku_traverse_send() call once the user side is ready.
 *
 * Returns 0 on success, -errno on failure.
 */

int meadow_ku_traverse_kernel_setup(void);

// These allow access to meadow_ku_traverse_send
int hcom_ku_traverse_syslog_to_host(char *syslogMsg, size_t syslogLen);
int hcom_ku_traverse_text_to_host(uint16_t requestType,
        char *textMsg, size_t textLen);
int hcom_ku_traverse_string_to_host(uint16_t requestType, char *stringMsg);

/* Marshal data into an envelope and write it atomically to the FIFO.
 * Blocks on back-pressure if the pipe buffer is full.  Returns -ENXIO and
 * drops silently if the user side has not yet opened the read end.
 * len must be <= MEADOW_KU_TRAVERSE_PAYLOAD_MAX.
 *
 * Returns 0 on success, -errno on failure.
 */
int meadow_ku_traverse_send_string(enum MeadowKUTraverseType type,
  uint16_t requestType, const char *string);

int meadow_ku_traverse_send(enum MeadowKUTraverseType type,
  uint16_t requestType, const void *data, uint16_t len);

/* User-side API ---------------------------------------------------------- */

/* Open the FIFO for reading and spawn the receive thread.  Call early in
 * hcom startup before any kernel code calls meadow_ku_traverse_send().
 *
 * Returns 0 on success, -errno on failure.
 */

int meadow_ku_traverse_init_user_setup(void);

// /* Register a handler for a message type.  At most one handler per type;
//  * calling again replaces the previous handler.
//  *
//  * Returns 0 on success, -EINVAL if type is out of range or handler is NULL.
//  */

// int meadow_ku_traverse_register_handler(enum MeadowKUTraverseType type,
//                                          meadow_ku_traverse_handler_t handler);

#endif /* __INCLUDE_MEADOW_KU_TRAVERSE_H */
