/****************************************************************************
 * Meadow.OS/hcom_nx/diag/meadow_ku_traverse_kernel.c
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

/* Kernel-side implementation of the kernel-to-user text traverse channel.
 *
 * meadow_ku_traverse_kernel_setup() creates the named FIFO during kernel
 * startup.  meadow_ku_traverse_send() opens the write end lazily on the
 * first call, by which time the user side will have already opened the
 * read end (so the open does not block).  Subsequent write() calls are
 * atomic and block naturally on back-pressure.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_nx_common.h"

#include <meadow/meadow_ku_traverse.h>

#include <sys/stat.h>

#pragma GCC optimize("O0")    /* Prevent optimisation from removing test code */

#define USE_MEADOW_DEBUG_HELPERS
#include "stm32_gpio.h"
// #undef USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int _send_pipe_fd = -1;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ku_traverse_open_write_end
 *
 * Description:
 *   Open the write end of the FIFO with O_NONBLOCK so the kernel does not
 *   stall if the user side is not yet open.  On success, O_NONBLOCK is
 *   cleared immediately so that subsequent write() calls block on
 *   back-pressure as designed.
 *
 * Returned Value:
 *   Open file descriptor on success, -1 with errno set on failure.
 *
 ****************************************************************************/

static int ku_traverse_open_write_end(void)
{
  int fd = -1;
  int flags;

  // fd = open(MEADOW_KU_TRAVERSE_PIPE_PATH, O_WRONLY | O_NONBLOCK);
  // if (fd < 0)
  // {
  //   return -errno;
  // }

  do
  {
    fd = open(MEADOW_KU_TRAVERSE_PIPE_PATH, O_WRONLY | O_NONBLOCK);
    if(fd < 0)
    {
      syslog(LOG_MDIAG, "open Error:%d (%s)\n", errno, strerror(errno));
      if(errno == ENXIO)
      {
        // Wait for reader to open named pipe and unblock writer
        // DEBUG_SET_HIGH(DEBUG_PIN_V2_A3);
        // usleep(500 * 1000);
        // DEBUG_SET_LOW(DEBUG_PIN_V2_A3);
        usleep(10 * 1000);
        fd = -1;
        continue;
      }
      else
      {
        // DEBUG_SET_HIGH(DEBUG_PIN_V2_A4);
        // usleep(500 * 1000);
        // DEBUG_SET_LOW(DEBUG_PIN_V2_A4);
        // usleep(500 * 1000);
        
        syslog(LOG_MDIAG, "-->Error: Failed to get file open: %s\n", strerror(errno));
        return -1;
      }
    }
  } while(fd < 0);

  // Convert to blocking
  flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
  {
    syslog(LOG_MDIAG, "Error: Failed to get file flags: %s\n",
            strerror(errno));
    close(fd);
    return -1;
  }

  /* Switch to blocking writes so the caller stalls on back-pressure. */
  flags &= ~O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags) == -1)
  {
    syslog(LOG_MDIAG, "Error: Failed to set blocking mode: %s\n",
            strerror(errno));
    close(fd);
    return -1;
  }

  // if(fd >= 0)
  //   DEBUG_SET_HIGH(DEBUG_PIN_V2_A4);

  return fd;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_ku_traverse_kernel_setup
 *
 * Description:
 *   Create the named FIFO used for kernel-to-user text transport.  Call
 *   once during kernel init before user-space starts.  Does not open the
 *   write end; that happens lazily on the first send() call after the user
 *   side has opened the read end.
 *
 * Returned Value:
 *   0 on success, -errno on failure.
 *
 ****************************************************************************/
int meadow_ku_traverse_kernel_setup(void)
{
  int ret;

  DEBUG_CONFIGURE_PIN(DEBUG_PIN_V2_A2);
  DEBUG_CONFIGURE_PIN(DEBUG_PIN_V2_A3);
  DEBUG_CONFIGURE_PIN(DEBUG_PIN_V2_A4);
  DEBUG_CONFIGURE_PIN(DEBUG_PIN_V2_A5);

  // DEBUG_SET_HIGH(DEBUG_PIN_V2_A2);
  // DEBUG_SET_HIGH(DEBUG_PIN_V2_A3);
  // DEBUG_SET_HIGH(DEBUG_PIN_V2_A4);
  // DEBUG_SET_HIGH(DEBUG_PIN_V2_A5);

  // sleep(1);
  
  // DEBUG_SET_LOW(DEBUG_PIN_V2_A2);
  // DEBUG_SET_LOW(DEBUG_PIN_V2_A3);
  // DEBUG_SET_LOW(DEBUG_PIN_V2_A4);
  // DEBUG_SET_LOW(DEBUG_PIN_V2_A5);

  // Make the fifo (aka named pipe)
  ret = mkfifo(MEADOW_KU_TRAVERSE_PIPE_PATH, 0666);
  if (ret < 0 && errno != EEXIST)
  {
    syslog(LOG_ERR, "%s@%d: mkfifo failed, errno:%d\n",
            __FILE__, __LINE__, errno);
    return -errno;
  }

  _send_pipe_fd = -1;
  return OK;
}

//==================================================================
// Trace / syslog send to CLI called 
int hcom_ku_traverse_syslog_to_host(char *syslogMsg, size_t syslogLen)
{
  int ret;

  // Send the text from syslog messages
  ret = meadow_ku_traverse_send(MEADOW_KU_TRAVERSE_TYPE_SYSLOG_TEXT,
    HCOM_HOST_REQUEST_TEXT_TRACE_MSG, syslogMsg, syslogLen);
  if(ret < 0)
  {
    return -errno;
  }
  return OK;
}

