/****************************************************************************
 * Meadow.OS/kerneltests/quick_misc_tests.c
 * 
 *   Copyright (C) 2023-2026 Wilderness Labs. All rights reserved.
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
 * Included Files
 ****************************************************************************/
#include <nuttx/config.h>

#include "../hcom_nx/hcom_nx_common.h"
#include <meadow/hcom_shared_common.h>
#include <nuttx/wqueue.h>

// Only build if configured
#if defined(CONFIG_QUICK_MISC_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)

#pragma message "(--) quick_misc_tests.c"

// Diagnostic always as this is test code
// #define USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>

// #pragma GCC optimize("O0")    // Prevent code optimization

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/

#define MEADOW_QUICK_MISC_TESTS_MSG_RATE (8)

/************************************************************************************
 * Private Data
 ************************************************************************************/
static struct work_s wdog_work_queue;

/************************************************************************************
 * Public Data
 ************************************************************************************/

/************************************************************************************
 * Private Function Prototypes
 ************************************************************************************/
static void quick_misc_tests_meadow_listen(uint32_t userData);
static void quick_misc_tests_listen_worker(void *arg);
static void quick_misc_tests_listen_cancel(void);

/************************************************************************************
 * Public Functions
 ************************************************************************************/
// set developer -p 10 come here
void meadow_kt_quick_misc_tests(uint32_t userData)
{
  syslog(LOG_MTEST, "Quick and Misc tests received 'set developer -p 10 -v %lu'.\n",
    userData);

  switch(userData)
  {
    // This is defined in hcom_shared_common.h
#if(MEADOW_INCLUDE_CODE_FOR_TESTING_5MB_OF_FLASH > 0)
    case 1:
      // Fill the 5MB reserved space with a pattern
      hcom_nx_flash_testing_fill_5mb_of_flash();
      break;

    case 2:
      // Verify flash layout Test
      hcom_nx_flash_testing_verify_regions();
      break;
#endif

    // To test 'meadow listen' Send a message waits and repeats.
    case 3:
      syslog(LOG_MTEST, "Starting periodic syslog for 'meadow listen' test");
      quick_misc_tests_meadow_listen(userData);
      break;

    case 4:
      syslog(LOG_MTEST, "Stopping periodic syslog for 'meadow listen' test");
      quick_misc_tests_listen_cancel();
      break;

    default:
      syslog(LOG_MTEST,
        "Undefined test for meadow_kt_quick_misc_tests, userData:%lu. NO TEST DEFINED\n",
        userData);
      break;
  }
}

/************************************************************************************
 * Private Functions
 ************************************************************************************/
// One time setup
void quick_misc_tests_meadow_listen(uint32_t userData)
{
  syslog(LOG_MTEST, "Starting syslog output every %d seconds\n",
    MEADOW_QUICK_MISC_TESTS_MSG_RATE);

  memset(&wdog_work_queue, 0, sizeof(struct work_s));

  // Queue worker to send message again
  work_queue(LPWORK, &wdog_work_queue, quick_misc_tests_listen_worker,
    NULL, SEC2TICK(MEADOW_QUICK_MISC_TESTS_MSG_RATE));
} 

//=================================================================
// Callback via the LPWORK worker queue
static void quick_misc_tests_listen_worker(void *arg)
{
  static int count = 1;

  // Output this message a periodically
  syslog(LOG_MTEST, "meadow listen test message-%d\n", count);
  count ++;
  
  // Queue worker to send message again
  work_queue(LPWORK, &wdog_work_queue, quick_misc_tests_listen_worker,
    NULL, SEC2TICK(MEADOW_QUICK_MISC_TESTS_MSG_RATE));
}

//=================================================================
// Delete the work queue timer for detecting download failures
// Called from: hcom_host_preprocess.c and hcom_file_dnld_esp32.c as soon
// as the file download is finished.
void quick_misc_tests_listen_cancel(void)
{
  syslog(LOG_MTEST, "%s@%d-Cancelling the meadow listen test\n",
    __FILE__, __LINE__);

  // If worker is not active return. Otherwise cancel the work
  if (work_available(&wdog_work_queue))
    return;

  // Only possible error is -ENOENT no such work queued, we can ignore
  work_cancel(LPWORK, &wdog_work_queue);
}

#endif  // #if defined(CONFIG_QUICK_MISC_TESTS)
