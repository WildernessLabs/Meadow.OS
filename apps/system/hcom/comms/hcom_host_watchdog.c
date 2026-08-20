/****************************************************************************
 * \apps\examples\hcom\comms\hcom_host_watchdog.c
 * 
 *   Copyright (C) 2022-2026 Wilderness Labs. All rights reserved.
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

// This file consists of a watchdog timer to insure that file downloads don't
// stall.

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include "../hcom_common.h"
#include <nuttx/wqueue.h>
#include <meadow/hcom_dnld_shared.h>
#include <meadow/meadow_cirbuf.h>
#include <meadow/hcom_protocol.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/
static char *thisFile = __FILE__;
static struct work_s wdog_work_queue;
static bool _shutting_down;
static hcom_dnld_shared_t *_dnldShared;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void hcom_host_watchdog_worker(void *arg);

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// Called once from hcom_host_preprocess.c during startup via the
// hcom_host_process_setup() function.
int hcom_host_watchdog_dnld_starting(hcom_dnld_shared_t *dnldShared)
{
  // Need this to provide file name on failure.
  _dnldShared = dnldShared;
  _shutting_down = false;

  // One time setup
  memset(&wdog_work_queue, 0, sizeof(struct work_s));
  return OK;
}

//=================================================================
void hcom_host_watchdog_stopping()
{
  _shutting_down = true;
}

//=================================================================
// Callback via the USRWORK worker queue
static void hcom_host_watchdog_worker(void *arg)
{
  if (_shutting_down)
    return;

  syslog(LOG_INFO, "%s@%d-Watchdog timed out.\n", __FILE__, __LINE__);

  // This thread can't delete the partially download file directly. We need
  // to use the thread that was downloading to do it.

  // Making this call will wakeup the processing thread that's doing the
  // download and cause it to call hcom_host_watchdog_dnld_cleanup_wdog()
  // (below) to cleanup the partial download state information.
  hcom_host_enq_deq_dnld_watchdog_wakeup();
}

//=================================================================
// Encountered a watchdog timeout. This function gets called from our
// pthread main loop while waiting for the semaphore.
void hcom_host_watchdog_dnld_cleanup_wdog()
{
  int ret;

  // Delete the download wdog timer that got us here
  hcom_host_watchdog_dnld_timer_cancel();

  // This shared timer covers both the STM32F7 and ESP32 download paths, and
  // each keeps its own, unrelated state -- so cleanup has to know which one
  // was actually stalled rather than always assuming STM32F7.
  if (hcom_file_dnld_esp32_is_active())
  {
    syslog(LOG_NOTICE, "%s@%d-Watchdog Timeout cleanup (ESP32)\n", __FILE__, __LINE__);

    hcom_file_dnld_esp32_abort_active_download();

    // Clear the receive data buffer queue
    if(! hcom_host_enq_deq_clear_buffer())
    {
      syslog(LOG_ERR, "%s@%d-ESP32 download failed, clearing buff failed\n",
               thisFile, __LINE__);
    }

    // Tell CLI to restart the download
    char hostMsg[HCOM_SHORT_HOST_STRING_BUFF_LENGTH];
    snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH,
          "ESP32 firmware download failed, resend");
    hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_DNLD_FAIL_RESEND, 0, hostMsg,
          thisFile, __LINE__);
    return;
  }

  // dnldOrigPathName is NULL whenever no download is active. The enqueue/
  // dequeue fast-path fix should prevent this cleanup from firing while idle,
  // but guard the %s formatting below regardless, since NULL there would be
  // a crash risk on a path that's meant to be reporting a problem, not
  // causing a new one.
  const char *dnldFileName = (_dnldShared->dnldOrigPathName != NULL) ?
      _dnldShared->dnldOrigPathName : "(none)";

  syslog(LOG_NOTICE, "%s@%d-Watchdog Timeout cleanup\n", __FILE__, __LINE__);

  // Close the partially downloaded file
  ret = hcom_file_write_close_active_file(_dnldShared);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-close failed for '%s', ret:%d, errno:%d\n",
             thisFile, __LINE__, dnldFileName,
             ret, get_errno());
  }

  // Delete the partially downloaded file
  ret = hcom_file_delete_stm32f7_file_by_name_internal(_dnldShared);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-delete failed for '%s', ret:%d, errno:%d\n",
             thisFile, __LINE__, dnldFileName,
             ret, get_errno());
  }

  // Clear the receive data buffer queue
  if(! hcom_host_enq_deq_clear_buffer())
  {
    syslog(LOG_ERR, "%s@%d-download failed, clearing buff failed '%s'\n",
             thisFile, __LINE__, dnldFileName);
  }

  // Tell CLI to restart the download
  char hostMsg[HCOM_SHORT_HOST_STRING_BUFF_LENGTH];
  snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH,
        "File '%s' download failed, resend", dnldFileName);
  hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_DNLD_FAIL_RESEND, 0, hostMsg,
        thisFile, __LINE__);

  // Setting the download state to none allows future downloads.
  _dnldShared->dnldCurrentState = HcomStm32F7DnldStateNone;
}

//=================================================================
// Start, restart, or stop the timer
// This gets called a lot when downloading, once per packet
int hcom_host_watchdog_dnld_timer_set_delay(time_t delayInSeconds)
{
  int ret;

  // Wrote like this to allow breakpoint
  ret = work_queue(USRWORK, &wdog_work_queue, hcom_host_watchdog_worker,
     NULL, SEC2TICK(delayInSeconds));
  return ret;
}

//=================================================================
// Delete the work queue timer for detecting download failures
// Called from: hcom_host_preprocess.c and hcom_file_dnld_esp32.c as soon
// as the file download is finished.
void hcom_host_watchdog_dnld_timer_cancel()
{
  // syslog(LOG_INFO, "%s@%d-Cancelling the watchdog timer, ptr:%p\n",
  //   __FILE__, __LINE__, wdog_work_queue.worker);

  // If worker is not active return. Otherwise cancel the work
  if (work_available(&wdog_work_queue))
    return;

  // Only possible error is -ENOENT no such work queued, we can ignore
  work_cancel(USRWORK, &wdog_work_queue);
}