//==================================================================
// Send generic text to CLI
int hcom_ku_traverse_text_to_host(uint16_t requestType,
        char *textMsg, size_t textLen)
{
  int ret;
  
  // Send the text to CLI
  ret = meadow_ku_traverse_send(MEADOW_KU_TRAVERSE_TYPE_GENERIC_TEXT,
    requestType, textMsg, textLen);
  if(ret < 0)
  {
    return -errno;
  }
  return OK;
}

//==================================================================
// Send generic string (NULL terminated char array) to CLI
int hcom_ku_traverse_string_to_host(uint16_t requestType,
        char *stringMsg)
{
  int ret;
  
  // Send the NULL terminated string to CLI
  ret = meadow_ku_traverse_send_string(MEADOW_KU_TRAVERSE_TYPE_GENERIC_STRING,
    requestType, stringMsg);
  if(ret < 0)
  {
    return -errno;
  }
  
  return OK;
}

/****************************************************************************
 * Name: meadow_ku_traverse_send
 *
 * Description:
 *   Marshal msgBody into a meadow_ku_traverse_msg_t envelope and write it
 *   atomically to the FIFO.  Because sizeof(envelope) < PIPE_BUF (1024
 *   bytes on NuttX) the write is guaranteed to be atomic — no framing or
 *   mutex is needed.
 *
 *   Blocks if the pipe buffer is full (natural back-pressure).  Returns
 *   -ENXIO without logging if the user side has not yet opened the read
 *   end; the caller should treat this as a transient condition.
 *
 * Input Parameters:
 *   traverseType - message type discriminator
 *   msgBody - pointer to payload bytes
 *   msgLen  - payload byte count; must be <= MEADOW_KU_TRAVERSE_PAYLOAD_MAX
 *
 * Returned Value:
 *   0 on success, -errno on failure.
 *
 ****************************************************************************/
// Special case for strings
int meadow_ku_traverse_send_string(enum MeadowKUTraverseType traverseType,
    uint16_t requestType, const char *strBody)
{
    uint16_t strLen = strlen(strBody);
    return meadow_ku_traverse_send(traverseType,
      requestType,
      (void *)strBody,
      strLen);
}

//-------------------------------------------------------------------
// Primary send function
int meadow_ku_traverse_send(enum MeadowKUTraverseType traverseType,
    uint16_t requestType, const void *msgBody, uint16_t msgLen)
{
  struct meadow_ku_traverse_msg_t msg;
  ssize_t written;

  if (msgBody == NULL || msgLen == 0 || msgLen > MEADOW_KU_TRAVERSE_PAYLOAD_MAX)
  {
    syslog(LOG_ERR, "%s@%d: send failed, message or length error\n",
            __FILE__, __LINE__);
    return -EINVAL;
  }
  
  if ((int)traverseType < 0 || traverseType >= MEADOW_KU_TRAVERSE_TYPE_COUNT)
  {
    syslog(LOG_ERR, "%s@%d: send failed, unknown traverse type value:%d\n",
            __FILE__, __LINE__, traverseType);
    return -EINVAL;
  }

  /* Lazy open: attempt to connect to the read end opened by the user side.
   * Returns -ENXIO (no reader) if user init has not run yet — caller drops.
   */
  if (_send_pipe_fd < 0)
  {
    DEBUG_SET_HIGH(DEBUG_PIN_V2_A2);
    _send_pipe_fd = ku_traverse_open_write_end();
    if (_send_pipe_fd < 0)
    {
      syslog(LOG_ERR, "%s@%d-_send_pipe_fd: errno%d\n",
              __FILE__, __LINE__, errno);
      DEBUG_SET_HIGH(DEBUG_PIN_V2_A3);
      return -errno;
    }
    DEBUG_SET_LOW(DEBUG_PIN_V2_A2);
  }

  /* Build the envelope. */
  memset(&msg, 0, sizeof(msg));
  msg.traverseType = traverseType;
  msg.rqstType     = requestType;
  msg.length       = msgLen;
  memcpy(msg.payload, msgBody, msgLen);

  /* A single write() of the full envelope is atomic (sizeof(msg) < PIPE_BUF)
   * so partial writes cannot occur and no framing layer is needed.
   */
  // DEBUG_SET_HIGH(DEBUG_PIN_V2_A4);
  while(1)
  {
    written = write(_send_pipe_fd, &msg, sizeof(msg));
    if (written == (ssize_t)sizeof(msg))
      break;

    if(errno == EAGAIN)      // 11 EAGAIN
    {
      DEBUG_SET_HIGH(DEBUG_PIN_V2_A4);
      usleep(50 * 1000);
      DEBUG_SET_LOW(DEBUG_PIN_V2_A4);
      usleep(1 * 1000);
    }
    else if(errno == EPIPE)      // 32 Broken pipe
    {
      DEBUG_SET_HIGH(DEBUG_PIN_V2_A5);
      usleep(200);
      DEBUG_SET_LOW(DEBUG_PIN_V2_A5);
    }
    else
    {
      // syslog(LOG_ERR, "%s@%d: write failed, written:%zd errno:%d\n",
      //         __FILE__, __LINE__, written, errno);
      return -errno;
    }
  }
  return OK;
}

