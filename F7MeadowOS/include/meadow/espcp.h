/****************************************************************************
 * include/meadow/espcp.h
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
 *
 *  Values shared between the kernel-side ESP32 coprocessor driver
 *  (F7MeadowOS/espcp/) and the user-space host-communication application
 *  (apps/system/hcom/).  Both sides open the same named message queues and
 *  exchange the same encoded event records, so these definitions must agree
 *  exactly; they were previously duplicated in espcp_queue.h,
 *  espcp_encoders.h and apps/system/hcom/misc/espcp_utils.h.
 *
 *  Note this is *not* the place for values shared with the ESP32 firmware -
 *  those live in F7MeadowOS/espcp/espcp_shared_enums.h.
 *
 ****************************************************************************/

#ifndef __INCLUDE_MEADOW_ESPCP__H
#define __INCLUDE_MEADOW_ESPCP__H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/*
 *  Names of the POSIX message queues used by the coprocessor subsystem:
 *
 *  /Esp32Requests  - outbound messages waiting to be sent to the ESP32.
 *  /IncomingEvents - events received from the ESP32 awaiting dispatch.
 *  /Esp32Events    - encoded events carried between the kernel and
 *                    user space.
 */
#define ESPCP_REQUEST_MESSAGE_QUEUE_NAME        "/Esp32Requests"
#define ESPCP_EVENT_HANDLER_MESSAGE_QUEUE_NAME  "/IncomingEvents"
#define ESPCP_EVENT_MESSAGE_QUEUE_NAME          "/Esp32Events"

/*
 *  Maximum number of messages a coprocessor message queue can hold, and the
 *  priority every message is queued at.
 */
#define ESPCP_MAXIMUM_MESSAGE_QUEUE_LENGTH      10
#define ESPCP_DEFAULT_MESSAGE_PRIORITY          1

/*
 *  Number of consecutive message queue read failures after which a worker
 *  thread gives up.  The threads cannot be restarted, so this ends in a reset.
 */
#define ESPCP_MAXIMUM_CONSECUTIVE_QUEUE_FAILURES 20

/*
 *  Size in bytes of an espcp_event_data_t once encoded by
 *  espcp_encode_event_data():  one byte of interface followed by three
 *  4-byte big-endian fields.  This is the encoded (wire) size and is
 *  deliberately not sizeof(espcp_event_data_t), which is 16 because of
 *  structure padding.
 */
#define ESPCP_EVENT_DATA_SIZE                   13

/****************************************************************************
 * Public Types
 ****************************************************************************/

/*
 *  An event passed between the coprocessor driver and user space.  It is
 *  never copied to the queue directly - both sides serialise it with
 *  espcp_encode_event_data() and recover it with espcp_extract_event_data() -
 *  so the field set, rather than the in-memory layout, is what has to match.
 */
struct espcp_event_data_s
{
    uint8_t interface;
    uint32_t function;
    uint32_t status_code;
    uint32_t message_id;
};
typedef struct espcp_event_data_s espcp_event_data_t;

#endif // __INCLUDE_MEADOW_ESPCP__H
