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
 *
 * The write end is opened via file_open()/file_write()/file_close()
 * (a struct file*, not a plain fd) rather than open()/write()/close().
 * This channel has two independent senders that can each be first to
 * open it: the ramlog-reader kernel thread (hcom_nx_trace_msg_proc.c,
 * a kthread, TCB_FLAG_TTYPE_KERNEL) and the traverse_ku kernel tests
 * (meadow_kt_traverse_ku_tests.c, reached via a syscall so it runs on
 * whatever regular task issued it, e.g. hcom_main - TCB_FLAG_TTYPE_TASK).
 * A plain fd is resolved against the CALLING task's file descriptor
 * table (see nxsched_get_fdlist()/this_task()), and kernel threads vs.
 * regular tasks have separate tables - so whichever of these two opens
 * the fd first, the other's write() would resolve it against the wrong
 * table and fail with EBADF. file_write() operates directly on the
 * resolved struct file*, bypassing that per-task lookup, so it works
 * regardless of which context opened it or which is calling now.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_nx_common.h"

#include <meadow/hcom_traverse_ku.h>

#include <sys/stat.h>
#include <nuttx/fs/fs.h>

// Diagnostics
// #define USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>
// #include "stm32_gpio.h"

// #pragma GCC optimize("O0")    /* Prevent optimisation from removing test code */

/****************************************************************************
 * Private Data
 ****************************************************************************/

// Zero-initialized (static): f_inode == NULL means "not open", matching
// the state after a successful file_close() too - see struct file in
// nuttx/include/nuttx/fs/fs.h.
static struct file _send_pipe_filep;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: traverse_ku_open_write_end
 *
 * Description:
 *   Open the write end of the FIFO with O_NONBLOCK so the kernel does not
 *   stall if the user side is not yet open. O_NONBLOCK is deliberately
 *   kept afterward - see the comment below.
 *
 * Returned Value:
 *   0 (OK) on success, -errno on failure.
 *
 ****************************************************************************/

static int traverse_ku_open_write_end(void)
{
  int ret;

  do
  {
    ret = file_open(&_send_pipe_filep, HCOM_TRAVERSE_KU_PIPE_PATH,
                     O_WRONLY | O_NONBLOCK);
    if(ret < 0)
    {
      syslog(LOG_MDIAG, "file_open Error:%d\n", ret);
      if(ret == -ENXIO)
      {
        // Wait for reader to open named pipe and unblock writer
        usleep(10 * 1000);
        continue;
      }
      else
      {
        syslog(LOG_MDIAG, "-->Error: Failed to get file open:%d\n", ret);
        usleep(10 * 1000);
        return ret;
      }
    }
  } while(ret < 0);

  /* Keep O_NONBLOCK on the write end.  NuttX's blocking write can split
   * a message across two circbuf_write() calls when the ring buffer is
   * almost full, placing a partial envelope in the pipe.  The receiver
   * sees nread != sizeof(msg) and loses sync.  With O_NONBLOCK the write
   * either places the entire envelope atomically or returns EAGAIN with
   * nothing written; the EAGAIN handler above retries after a short sleep.
   */
  return OK;
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

  if (_send_pipe_filep.f_inode != NULL)
  {
    file_close(&_send_pipe_filep);
  }

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
    // ret is already a negated errno from hcom_traverse_ku_send()/
    // hcom_traverse_ku_send_string() - errno itself is not set by the
    // file_open()/file_write() calls underneath, and may be stale here.
    return ret;
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
    // ret is already a negated errno from hcom_traverse_ku_send()/
    // hcom_traverse_ku_send_string() - errno itself is not set by the
    // file_open()/file_write() calls underneath, and may be stale here.
    return ret;
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
    // ret is already a negated errno from hcom_traverse_ku_send()/
    // hcom_traverse_ku_send_string() - errno itself is not set by the
    // file_open()/file_write() calls underneath, and may be stale here.
    return ret;
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

  do
  {
    /* Lazy open: attempt to connect to the read end opened by the user side.
    * Returns -ENXIO (no reader) if user init has not run yet — caller drops.
    */
    if (_send_pipe_filep.f_inode == NULL)
    {
      int openRet = traverse_ku_open_write_end();
      if (openRet < 0)
      {
        syslog(LOG_ERR, "%s@%d-_send_pipe_filep: ret:%d\n",
                __FILE__, __LINE__, openRet);
        return openRet;
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
      written = file_write(&_send_pipe_filep,
                      (const uint8_t *)&msg + total_written,
                      sizeof(msg) - total_written);
      if (written > 0)
      {
        total_written += (size_t)written;

        // Test if full message, get more if not
        continue;
      }

      if(written == 0)
      {
        // Nothing written?
        usleep(20 * 1000);
        continue;
      }

      if(written == -EPIPE)         // Broken pipe
      {
        // Will spin here waiting for User side to start, creating the other
        // end of the pipe.
        file_close(&_send_pipe_filep);
        usleep(20 * 1000);
        break;            // Break out of inner loop
      }
      else if(written == -EAGAIN)   // EAGAIN
      {
        usleep(1 * 1000);
      }
      else if(written == -EINTR)    // Interrupted
      {
        /* Signal interrupted the call — retry immediately. */
      }
      else
      {
        syslog(LOG_ERR, "%s@%d: write failed, ret:%zd\n",
                __FILE__, __LINE__, written);
        return (int)written;
      }
    }

    // Only loop if the write end got closed above
  } while(_send_pipe_filep.f_inode == NULL);

  // One message written
  return OK;
}

