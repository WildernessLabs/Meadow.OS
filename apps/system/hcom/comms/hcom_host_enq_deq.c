/****************************************************************************
 * \apps\examples\hcom\comms\hcom_host_enq_deq.c
 * 
 *   Copyright (C) 2019 - 2026 Wilderness Labs. All rights reserved.
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

// This file is mostly about saving cobs encoded data, buffering it and
// providing a way to pull the data in packets that can be forwarded and
// routed.

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_common.h"
#include <meadow/hcom_protocol.h>
#include <meadow/meadow_cirbuf.h>
#include "semaphore.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/
static char *thisFile = __FILE__;

static bool _shutting_down;
static sem_t _runProcSem;
static sem_t _runRecvSem;
static host_com_cir_buffer_t *_hcom_cbuf;
static bool _hcom_host_enq_deq_wdog_wakeup;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int hcom_host_enq_deq_wait_for_work(void);

/****************************************************************************
 * Public Functions
 ****************************************************************************/
int hcom_host_enq_deq_setup()
{
  _shutting_down = false;
  _hcom_host_enq_deq_wdog_wakeup = false;

  // Notifies process thread a message has been queued
  sem_init(&_runProcSem, 0, 0);
  sem_setprotocol(&_runProcSem, SEM_PRIO_NONE);

  // Notifies receive thread that it should retry to add message to circular buffer
  sem_init(&_runRecvSem, 0, 0);
  sem_setprotocol(&_runRecvSem, SEM_PRIO_NONE);

  _hcom_cbuf = (host_com_cir_buffer_t *)malloc(sizeof(host_com_cir_buffer_t));
  if (_hcom_cbuf == NULL)
  {
    syslog(LOG_ERR, "%s@%d-_hcom_cbuf allocation failed\n",
              thisFile, __LINE__);
    return -ENOMEM;
  }

  int result = hcom_cirbuf_init(_hcom_cbuf, HCOM_CIRCULAR_BUF_MEM_SIZE,
          HCOM_PROTOCOL_COBS_DELIMITER);
  if (result == HCOM_CIR_BUF_ALLOC_FAILED)
  {
    syslog(LOG_ERR, "%s@%d-Buffer allocation failed\n", thisFile, __LINE__);
    return -1;
  }

  return OK;
}

//====================================================================
//
void hcom_host_enq_deq_shutdown()
{
  _shutting_down = true;

  hcom_cirbuf_release_memory(_hcom_cbuf);
  free(_hcom_cbuf);

  sem_destroy(&_runProcSem);
  sem_destroy(&_runRecvSem);
}

//====================================================================
// Called by watchdog to clean up after download stopped before completion
// Calls hcom_cirbuf_clear_buffer() to clear buffer and returns success
bool hcom_host_enq_deq_clear_buffer()
{
  return (hcom_cirbuf_clear_buffer(_hcom_cbuf) == HCOM_CIR_BUF_INIT_OK);
}

// //====================================================================
// // Needing more information about why download would stop, created this code
// // to show the state of the semaphores when the file download timer expired.
// // Visual Studio is running HcomDiagUi and reports, "The semaphore timeout
// // has expired"
// void hcom_host_enq_deq_dbg_info()
// {
//   int value_runProcSem;
//   int value_runRecvSem;

//   sem_getvalue(&_runProcSem, &value_runProcSem);
//   sem_getvalue(&_runRecvSem, &value_runRecvSem);

//   syslog(LOG_MDIAG, "Error State: _runProcSem:%d, _runRecvSem:%d\n",
//             value_runProcSem, value_runRecvSem);
// }

//=======================================================================
// Add the received data, it's put into the circular buffer. It is added as
// it is received.
// Receive calls here when data is available
int hcom_host_enq_deq_enqueue_rcvd_data(uint8_t recvBuff[], const ssize_t recvByteCnt)
{
  int result;
  
  if (recvByteCnt == 0)
    return OK;

  do
  {
    // Only possible return values: HCOM_CIR_BUF_ADD_SUCCESS,
    // HCOM_CIR_BUF_ADD_WONT_FIT and HCOM_CIR_BUF_ADD_BAD_ARG
    result = hcom_cirbuf_add_bytes(_hcom_cbuf, recvBuff, recvByteCnt);
    switch(result)
    {
      case HCOM_CIR_BUF_ADD_SUCCESS:
        sem_post(&_runProcSem);         // Notify proc of message
        return OK;                      // Return to read more data

      case HCOM_CIR_BUF_ADD_WONT_FIT:
        sem_post(&_runProcSem);         // Notify proc of message

        // Read thread waits here for space in buffer
        sem_wait(&_runRecvSem);         // Wait for a message to be removed
        continue;                       // Try again to add message

      case HCOM_CIR_BUF_ADD_BAD_ARG:
        // Report error and return. The message is lost.
        syslog(LOG_ERR, "%s@%d-Bad argument to cir buf\n", thisFile, __LINE__);
        return OK;

      default:
        syslog(LOG_ERR, "%s@%d-Unknown return from hcom_cirbuf_add_bytes():%d\n",
                    thisFile, __LINE__, result);
        break;
    }
  } while (! _shutting_down);

  return OK;
}

