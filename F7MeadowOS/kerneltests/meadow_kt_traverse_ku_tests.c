/****************************************************************************
 * Meadow.OS/kerneltests/hcom_kt_traverse_ku_tests.c
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

/* On-target tests for the hcom_traverse_ku channel.
 *
 * The kernel-side send() is exercised directly; pass/fail is confirmed by
 * observing the messages at the host CLI (forwarded by the user-side handler).
 *
 * Test cases (select with --unittests=traverse_ku and pass -v <N>):
 *
 *   value == 1  Round-trip smoke: send one TRACE_TEXT message and verify
 *               hcom_traverse_ku_send() returns OK.
 *
 *   value == 2  Ordering: send 8 TRACE_TEXT messages with sequential
 *               counters.  Messages must appear at the host in order.
 *
 *   value == 3  Multi-type: send one TRACE_TEXT and one RAMLOG_TEXT message.
 *               Both must appear at the host.
 *
 *   value == 4  Run all three cases above in sequence.
 *
 *   value == 8  Cross-context regression test: a throwaway kthread opens
 *               the write end and sends first (TCB_FLAG_TTYPE_KERNEL,
 *               shares g_kthread_group), then this test itself sends
 *               again from its own syscall-driven task context
 *               (TCB_FLAG_TTYPE_TASK, its own private fd table) reusing
 *               the same already-open handle. Before the file_open()/
 *               file_write() fix in hcom_traverse_ku_kernel.c, the second
 *               send would fail with EBADF because a plain fd/write() is
 *               resolved against the CALLING task's fd table, not the
 *               table of whichever context originally opened it.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include "../hcom_nx/hcom_nx_common.h"

/* Only build if configured */

#if defined(CONFIG_TRAVERSE_KU_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
#include <meadow/hcom_traverse_ku.h>

#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <nuttx/semaphore.h>
#include <time.h>
#include <nuttx/kthread.h>

// #pragma GCC optimize("O0")    /* Prevent optimisation from removing test code */

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define KUT_TAG     "KUT"   /* prefix for all test messages visible at host */
#define KUT_NORDER  8       /* number of messages in the ordering test       */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: kt_traverse_ku_test_round_trip
 *
 * Description:
 *   Send one TRACE_TEXT message and verify the return value is OK.
 *   Pass/fail is also visible at the host when the message appears in the
 *   CLI output.
 *
 ****************************************************************************/

static void kt_traverse_ku_test_round_trip(void)
{
  static const char msg[] = KUT_TAG ":round-trip\n";
  int ret;

  syslog(LOG_INFO, "KU Traverse test 1: round-trip\n");

  ret = hcom_traverse_ku_send(HCOM_TRAVERSE_KU_TYPE_SYSLOG_TEXT,
    HCOM_HOST_REQUEST_TEXT_INFORMATION, msg, (uint16_t)strlen(msg));
  if (ret == OK)
  {
    syslog(LOG_INFO, "KU Traverse test 1: PASS (send returned OK)\n");
  }
  else
  {
    syslog(LOG_ERR, "KU Traverse test 1: FAIL (send returned %d)\n", ret);
  }
}

/****************************************************************************
 * Name: kt_traverse_ku_test_ordering
 *
 * Description:
 *   Send KUT_NORDER messages with embedded sequence numbers.  The messages
 *   must arrive at the host in the same order they were sent.
 *
 ****************************************************************************/

static void kt_traverse_ku_test_ordering(void)
{
  char msg[48];
  int  len;
  int  i;
  int  failures = 0;

  syslog(LOG_INFO, "KU Traverse test 2: ordering (%d messages)\n",
         KUT_NORDER);

  for (i = 0; i < KUT_NORDER; i++)
  {
    len = snprintf(msg, sizeof(msg), KUT_TAG ":order[%d/%d]\n",
      i, KUT_NORDER - 1);

    if (hcom_traverse_ku_send(HCOM_TRAVERSE_KU_TYPE_SYSLOG_TEXT,
        HCOM_HOST_REQUEST_TEXT_INFORMATION, msg, (uint16_t)len) != OK)
      {
        failures++;
        syslog(LOG_ERR, "KU Traverse test 2: send[%d] FAIL\n", i);
      }
  }

  if (failures == 0)
  {
    syslog(LOG_INFO,
            "KU Traverse test 2: PASS (all %d sends OK; "
            "verify order at host)\n", KUT_NORDER);
  }
  else
  {
    syslog(LOG_ERR, "KU Traverse test 2: FAIL (%d sends failed)\n",
            failures);
  }
}

