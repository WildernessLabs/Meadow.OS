/****************************************************************************
 * apps/system/hcom/diag/meadow_ku_traverse_user.c
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

/* User-side implementation of the kernel-to-user text traverse channel.
 *
 * meadow_ku_traverse_init_user_setup() opens the named FIFO O_RDONLY | O_NONBLOCK
 * (so the open returns immediately without waiting for the kernel write end),
 * removes O_NONBLOCK so the receive thread blocks on each read(), and spawns
 * the receive thread.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_common.h"

#include <meadow/meadow_ku_traverse.h>
#include <meadow/meadow_thread_config.h>

// Diagnostic 
#include "../diag/hcom_diag_gpio.h"

// void hcom_diag_gpio_config(uint32_t pin);
// void hcom_diag_gpio_set_high(uint32_t pin);
// void hcom_diag_gpio_set_low(uint32_t pin);  
// void hcom_diag_gpio_pulse(uint32_t pin, uint32_t usec);

#pragma GCC optimize("O0")    /* Prevent optimization from removing test code */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int         _recv_pipe_fd       = -1;
static bool        _thread_running = false;
static pthread_t   _recv_pthread;

// static meadow_ku_traverse_handler_t _handlers[MEADOW_KU_TRAVERSE_TYPE_COUNT];

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static FAR void *ku_traverse_recv_pthread(FAR void *arg);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ku_traverse_recv_pthread
 *
 * Description:
 *   Receive thread.  Blocks on each read() waiting for a complete envelope
 *   from the kernel side.  Dispatches to the registered handler for the
 *   message type.  Exits on a read error other than EINTR.
 *
 ****************************************************************************/

static FAR void *ku_traverse_recv_pthread(FAR void *arg)
{
  struct meadow_ku_traverse_msg_t msg;
  ssize_t nread;

#if HCOM_DIAG_OUTPUT_SYSLOG_PID_OF_NEW_THREADS > 0
  syslog(LOG_MDIAG, "New pthread [Thrd:%d][Task:%d] %s\n",
    gettid(), getpid(), HCOM_THREAD_NAME_RECV_TRAVERSE);
#endif

  _thread_running = true;
  
  while (_thread_running)
  {
    // Read end of pipe
    nread = read(_recv_pipe_fd, &msg, sizeof(msg));
    if (nread != (ssize_t)sizeof(msg))
    {
      if (nread < 0)
      {
        if(errno == EINTR)
        {
          usleep(5 * 1000);
          continue;          // Interrupted, try again
        }

        hcom_diag_gpio_set_high(DEBUG_PIN_V2_A1);
        usleep(5 * 1000);
        // hcom_diag_gpio_set_low(DEBUG_PIN_V2_A1);
        // usleep(5 * 1000);
        // Some error??
        continue;
      }

      if (nread == 0)
      {
        /* EOF: no writer is currently attached (kernel lazy-open has
          * not yet happened).  Yield briefly and retry; the thread
          * must not exit here or subsequent kernel writes will block
          * permanently.
          */

        // usleep(1000);
        hcom_diag_gpio_set_high(DEBUG_PIN_V2_A0);
        // usleep(5 * 1000);
        // hcom_diag_gpio_set_low(DEBUG_PIN_V2_A0);
        // usleep(5 * 1000);
        continue;
      }

      syslog(LOG_ERR, "%s@%d: read failed, nread:%zd errno:%d\n",
              __FILE__, __LINE__, nread, errno);
      break;
    }

    // Based on type process the information
    switch(msg.traverseType)
    {
      case MEADOW_KU_TRAVERSE_TYPE_SYSLOG_TEXT:
        hcom_host_send_raw_string_msg(msg.rqstType, 0,
          (char *)&msg.payload, msg.length, __FILE__, __LINE__);
        break;

      case MEADOW_KU_TRAVERSE_TYPE_GENERIC_TEXT:
       hcom_host_send_raw_string_msg(msg.rqstType, 0,
          (char *)&msg.payload, msg.length, __FILE__, __LINE__);
        break;

      case MEADOW_KU_TRAVERSE_TYPE_GENERIC_STRING:
        hcom_host_send_simple_string_msg(msg.rqstType, 0,
          (char *)&msg.payload, __FILE__, __LINE__);
        break;

      default:
        break;
    }
  }

  _thread_running = false;
  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_ku_traverse_init_user_setup
 *
 * Description:
 *   Open the named FIFO for reading and spawn the receive thread.
 *
 *   The open uses O_RDONLY | O_NONBLOCK so it returns immediately without
 *   blocking on the kernel write end.  O_NONBLOCK is removed immediately
 *   after the open so that read() in the receive thread blocks naturally.
 *
 *   Call early in hcom startup.  Safe to call multiple times; subsequent
 *   calls return OK immediately if the thread is already running.
 *
 * Returned Value:
 *   0 on success, -errno on failure.
 *
 ****************************************************************************/

int meadow_ku_traverse_init_user_setup(void)
{
  pthread_attr_t    attr;
  struct sched_param param;
  int flags;
  int ret;

  // Configure and set low
  hcom_diag_gpio_config(DEBUG_PIN_V2_A0);
  hcom_diag_gpio_config(DEBUG_PIN_V2_A1);
  // hcom_diag_gpio_config(DEBUG_PIN_V2_A2);

  if (_thread_running)
  {
    return OK;
  }

  /* Open O_NONBLOCK so we do not stall waiting for the kernel write end. */
  _recv_pipe_fd = open(MEADOW_KU_TRAVERSE_PIPE_PATH, O_RDONLY | O_NONBLOCK);
  if (_recv_pipe_fd < 0)
  {
    syslog(LOG_ERR, "%s@%d: open failed, errno:%d\n",
            __FILE__, __LINE__, errno);
    return -errno;
  }

  /* Switch to blocking reads: the receive thread must block on each read(),
   * not spin-poll.
   */
  flags = fcntl(_recv_pipe_fd, F_GETFL, 0);
  if (flags == -1)
    {
      syslog(LOG_MDIAG, "Error: Failed to get file flags: %s\n",
              strerror(errno));
      close(_recv_pipe_fd);
      return -1;
    }

  flags &= ~O_NONBLOCK;
  if (fcntl(_recv_pipe_fd, F_SETFL, flags) == -1)
    {
      syslog(LOG_MDIAG, "Error: Failed to set blocking mode: %s\n",
              strerror(errno));
      close(_recv_pipe_fd);
      return -1;
    }

  /* Spawn the receive thread at the same priority as other text-transport
   * threads.
   */
  param.sched_priority = HCOM_THREAD_PRIORITY_RECV_TRAVERSE;
  (void)pthread_attr_init(&attr);
  (void)pthread_attr_setschedparam(&attr, &param);
  (void)pthread_attr_setstacksize(&attr, HCOM_THREAD_STACKSIZE_RECV_TRAVERSE);

  ret = pthread_create(&_recv_pthread, &attr, ku_traverse_recv_pthread, NULL);
  if (ret != 0)
    {
      syslog(LOG_CRIT, "%s@%d: pthread_create failed, ret:%d errno:%d\n",
             __FILE__, __LINE__, ret, errno);
      close(_recv_pipe_fd);
      _recv_pipe_fd = -1;
      return -ret;
    }

  return OK;
}
