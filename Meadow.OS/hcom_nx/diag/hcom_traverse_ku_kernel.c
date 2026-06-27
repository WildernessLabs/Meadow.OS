/****************************************************************************
 * Meadow.OS/hcom_nx/diag/hcom_traverse_ku_kernel.c
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
 * hcom_traverse_ku_kernel_setup() creates the named FIFO during kernel
 * startup.  hcom_traverse_ku_send() opens the write end lazily on the
 * first call, by which time the user side will have already opened the
 * read end (so the open does not block).  Subsequent write() calls are
 * atomic and block naturally on back-pressure.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_nx_common.h"

#include <meadow/hcom_traverse_ku.h>

#include <sys/stat.h>

// Diagnostics
// #define USE_MEADOW_DEBUG_HELPERS
// #undef USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>
// #include "stm32_gpio.h"

// #pragma GCC optimize("O0")    /* Prevent optimisation from removing test code */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int _send_pipe_fd = -1;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: traverse_ku_open_write_end
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

static int traverse_ku_open_write_end(void)
{
  int fd = -1;

  do
  {
    fd = open(HCOM_TRAVERSE_KU_PIPE_PATH, O_WRONLY | O_NONBLOCK);
    if(fd < 0)
    {
      syslog(LOG_MDIAG, "open Error:%d (%s)\n", errno, strerror(errno));
      if(errno == ENXIO)
      {
        // Wait for reader to open named pipe and unblock writer
        usleep(10 * 1000);
        fd = -1;
        continue;
      }
      else
      {
        syslog(LOG_MDIAG, "-->Error: Failed to get file open:%d\n", errno);
        usleep(10 * 1000);
        return -1;
      }
    }
  } while(fd < 0);

  /* Keep O_NONBLOCK on the write end.  NuttX's blocking write can split
   * a message across two circbuf_write() calls when the ring buffer is
   * almost full, placing a partial envelope in the pipe.  The receiver
   * sees nread != sizeof(msg) and loses sync.  With O_NONBLOCK the write
   * either places the entire envelope atomically or returns EAGAIN with
   * nothing written; the EAGAIN handler above retries after a short sleep.
   */
  return fd;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: hcom_traverse_ku_kernel_setup
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
int hcom_traverse_ku_kernel_setup(void)
{
  int ret;

  // Make the fifo (aka named pipe)
  ret = mkfifo(HCOM_TRAVERSE_KU_PIPE_PATH, 0666);
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
int hcom_traverse_ku_syslog_to_host(char *syslogMsg, size_t syslogLen)
{
  int ret;

  // Send the text from syslog messages
  ret = hcom_traverse_ku_send(HCOM_TRAVERSE_KU_TYPE_SYSLOG_TEXT,
    HCOM_HOST_REQUEST_TEXT_TRACE_MSG, syslogMsg, syslogLen);
  if(ret < 0)
  {
    return -errno;
  }
  return OK;
}

//==================================================================
// Send generic text to CLI
int hcom_traverse_ku_text_to_host(uint16_t requestType,
        char *textMsg, size_t textLen)
{
  int ret;
  
  // Send the text to CLI
  ret = hcom_traverse_ku_send(HCOM_TRAVERSE_KU_TYPE_GENERIC_TEXT,
    requestType, textMsg, textLen);
  if(ret < 0)
  {
    return -errno;
  }
  return OK;
}

//==================================================================
// Send generic string (NULL terminated char array) to CLI
int hcom_traverse_ku_string_to_host(uint16_t requestType,
        char *stringMsg)
{
  int ret;
  
  // Send the NULL terminated string to CLI
  ret = hcom_traverse_ku_send_string(HCOM_TRAVERSE_KU_TYPE_GENERIC_STRING,
    requestType, stringMsg);
  if(ret < 0)
  {
    return -errno;
  }
  
  return OK;
}

/****************************************************************************
 * Name: hcom_traverse_ku_send
 *
 * Description:
 *   Marshal msgBody into a hcom_traverse_ku_msg_t envelope and write it
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
 *   msgLen  - payload byte count; must be <= HCOM_TRAVERSE_KU_PAYLOAD_MAX
 *
 * Returned Value:
 *   0 on success, -errno on failure.
 *
 ****************************************************************************/
// Special case for strings
int hcom_traverse_ku_send_string(enum HcomTraverseKUType traverseType,
    uint16_t requestType, const char *strBody)
{
    uint16_t strLen = strlen(strBody);
    return hcom_traverse_ku_send(traverseType,
      requestType,
      (void *)strBody,
      strLen);
}

//-------------------------------------------------------------------
// Primary send function
int hcom_traverse_ku_send(enum HcomTraverseKUType traverseType,
    uint16_t requestType, const void *msgBody, uint16_t msgLen)
{
  struct hcom_traverse_ku_msg_t msg;
  ssize_t written;

  if (msgBody == NULL || msgLen == 0 || msgLen > HCOM_TRAVERSE_KU_PAYLOAD_MAX)
  {
    syslog(LOG_ERR, "%s@%d: send failed, message or length error\n",
            __FILE__, __LINE__);
    return -EINVAL;
  }
  
  if ((int)traverseType < 0 || traverseType >= HCOM_TRAVERSE_KU_TYPE_COUNT)
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
    _send_pipe_fd = traverse_ku_open_write_end();
    if (_send_pipe_fd < 0)
    {
      syslog(LOG_ERR, "%s@%d-_send_pipe_fd: errno%d\n",
              __FILE__, __LINE__, errno);
      return -errno;
    }
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
  size_t total_written = 0;
  while(total_written < sizeof(msg))
  {
    written = write(_send_pipe_fd,
                    (const uint8_t *)&msg + total_written,
                    sizeof(msg) - total_written);
    if (written > 0)
    {
      total_written += (size_t)written;

      // Test if full message, get more if not
      continue;
    }

    if(errno == EPIPE)   // 32 Broken pipe
    {
      // Will always spin here waiting for User side to start
      usleep(20 * 1000);
    }
    else if(errno == EAGAIN)      // 11 EAGAIN
    {
      usleep(1 * 1000);
    }
    else if(errno == EINTR)      // 4 Interrupted
    {
      /* Signal interrupted the call — retry immediately. */
    }
    else
    {
      // syslog(LOG_ERR, "%s@%d: write failed, written:%zd errno:%d\n",
      //         __FILE__, __LINE__, written, errno);
      return -errno;
    }
  }

  // One message written
  return OK;
}