/****************************************************************************
 * Name: kt_traverse_ku_test_multi_type
 *
 * Description:
 *   Send one TRACE_TEXT and one RAMLOG_TEXT message.  Both must appear at
 *   the host, confirming that both type handlers are registered.
 *
 ****************************************************************************/

static void kt_traverse_ku_test_multi_type(void)
{
  static const char trace_msg[]  = KUT_TAG ":multi-type/TRACE_TEXT\n";
  static const char ramlog_msg[] = KUT_TAG ":multi-type/RAMLOG_TEXT\n";
  int failures = 0;

  syslog(LOG_INFO, "KU Traverse test 3: multi-type\n");

  if (hcom_traverse_ku_send(HCOM_TRAVERSE_KU_TYPE_SYSLOG_TEXT,
    HCOM_HOST_REQUEST_TEXT_INFORMATION,
    trace_msg,
    (uint16_t)strlen(trace_msg)) != OK)
  {
    failures++;
    syslog(LOG_ERR, "KU Traverse test 3: TRACE_TEXT send FAIL\n");
  }

  if (hcom_traverse_ku_send(HCOM_TRAVERSE_KU_TYPE_GENERIC_TEXT,
      HCOM_HOST_REQUEST_TEXT_INFORMATION,
      ramlog_msg,
      (uint16_t)strlen(ramlog_msg)) != OK)
  {
    failures++;
    syslog(LOG_ERR, "KU Traverse test 3: RAMLOG_TEXT send FAIL\n");
  }

  if (failures == 0)
  {
    syslog(LOG_INFO,
            "KU Traverse test 3: PASS (both sends OK; "
            "verify both appear at host)\n");
  }
}

/****************************************************************************
 * Name: kt_traverse_ku_test_opener_kthread
 *
 * Description:
 *   Entry point for the throwaway kthread used by
 *   kt_traverse_ku_test_cross_context(). Sends exactly one message (which,
 *   if the write end is not already open, opens it from THIS kthread's
 *   task group - g_kthread_group) and posts _kt_traverse_ku_opener_done
 *   when finished, whether the send succeeded or not.
 *
 ****************************************************************************/

static sem_t _kt_traverse_ku_opener_done;

static void *kt_traverse_ku_test_opener_kthread(int argc, char *argv[])
{
  static const char msg[] = KUT_TAG ":cross-context/opened-by-kthread\n";
  int ret;

  ret = hcom_traverse_ku_send(HCOM_TRAVERSE_KU_TYPE_SYSLOG_TEXT,
    HCOM_HOST_REQUEST_TEXT_INFORMATION, msg, (uint16_t)strlen(msg));
  if (ret != OK)
  {
    syslog(LOG_ERR,
      "KU Traverse test 8: opener kthread send FAIL (ret %d)\n", ret);
  }

  nxsem_post(&_kt_traverse_ku_opener_done);
  return NULL;
}

/****************************************************************************
 * Name: kt_traverse_ku_test_cross_context
 *
 * Description:
 *   Force the write end closed, then have a throwaway kthread open it and
 *   send first (TCB_FLAG_TTYPE_KERNEL, shares g_kthread_group with every
 *   other kthread). Once that completes, send again from THIS function's
 *   own context - this test is reached via the meadow_kt_dispatcher
 *   syscall, so it runs on whatever regular task issued the developer
 *   command (TCB_FLAG_TTYPE_TASK, its own private file descriptor table).
 *   The second send is the one that matters: it reuses the handle the
 *   kthread opened, from a different task group than the one that opened
 *   it - exactly the condition that used to fail with EBADF.
 *
 ****************************************************************************/

