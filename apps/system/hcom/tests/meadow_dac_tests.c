/****************************************************************************
 * apps/system/hcom/tests/meadow_dac_tests.c
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

/* Developer test 15: the Meadow DAC (issue #158). See
 * Documentation/MeadowDac.md.
 *
 * This test stands in for Meadow.Core. It runs in user space and uses the
 * DAC only through the meadow_dac_* kernel calls, the same way Meadow.Core
 * would. Run it with "meadow developer -p 15 -v <n>":
 *
 *   -v 1  API checks. No meter needed; each check logs PASS or FAIL.
 *   -v 2  Level steps on A00, output buffer on. Measure with a meter.
 *   -v 3  Level steps on A00, output buffer off.
 *   -v 4  Level steps on A01, output buffer on.
 *   -v 5  Leave A00 at mid-scale for measuring. -v 6 releases it.
 *   -v 6  Close both channels.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_DAC_TESTS)

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>

#include "../hcom_common.h"
#include <meadow/hcom_shared_common.h>
#include <meadow/meadow_syscall_support.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Pin identifiers, (port << 4) | pin, as Meadow.Core passes them */

#define DAC_TEST_PIN_A00          0x04  /* PA4, DAC channel 1 */
#define DAC_TEST_PIN_A01          0x05  /* PA5, DAC channel 2 */
#define DAC_TEST_PIN_NOT_DAC      0x06  /* PA6, no DAC output */

#define DAC_TEST_MAX_VALUE        4095

/* VREF+ is the 3.3 V rail on all Meadow boards. Expected voltages are
 * worked out for exactly 3.3 V, so they scale with the board's actual rail.
 */

#define DAC_TEST_VREF_MV          3300

/* With the output buffer on, the datasheet only guarantees the output to
 * within 0.2 V of each rail. That is a worst case at its minimum load; a
 * lightly loaded output gets much closer.
 */

#define DAC_TEST_BUFFER_MARGIN_MV 200

/* How long each level is held, so it can be read on a meter */

#define DAC_TEST_HOLD_SECONDS     5

#define DAC_TEST_MID_SCALE        2048

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const uint32_t g_dac_test_steps[] =
{
  0, 1024, 2048, 3072, DAC_TEST_MAX_VALUE
};

#define DAC_TEST_NSTEPS  (sizeof(g_dac_test_steps) / sizeof(g_dac_test_steps[0]))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dac_test_pin_name
 ****************************************************************************/

static const char *dac_test_pin_name(uint8_t pinid)
{
  return pinid == DAC_TEST_PIN_A00 ? "A00" : "A01";
}

/****************************************************************************
 * Name: dac_test_ideal_mv
 *
 * Description:
 *   The voltage, in millivolts, that a value gives with a 3.3 V reference.
 *
 ****************************************************************************/

static uint32_t dac_test_ideal_mv(uint32_t value)
{
  return (value * DAC_TEST_VREF_MV) / (DAC_TEST_MAX_VALUE + 1);
}

/****************************************************************************
 * Name: dac_test_log_expected
 *
 * Description:
 *   Log the voltage a value should give. With the buffer on, values near
 *   either rail are logged as a range, because the datasheet only
 *   guarantees the output to within 0.2 V of the rail.
 *
 ****************************************************************************/

static void dac_test_log_expected(const char *label, uint32_t value,
                                  bool buffered)
{
  uint32_t mv = dac_test_ideal_mv(value);

  if (buffered && mv < DAC_TEST_BUFFER_MARGIN_MV)
    {
      syslog(LOG_MTEST, "    %s: expect %lu-%d mV (ideal %lu mV; with the "
             "buffer on it may not reach 0 V)\n", label, (unsigned long)mv,
             DAC_TEST_BUFFER_MARGIN_MV, (unsigned long)mv);
    }
  else if (buffered && mv > DAC_TEST_VREF_MV - DAC_TEST_BUFFER_MARGIN_MV)
    {
      syslog(LOG_MTEST, "    %s: expect %d-%lu mV (ideal %lu mV; with the "
             "buffer on it may not reach the rail)\n", label,
             DAC_TEST_VREF_MV - DAC_TEST_BUFFER_MARGIN_MV, (unsigned long)mv,
             (unsigned long)mv);
    }
  else
    {
      syslog(LOG_MTEST, "    %s: expect about %lu mV\n", label,
             (unsigned long)mv);
    }
}

/****************************************************************************
 * Name: dac_test_check
 *
 * Description:
 *   Log PASS or FAIL for one check and count failures.
 *
 ****************************************************************************/