//====================================================================
// Get the next message
// Note: during sem_wait a signal will wake up this thread.
int hcom_host_enq_deq_dequeue_packet(uint8_t *packet_dest_buf,
          size_t *packetLength)
{
  int ret;
  int result;

  do
  {
    // Only HCOM_CIR_BUF_GET_FOUND_MSG, HCOM_CIR_BUF_GET_NONE_FOUND and
    // HCOM_CIR_BUF_GET_DELETED_TOO_BIG can be returned
    result = hcom_cirbuf_get_next_packet(_hcom_cbuf, packet_dest_buf,
              HCOM_PROTOCOL_SAFE_ENCODED_MSG_BUF_SIZE, packetLength);
    switch(result)
    {
      case HCOM_CIR_BUF_GET_FOUND_MSG:
          sem_post(&_runRecvSem);       // Allow recv to retry to add

          // This fast path bypasses hcom_host_enq_deq_wait_for_work(), so it
          // must do that function's bookkeeping itself: drain one accumulated
          // wake-credit from _runProcSem (posted by whichever enqueue produced
          // this message) and clear a stale watchdog flag. We just proved data
          // is flowing, so any pending watchdog notification is moot -- if the
          // download is still genuinely stalled, its timer keeps getting reset
          // per-packet and will fire again on its own.
          sem_trywait(&_runProcSem);
          _hcom_host_enq_deq_wdog_wakeup = false;
        return OK;                      // Return to process message

      case HCOM_CIR_BUF_GET_NONE_FOUND:
        // Thread waits to be notified that a message may be available. This
        // is also where the watchdog notification is detected.
        ret = hcom_host_enq_deq_wait_for_work();
        if(ret < 0)
        {
          return ret;   // May be watchdog timed out
        }
        break;          // Loop again to check for new message

      case HCOM_CIR_BUF_GET_DELETED_TOO_BIG:
        // The message was too big and the bad message removed from the
        // circular buffer. So, report error and return.
        syslog(LOG_ERR, "%s@%d-Message too big, deleted, size:%d\n",
                  thisFile, __LINE__, *packetLength);
        break;                          // Try again to get the next message

      default:
        syslog(LOG_ERR, "%s@%d-Unknown return from hcom_cirbuf_get_next_packet():%d\n",
                  thisFile, __LINE__, result);
        break;
    }
  } while (! _shutting_down);
  
  return OK;
}

//=======================================================================
// This is called from hcom_host_watchdog.c when the download watchdog timer
// has detected that the download has stopped.
// We need to use this indirect scheme because we need the thread doing the
// download to also do the cleanup. Otherwise, there are problems due to the
// way Nuttx threads and tasks handle open files etc.
void hcom_host_enq_deq_dnld_watchdog_wakeup()
{
  _hcom_host_enq_deq_wdog_wakeup = true;

  int ret = sem_post(&_runProcSem);         // Notify proc of message
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error: calling sem_post(&_runProcSem) ret:%d , errno:%d\n",
      __FILE__, __LINE__, ret, errno);
  }
}

//=======================================================================
// Wait to be told that there is more data to be processed in the cir buf
// (or that the watchdog timer has timedout).
int hcom_host_enq_deq_wait_for_work(void)
{
    for (;;)
    {
        if (sem_wait(&_runProcSem) < 0)
        {
            if (errno == EINTR)
                continue;

            syslog(LOG_ERR, "sem_wait failed errno:%d\n", errno);
            return -errno;
        }

        if (_hcom_host_enq_deq_wdog_wakeup)
        {
          _hcom_host_enq_deq_wdog_wakeup = false;
          syslog(LOG_INFO, "%s@%d-HCOM Processing thread calling Watchdog Cleanup\n",
                  __FILE__, __LINE__);

          hcom_host_watchdog_dnld_cleanup_wdog();
          return -ETIME;
        }
        break;
    }

    return OK;
}