static void kt_traverse_ku_test_cross_context(void)
{
  static const char msg[] = KUT_TAG ":cross-context/sent-by-this-task\n";
  int ret;
  int pid;

  syslog(LOG_INFO,
    "KU Traverse test 8: cross-context (kthread opens+sends first, "
    "this task sends second)\n");

  // Force a clean slate so the kthread below is guaranteed to be the one
  // that (re)opens the write end, regardless of what earlier tests in this
  // boot session may have left open.
  ret = hcom_traverse_ku_kernel_setup();
  if (ret < 0)
  {
    syslog(LOG_ERR,
      "KU Traverse test 8: FAIL (kernel_setup returned %d)\n", ret);
    return;
  }

  nxsem_init(&_kt_traverse_ku_opener_done, 0, 0);

  pid = kthread_create("KUTraverseOpener", 100, 2048,
    (main_t) kt_traverse_ku_test_opener_kthread, (char * const *)NULL);
  if (pid < 0)
  {
    syslog(LOG_ERR,
      "KU Traverse test 8: FAIL (kthread_create returned %d)\n", pid);
    nxsem_destroy(&_kt_traverse_ku_opener_done);
    return;
  }

  // Reader must already be connected for either send to complete at all
  // (same precondition as every other test in this file) - this timeout
  // is just to keep the test from hanging forever if that's not true.
  ret = nxsem_tickwait_uninterruptible(&_kt_traverse_ku_opener_done,
                                       SEC2TICK(5));
  nxsem_destroy(&_kt_traverse_ku_opener_done);
  if (ret < 0)
  {
    syslog(LOG_ERR,
      "KU Traverse test 8: FAIL (opener kthread did not finish in time - "
      "is a host reader connected?)\n");
    return;
  }

  // The write end is now open, opened by the kthread's task group above.
  // Send again from this (different) task group.
  ret = hcom_traverse_ku_send(HCOM_TRAVERSE_KU_TYPE_SYSLOG_TEXT,
    HCOM_HOST_REQUEST_TEXT_INFORMATION, msg, (uint16_t)strlen(msg));
  if (ret == OK)
  {
    syslog(LOG_INFO,
      "KU Traverse test 8: PASS (cross-context send returned OK)\n");
  }
  else
  {
    syslog(LOG_ERR,
      "KU Traverse test 8: FAIL (cross-context send returned %d)\n", ret);
  }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: hcom_kt_traverse_ku_tests
 *
 * Description:
 *   Entry point for the KU traverse on-target test suite.
 *
 * Input Parameters:
 *   value - see the test case list in the file header comment.
 *
 ****************************************************************************/

void hcom_kt_traverse_ku_tests(uint32_t userData)
{
  int ret = OK;

  syslog(LOG_INFO,
         "KU Traverse tests starting (value=%" PRIu32 ")\n", userData);
  
  switch (userData)
    {
      case 1:
        kt_traverse_ku_test_round_trip();
        break;

      case 2:
        kt_traverse_ku_test_ordering();
        break;

      case 3:
        kt_traverse_ku_test_multi_type();
        break;

      case 4:
        kt_traverse_ku_test_round_trip();
        kt_traverse_ku_test_ordering();
        kt_traverse_ku_test_multi_type();
        break;

      // Send a syslog message to 'meadow listen' via syslog's buffer and
      // directly to CLI.
      case 5:
        syslog(LOG_MTEST, "Via syslog() call from Unit Test #5a\n");
        ret =  hcom_traverse_ku_syslog_to_host("Direct, syslog from Unit Test #5b",
        strlen("Direct, syslog from Unit Test #5b"));
        break;

      // Send a text message to 'meadow listen'
      case 6:
        ret =  hcom_traverse_ku_text_to_host(HCOM_HOST_REQUEST_TEXT_INFORMATION,
          "This is a text message from Unit Test #6",
          strlen("This is a text message from Unit Test #6"));
        break;

      // Send a string message to 'meadow listen'
      case 7:
        ret =  hcom_traverse_ku_string_to_host(HCOM_HOST_REQUEST_TEXT_INFORMATION,
          "This is a string from Unit Test #7");
        break;

      // Cross-context regression test - see the file header comment.
      case 8:
        kt_traverse_ku_test_cross_context();
        break;

      default:
        syslog(LOG_MTEST, "Undefined test for hcom_kt_traverse_ku_tests, userData:%lu\n",
            userData);
        break;
    }

    if(ret < 0)
    {
      // ret is already a negated errno from the hcom_traverse_ku_*
      // wrappers - errno itself is not set underneath and may be stale.
      syslog(LOG_MDIAG, "KU Traverse tests, error:ret:%d\n", ret);
    }

  syslog(LOG_INFO, "KU Traverse tests complete\n");
}

#endif /* defined(CONFIG_TRAVERSE_KU_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS) */