static void dac_test_check(const char *what, int ret, int expected,
                           int *failures)
{
  if (ret == expected)
    {
      syslog(LOG_MTEST, "    PASS: %s (returned %d)\n", what, ret);
    }
  else
    {
      syslog(LOG_MTEST, "    FAIL: %s (returned %d, expected %d)\n",
             what, ret, expected);
      (*failures)++;
    }
}

/****************************************************************************
 * Name: dac_test_check_value
 *
 * Description:
 *   Read the output back and check it holds the expected value.
 *
 ****************************************************************************/

static void dac_test_check_value(const char *what, uint8_t pinid,
                                 uint32_t expected, int *failures)
{
  uint32_t value = 0;
  int ret;

  ret = meadow_dac_read(pinid, &value);
  if (ret == OK && value == expected)
    {
      syslog(LOG_MTEST, "    PASS: %s (value %lu)\n", what,
             (unsigned long)value);
    }
  else
    {
      syslog(LOG_MTEST,
             "    FAIL: %s (returned %d, value %lu, expected %lu)\n",
             what, ret, (unsigned long)value, (unsigned long)expected);
      (*failures)++;
    }
}

/****************************************************************************
 * Name: dac_test_api
 *
 * Description:
 *   -v 1: check the error handling and basic behaviour of each call. No
 *   meter is needed.
 *
 ****************************************************************************/

static void dac_test_api(void)
{
  uint32_t value;
  int failures = 0;

  syslog(LOG_MTEST, "DAC test 1: API checks on A00\n");

  /* Start from a known state: A00 closed. It may already be closed. */

  meadow_dac_close(DAC_TEST_PIN_A00);

  dac_test_check("open a pin with no DAC output",
                 meadow_dac_open(DAC_TEST_PIN_NOT_DAC, 0), -EBADSLT,
                 &failures);
  dac_test_check("open with an unknown flag",
                 meadow_dac_open(DAC_TEST_PIN_A00, 1 << 1), -EINVAL,
                 &failures);
  dac_test_check("write before open",
                 meadow_dac_write(DAC_TEST_PIN_A00, 0), -EBADF, &failures);
  dac_test_check("close before open",
                 meadow_dac_close(DAC_TEST_PIN_A00), -EBADF, &failures);

  dac_test_check("read before open",
                 meadow_dac_read(DAC_TEST_PIN_A00, &value), -EBADF,
                 &failures);

  dac_test_check("open", meadow_dac_open(DAC_TEST_PIN_A00, 0), OK,
                 &failures);
  dac_test_check_value("output starts at 0", DAC_TEST_PIN_A00, 0,
                       &failures);
  dac_test_check("read into NULL",
                 meadow_dac_read(DAC_TEST_PIN_A00, NULL), -EINVAL,
                 &failures);
  dac_test_check("write a value above 4095",
                 meadow_dac_write(DAC_TEST_PIN_A00, DAC_TEST_MAX_VALUE + 1),
                 -EINVAL, &failures);
  dac_test_check("write to a pin with no DAC output",
                 meadow_dac_write(DAC_TEST_PIN_NOT_DAC, 0), -EBADSLT,
                 &failures);
  dac_test_check("write 1234", meadow_dac_write(DAC_TEST_PIN_A00, 1234),
                 OK, &failures);
  dac_test_check_value("read back 1234", DAC_TEST_PIN_A00, 1234,
                       &failures);
  dac_test_check("open again while open",
                 meadow_dac_open(DAC_TEST_PIN_A00, 0), OK, &failures);
  dac_test_check_value("second open restarts at 0", DAC_TEST_PIN_A00, 0,
                       &failures);
  dac_test_check("close", meadow_dac_close(DAC_TEST_PIN_A00), OK,
                 &failures);
  dac_test_check("close again",
                 meadow_dac_close(DAC_TEST_PIN_A00), -EBADF, &failures);

  if (failures == 0)
    {
      syslog(LOG_MTEST, "DAC test 1: PASSED\n");
    }
  else
    {
      syslog(LOG_MTEST, "DAC test 1: FAILED, %d check(s) failed\n",
             failures);
    }
}

/****************************************************************************
 * Name: dac_test_steps
 *
 * Description:
 *   -v 2, 3 and 4: open a channel, report the starting value, then step
 *   through a set of levels, holding each so it can be measured. The
 *   channel is closed at the end.
 *
 ****************************************************************************/

