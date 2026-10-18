/****************************************************************************
 * apps/system/hcom/diag/hcom_traverse_ku_user.c
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
 * hcom_traverse_ku_init_user_setup() opens the named FIFO O_RDONLY | O_NONBLOCK
 * (so the open returns immediately without waiting for the kernel write end),
 * removes O_NONBLOCK so the receive thread blocks on each read(), and spawns
 * the receive thread.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_common.h"

#include <meadow/hcom_traverse_ku.h>
#include <meadow/meadow_thread_config.h>

// Diagnostic 
// #include "../diag/hcom_diag_gpio.h"
// #pragma GCC optimize("O0")    /* Prevent optimization from removing test code */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int         _recv_pipe_fd       = -1;
static bool        _thread_running = false;
static pthread_t   _recv_pthread;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static FAR void *traverse_ku_recv_pthread(FAR void *arg);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: traverse_ku_recv_pthread
 *
 * Description:
 *   Receive thread.  Blocks on each read() waiting for a complete envelope
 *   from the kernel side.  Dispatches to the registered handler for the
 *   message type.  Exits on a read error other than EINTR.
 *
 ****************************************************************************/

static FAR void *traverse_ku_recv_pthread(FAR void *arg)
{
  struct hcom_traverse_ku_msg_t msg;
  uint8_t *dst = (uint8_t *)&msg;
  size_t   total = 0;
  ssize_t  nread;

#if HCOM_DIAG_OUTPUT_SYSLOG_PID_OF_NEW_THREADS > 0
  syslog(LOG_MDIAG, "New pthread [Thrd:%d][Task:%d] %s\n",
    gettid(), getpid(), HCOM_THREAD_NAME_RECV_TRAVERSE);
#endif

  // _thread_running is set by hcom_traverse_ku_init_user_setup() itself,
  // before it calls pthread_create() -- not here -- so it's already true
  // by the time this thread runs its very first statement, and a second
  // caller of the setup function can't race in behind it either.
  while (_thread_running)
  {
    /* Accumulate bytes into msg until a complete envelope is assembled.
     * The kernel writes fixed-size envelopes; NuttX may return fewer bytes
     * than requested (partial read) when the pipe buffer is nearly full, so
     * we must reassemble rather than require a single sizeof(msg) read.
     */
    nread = read(_recv_pipe_fd, dst + total, sizeof(msg) - total);
    if (nread > 0)
    {
      total += (size_t)nread;
      if (total < sizeof(msg))
      {
        continue;  /* Need more bytes to complete the envelope */
      }

      /* Complete envelope received -- reset accumulator and dispatch. */
      total = 0;
    }
    else if (nread < 0)
    {
      if(errno == EINTR)
      {
        continue;          // Interrupted, try again
      }

      // Not expected to be reachable in practice (the only realistic
      // read() failures on this fd are EINTR, handled above, and
      // ECANCELED, which would require pthread_cancel() -- never called
      // on this thread) -- logged so a persistent fault here is visible
      // rather than silently retrying forever.
      syslog(LOG_ERR, "%s@%d: read failed, errno:%d\n",
              __FILE__, __LINE__, errno);
      usleep(5 * 1000);
      continue;
    }
    else  /* nread == 0 */
    {
      /* EOF: no writer is currently attached. This can be the initial
       * kernel lazy-open not having happened yet, or the kernel-side
       * writer closing and later reopening its end (it does this, e.g.
       * on an EPIPE-recovery reconnect). Either way, any bytes already
       * accumulated belong to a writer session that's now gone -- a
       * reconnecting writer always starts a fresh envelope from its own
       * byte 0, so resuming into a stale mid-struct offset would splice
       * the old partial envelope with the new one. Discard it and start
       * over. Yield briefly and retry; the thread must not exit here or
       * subsequent kernel writes will block permanently.
       */
      total = 0;
      usleep(5 * 1000);
      continue;
    }

    // Based on type process the information
    switch(msg.traverseType)
    {
      case HCOM_TRAVERSE_KU_TYPE_SYSLOG_TEXT:
        hcom_host_send_raw_string_msg(msg.rqstType, 0,
          (char *)&msg.payload, msg.length, __FILE__, __LINE__);
        break;

      case HCOM_TRAVERSE_KU_TYPE_GENERIC_TEXT:
       hcom_host_send_raw_string_msg(msg.rqstType, 0,
          (char *)&msg.payload, msg.length, __FILE__, __LINE__);
        break;

      case HCOM_TRAVERSE_KU_TYPE_GENERIC_STRING:
        // Call finds length if terminated or not
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
 * Name: hcom_traverse_ku_init_user_setup
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
 *   0 on success, negative failure.
 *
 ****************************************************************************/

int hcom_traverse_ku_init_user_setup(void)
{
  pthread_attr_t    attr;
  struct sched_param param;
  int flags;
  int ret;

  if (_thread_running)
  {
    return OK;
  }

  // Set synchronously here, before spawning the thread, rather than letting
  // the spawned thread set it on its own first line. Two things this must
  // get right:
  //  1. A second caller reaching the guard above must see this as true
  //     immediately -- not after some window where the thread is still
  //     starting up -- or the "safe to call multiple times" guarantee this
  //     function documents doesn't actually hold.
  //  2. The new thread's own `while (_thread_running)` loop must already
  //     see true the moment it first runs -- if that thread happens to get
  //     scheduled before a subsequent statement here could set the flag,
  //     the loop would see false and the thread would exit immediately
  //     without ever doing anything. Setting it before pthread_create()
  //     avoids that race entirely.
  // Every early-return failure path below resets this back to false so a
  // later retry can still attempt initialization from scratch.
  _thread_running = true;

  /* Open O_NONBLOCK so we do not stall waiting for the kernel write end. */
  _recv_pipe_fd = open(HCOM_TRAVERSE_KU_PIPE_PATH, O_RDONLY | O_NONBLOCK);
  if (_recv_pipe_fd < 0)
  {
    syslog(LOG_ERR, "%s@%d: open failed, errno:%d\n",
            __FILE__, __LINE__, errno);
    _thread_running = false;
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
    _thread_running = false;
    return -EINVAL;
  }

  flags &= ~O_NONBLOCK;
  if (fcntl(_recv_pipe_fd, F_SETFL, flags) == -1)
  {
    syslog(LOG_MDIAG, "Error: Failed to set blocking mode: %s\n",
            strerror(errno));
    close(_recv_pipe_fd);
    _thread_running = false;
    return -EINVAL;
  }

  /* Spawn the receive thread at the same priority as other text-transport
   * threads.
   */
  param.sched_priority = HCOM_THREAD_PRIORITY_RECV_TRAVERSE;
  (void)pthread_attr_init(&attr);
  (void)pthread_attr_setschedparam(&attr, &param);
  (void)pthread_attr_setstacksize(&attr, HCOM_THREAD_STACKSIZE_RECV_TRAVERSE);

  ret = pthread_create(&_recv_pthread, &attr, traverse_ku_recv_pthread, NULL);
  if (ret != 0)
  {
    syslog(LOG_CRIT, "%s@%d: pthread_create failed, ret:%d errno:%d\n",
            __FILE__, __LINE__, ret, errno);
    close(_recv_pipe_fd);
    _recv_pipe_fd = -1;
    _thread_running = false;
    return -ret;
  }
  (void)pthread_setname_np(_recv_pthread, HCOM_THREAD_NAME_RECV_TRAVERSE);

  return OK;
}