static void dac_test_steps(uint8_t pinid, bool buffered)
{
  const char *pin = dac_test_pin_name(pinid);
  char label[16];
  uint32_t value;
  int failures = 0;
  int ret;
  int i;

  syslog(LOG_MTEST, "DAC test: level steps on %s, output buffer %s, "
         "%d s per step\n", pin, buffered ? "on" : "off",
         DAC_TEST_HOLD_SECONDS);
  syslog(LOG_MTEST, "    Expected voltages assume a 3.3 V rail and scale "
         "with the board's actual 3.3 V\n");

  ret = meadow_dac_open(pinid, buffered ? 0 : MEADOW_DAC_FLAG_BUFFER_OFF);
  if (ret < 0)
    {
      syslog(LOG_MTEST, "    FAIL: open %s returned %d\n", pin, ret);
      return;
    }

  ret = meadow_dac_read(pinid, &value);
  syslog(LOG_MTEST, "    After open: value %lu (read returned %d)\n",
         (unsigned long)value, ret);
  dac_test_log_expected("After open", 0, buffered);
  sleep(DAC_TEST_HOLD_SECONDS);

  for (i = 0; i < DAC_TEST_NSTEPS; i++)
    {
      ret = meadow_dac_write(pinid, g_dac_test_steps[i]);
      if (ret < 0)
        {
          syslog(LOG_MTEST, "    FAIL: write %lu returned %d\n",
                 (unsigned long)g_dac_test_steps[i], ret);
          failures++;
          continue;
        }

      snprintf(label, sizeof(label), "Value %4lu",
               (unsigned long)g_dac_test_steps[i]);
      dac_test_log_expected(label, g_dac_test_steps[i], buffered);
      dac_test_check_value("read back", pinid, g_dac_test_steps[i],
                           &failures);
      sleep(DAC_TEST_HOLD_SECONDS);
    }

  dac_test_check("close", meadow_dac_close(pinid), OK, &failures);

  if (failures == 0)
    {
      syslog(LOG_MTEST, "DAC test: steps on %s done, all read-backs "
             "matched; compare the meter readings\n", pin);
    }
  else
    {
      syslog(LOG_MTEST, "DAC test: steps on %s FAILED, %d check(s) "
             "failed\n", pin, failures);
    }
}

/****************************************************************************
 * Name: dac_test_hold
 *
 * Description:
 *   -v 5: leave A00 at mid-scale, buffer on, so it can be measured at
 *   leisure. -v 6 releases it.
 *
 ****************************************************************************/

static void dac_test_hold(void)
{
  int ret;

  ret = meadow_dac_open(DAC_TEST_PIN_A00, 0);
  if (ret == OK)
    {
      ret = meadow_dac_write(DAC_TEST_PIN_A00, DAC_TEST_MID_SCALE);
    }

  if (ret == OK)
    {
      syslog(LOG_MTEST, "DAC test 5: A00 held at %d, expect about %lu mV "
             "(for a 3.3 V rail). Run -v 6 to release it.\n",
             DAC_TEST_MID_SCALE,
             (unsigned long)dac_test_ideal_mv(DAC_TEST_MID_SCALE));
    }
  else
    {
      syslog(LOG_MTEST, "DAC test 5: FAILED, returned %d\n", ret);
    }
}

/****************************************************************************
 * Name: dac_test_close_all
 *
 * Description:
 *   -v 6: close both channels. A channel that is not open returns -EBADF,
 *   which is reported but is not a failure here.
 *
 ****************************************************************************/

static void dac_test_close_all(void)
{
  syslog(LOG_MTEST, "DAC test 6: close A00 returned %d, close A01 "
         "returned %d (%d = was not open)\n",
         meadow_dac_close(DAC_TEST_PIN_A00),
         meadow_dac_close(DAC_TEST_PIN_A01), -EBADF);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_dac_tests
 *
 * Description:
 *   Developer test 15 entry point.
 *
 * Input Parameters:
 *   userdata - The -v value from "meadow developer", selecting the test.
 *
 ****************************************************************************/

void meadow_dac_tests(uint32_t userdata)
{
  switch (userdata)
    {
      case 1:
        dac_test_api();
        break;

      case 2:
        dac_test_steps(DAC_TEST_PIN_A00, true);
        break;

      case 3:
        dac_test_steps(DAC_TEST_PIN_A00, false);
        break;

      case 4:
        dac_test_steps(DAC_TEST_PIN_A01, true);
        break;

      case 5:
        dac_test_hold();
        break;

      case 6:
        dac_test_close_all();
        break;

      default:
        syslog(LOG_MTEST, "DAC test: unknown -v %lu. Use 1 (API checks), "
               "2-4 (level steps), 5 (hold A00), 6 (close all)\n",
               (unsigned long)userdata);
        break;
    }
}

#endif /* CONFIG_DAC_TESTS */
