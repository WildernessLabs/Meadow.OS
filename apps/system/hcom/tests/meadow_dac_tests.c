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
 *   -v 6  Close both channels, stopping any waveform.
 *
 * Waveforms (issue #658). Each starts a waveform and returns, leaving it
 * playing until -v 6. Watch A00 (and A01) on a scope.
 *
 *   -v 7  Waveform API checks. No scope needed.
 *   -v 8  1 kHz sine on A00 (60 samples at 60,000 Hz).
 *   -v 9  100 Hz sawtooth on A00 (250 samples at 25,000 Hz).
 *   -v 10 Triangle on A00, amplitude 4095 at 1,000,000 Hz (about 122 Hz).
 *   -v 11 Noise on A00, amplitude 4095 at 100,000 Hz.
 *   -v 12 1 kHz sine on A00 and cosine on A01: a circle in XY mode.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_DAC_TESTS)

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
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

/* Waveform tests. The rates divide TIM6's 108 MHz exactly. */

#define DAC_TEST_SINE_SAMPLES     60
#define DAC_TEST_SINE_RATE        60000     /* 1 kHz sine */
#define DAC_TEST_SAW_SAMPLES      250
#define DAC_TEST_SAW_RATE         25000     /* 100 Hz sawtooth */
#define DAC_TEST_TRI_RATE         1000000   /* 1e6 / (2 * 4095) = 122 Hz */
#define DAC_TEST_NOISE_RATE       100000
#define DAC_TEST_WAVE_MAX_SAMPLES 8192

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const uint32_t g_dac_test_steps[] =
{
  0, 1024, 2048, 3072, DAC_TEST_MAX_VALUE
};

#define DAC_TEST_NSTEPS  (sizeof(g_dac_test_steps) / sizeof(g_dac_test_steps[0]))

/* One cycle of a full-scale sine wave, 60 samples */

static const uint16_t g_dac_test_sine[DAC_TEST_SINE_SAMPLES] =
{
  2048, 2262, 2473, 2680, 2880, 3071, 3251, 3418, 3569, 3704,
  3821, 3918, 3995, 4050, 4084, 4095, 4084, 4050, 3995, 3918,
  3821, 3704, 3569, 3418, 3251, 3071, 2880, 2680, 2473, 2262,
  2048, 1833, 1622, 1415, 1215, 1024,  844,  677,  526,  391,
  274,  177,  100,   45,   11,    0,   11,   45,  100,  177,
  274,  391,  526,  677,  844, 1024, 1215, 1415, 1622, 1833
};

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
 * Name: dac_test_wave_init
 *
 * Description:
 *   Fill in a waveform description the way Meadow.Core would.
 *
 ****************************************************************************/

static void dac_test_wave_init(struct meadow_dac_wave_s *wave,
                               uint32_t type, uint32_t rate_hz)
{
  memset(wave, 0, sizeof(*wave));
  wave->size    = sizeof(*wave);
  wave->type    = type;
  wave->rate_hz = rate_hz;
}

/****************************************************************************
 * Name: dac_test_start_buffer
 *
 * Description:
 *   Start a buffer loop and return meadow_dac_wave_start()'s result. The
 *   actual rate is stored in *actual_hz if it is not NULL.
 *
 ****************************************************************************/

static int dac_test_start_buffer(uint8_t pinid, const uint16_t *samples,
                                 uint32_t count, uint32_t rate_hz,
                                 uint32_t *actual_hz)
{
  struct meadow_dac_wave_s wave;
  int ret;

  dac_test_wave_init(&wave, MEADOW_DAC_WAVE_BUFFER, rate_hz);
  wave.count   = count;
  wave.samples = samples;

  ret = meadow_dac_wave_start(pinid, &wave);
  if (actual_hz != NULL)
    {
      *actual_hz = wave.actual_hz;
    }

  return ret;
}

/****************************************************************************
 * Name: dac_test_output_changes
 *
 * Description:
 *   Read the output repeatedly and return true if it took more than one
 *   value: a sign that a waveform is playing. The reads are back to back
 *   rather than spaced with usleep(), which rounds to the 1 ms tick: that is
 *   exactly one cycle of the 1 kHz test sine, and the tick and TIM6 come
 *   from the same clock, so every read could see the same point.
 *
 ****************************************************************************/

static bool dac_test_output_changes(uint8_t pinid)
{
  uint32_t first = 0;
  uint32_t value = 0;
  int i;

  if (meadow_dac_read(pinid, &first) < 0)
    {
      return false;
    }

  for (i = 0; i < 1000; i++)
    {
      if (meadow_dac_read(pinid, &value) == OK && value != first)
        {
          return true;
        }
    }

  return false;
}

/****************************************************************************
 * Name: dac_test_wave_api
 *
 * Description:
 *   -v 7: check the waveform calls' error handling and basic behaviour. No
 *   scope is needed.
 *
 ****************************************************************************/

static void dac_test_wave_api(void)
{
  static uint16_t bad[4] =
  {
    0, 1000, DAC_TEST_MAX_VALUE + 1, 0
  };

  struct meadow_dac_wave_s wave;
  uint32_t actual = 0;
  int failures = 0;
  bool changes;

  syslog(LOG_MTEST, "DAC test 7: waveform API checks on A00 and A01\n");

  /* Start from a known state: both channels closed */

  meadow_dac_close(DAC_TEST_PIN_A00);
  meadow_dac_close(DAC_TEST_PIN_A01);

  dac_test_check("start before open",
                 dac_test_start_buffer(DAC_TEST_PIN_A00, g_dac_test_sine,
                                       DAC_TEST_SINE_SAMPLES,
                                       DAC_TEST_SINE_RATE, NULL),
                 -EBADF, &failures);
  dac_test_check("stop before open", meadow_dac_wave_stop(DAC_TEST_PIN_A00),
                 -EBADF, &failures);

  dac_test_check("open A00", meadow_dac_open(DAC_TEST_PIN_A00, 0), OK,
                 &failures);

  dac_test_check("start with NULL wave",
                 meadow_dac_wave_start(DAC_TEST_PIN_A00, NULL), -EINVAL,
                 &failures);

  dac_test_wave_init(&wave, MEADOW_DAC_WAVE_TRIANGLE, DAC_TEST_TRI_RATE);
  wave.amplitude = 4095;
  wave.size = sizeof(wave) - 4;
  dac_test_check("wrong struct size",
                 meadow_dac_wave_start(DAC_TEST_PIN_A00, &wave), -EINVAL,
                 &failures);

  dac_test_wave_init(&wave, 0, DAC_TEST_TRI_RATE);
  dac_test_check("unknown type",
                 meadow_dac_wave_start(DAC_TEST_PIN_A00, &wave), -EINVAL,
                 &failures);

  dac_test_check("rate 0",
                 dac_test_start_buffer(DAC_TEST_PIN_A00, g_dac_test_sine,
                                       DAC_TEST_SINE_SAMPLES, 0, NULL),
                 -EINVAL, &failures);
  dac_test_check("rate above 1,000,000",
                 dac_test_start_buffer(DAC_TEST_PIN_A00, g_dac_test_sine,
                                       DAC_TEST_SINE_SAMPLES, 1000001,
                                       NULL),
                 -EINVAL, &failures);
  dac_test_check("count 0",
                 dac_test_start_buffer(DAC_TEST_PIN_A00, g_dac_test_sine,
                                       0, DAC_TEST_SINE_RATE, NULL),
                 -EINVAL, &failures);
  dac_test_check("count above 8192",
                 dac_test_start_buffer(DAC_TEST_PIN_A00, g_dac_test_sine,
                                       DAC_TEST_WAVE_MAX_SAMPLES + 1,
                                       DAC_TEST_SINE_RATE, NULL),
                 -EINVAL, &failures);
  dac_test_check("NULL samples",
                 dac_test_start_buffer(DAC_TEST_PIN_A00, NULL,
                                       DAC_TEST_SINE_SAMPLES,
                                       DAC_TEST_SINE_RATE, NULL),
                 -EINVAL, &failures);
  dac_test_check("a sample above 4095",
                 dac_test_start_buffer(DAC_TEST_PIN_A00, bad, 4,
                                       DAC_TEST_SINE_RATE, NULL),
                 -EINVAL, &failures);

  dac_test_wave_init(&wave, MEADOW_DAC_WAVE_TRIANGLE, DAC_TEST_TRI_RATE);
  wave.amplitude = 100;
  dac_test_check("unsupported amplitude",
                 meadow_dac_wave_start(DAC_TEST_PIN_A00, &wave), -EINVAL,
                 &failures);

  meadow_dac_write(DAC_TEST_PIN_A00, 100);
  wave.amplitude = 4095;
  dac_test_check("triangle base + amplitude above 4095",
                 meadow_dac_wave_start(DAC_TEST_PIN_A00, &wave), -EINVAL,
                 &failures);
  meadow_dac_write(DAC_TEST_PIN_A00, 0);

  dac_test_check("stop with no waveform",
                 meadow_dac_wave_stop(DAC_TEST_PIN_A00), -EINVAL,
                 &failures);

  /* A real waveform */

  dac_test_check("start a sine on A00",
                 dac_test_start_buffer(DAC_TEST_PIN_A00, g_dac_test_sine,
                                       DAC_TEST_SINE_SAMPLES,
                                       DAC_TEST_SINE_RATE, &actual),
                 OK, &failures);
  dac_test_check("actual rate is 60000", (int)actual, DAC_TEST_SINE_RATE,
                 &failures);

  changes = dac_test_output_changes(DAC_TEST_PIN_A00);
  dac_test_check("A00 output changes while playing", changes, true,
                 &failures);

  dac_test_check("start again on the same channel",
                 dac_test_start_buffer(DAC_TEST_PIN_A00, g_dac_test_sine,
                                       DAC_TEST_SINE_SAMPLES,
                                       DAC_TEST_SINE_RATE, NULL),
                 -EBUSY, &failures);
  dac_test_check("write during a waveform",
                 meadow_dac_write(DAC_TEST_PIN_A00, 0), -EBUSY, &failures);

  /* The second channel shares TIM6 */

  dac_test_check("open A01", meadow_dac_open(DAC_TEST_PIN_A01, 0), OK,
                 &failures);
  dac_test_check("start A01 at a different rate",
                 dac_test_start_buffer(DAC_TEST_PIN_A01, g_dac_test_sine,
                                       DAC_TEST_SINE_SAMPLES, 50000, NULL),
                 -EBUSY, &failures);
  dac_test_check("start A01 at the same rate",
                 dac_test_start_buffer(DAC_TEST_PIN_A01, g_dac_test_sine,
                                       DAC_TEST_SINE_SAMPLES,
                                       DAC_TEST_SINE_RATE, NULL),
                 OK, &failures);

  /* Stopping A00 must leave A01 playing */

  dac_test_check("stop A00", meadow_dac_wave_stop(DAC_TEST_PIN_A00), OK,
                 &failures);
  dac_test_check("stop A00 again", meadow_dac_wave_stop(DAC_TEST_PIN_A00),
                 -EINVAL, &failures);
  changes = dac_test_output_changes(DAC_TEST_PIN_A01);
  dac_test_check("A01 still playing after A00 stopped", changes, true,
                 &failures);
  dac_test_check("write to A00 after stop",
                 meadow_dac_write(DAC_TEST_PIN_A00, 1234), OK, &failures);
  dac_test_check_value("A00 static again", DAC_TEST_PIN_A00, 1234,
                       &failures);

  /* Closing a channel stops its waveform */

  dac_test_check("close A01 while playing",
                 meadow_dac_close(DAC_TEST_PIN_A01), OK, &failures);
  dac_test_check("close A00", meadow_dac_close(DAC_TEST_PIN_A00), OK,
                 &failures);

  if (failures == 0)
    {
      syslog(LOG_MTEST, "DAC test 7: PASSED\n");
    }
  else
    {
      syslog(LOG_MTEST, "DAC test 7: FAILED, %d check(s) failed\n",
             failures);
    }
}

/****************************************************************************
 * Name: dac_test_wave_report
 *
 * Description:
 *   Log the result of starting a waveform test.
 *
 ****************************************************************************/

static void dac_test_wave_report(int test, const char *what, int ret,
                                 uint32_t actual_hz)
{
  if (ret == OK)
    {
      syslog(LOG_MTEST, "DAC test %d: %s, sample rate %lu Hz. "
             "Run -v 6 to stop it.\n", test, what,
             (unsigned long)actual_hz);
    }
  else
    {
      syslog(LOG_MTEST, "DAC test %d: FAILED to start %s, returned %d\n",
             test, what, ret);
    }
}

/****************************************************************************
 * Name: dac_test_wave_sine
 *
 * Description:
 *   -v 8: 1 kHz sine on A00. -v 12: also a cosine on A01, the same table
 *   started a quarter of a cycle in.
 *
 ****************************************************************************/

static void dac_test_wave_sine(bool both)
{
  static uint16_t cosine[DAC_TEST_SINE_SAMPLES];
  uint32_t actual = 0;
  int ret;
  int i;

  meadow_dac_open(DAC_TEST_PIN_A00, 0);
  ret = dac_test_start_buffer(DAC_TEST_PIN_A00, g_dac_test_sine,
                              DAC_TEST_SINE_SAMPLES, DAC_TEST_SINE_RATE,
                              &actual);
  if (!both)
    {
      dac_test_wave_report(8, "1 kHz sine on A00", ret, actual);
      return;
    }

  for (i = 0; i < DAC_TEST_SINE_SAMPLES; i++)
    {
      cosine[i] = g_dac_test_sine[(i + DAC_TEST_SINE_SAMPLES / 4) %
                                  DAC_TEST_SINE_SAMPLES];
    }

  if (ret == OK)
    {
      meadow_dac_open(DAC_TEST_PIN_A01, 0);
      ret = dac_test_start_buffer(DAC_TEST_PIN_A01, cosine,
                                  DAC_TEST_SINE_SAMPLES, DAC_TEST_SINE_RATE,
                                  &actual);
    }

  dac_test_wave_report(12, "1 kHz sine on A00 and cosine on A01", ret,
                       actual);
}

/****************************************************************************
 * Name: dac_test_wave_saw
 *
 * Description:
 *   -v 9: 100 Hz sawtooth on A00.
 *
 ****************************************************************************/

static void dac_test_wave_saw(void)
{
  static uint16_t saw[DAC_TEST_SAW_SAMPLES];
  uint32_t actual = 0;
  int ret;
  int i;

  for (i = 0; i < DAC_TEST_SAW_SAMPLES; i++)
    {
      saw[i] = (uint16_t)((i * DAC_TEST_MAX_VALUE) /
                          (DAC_TEST_SAW_SAMPLES - 1));
    }

  meadow_dac_open(DAC_TEST_PIN_A00, 0);
  ret = dac_test_start_buffer(DAC_TEST_PIN_A00, saw, DAC_TEST_SAW_SAMPLES,
                              DAC_TEST_SAW_RATE, &actual);
  dac_test_wave_report(9, "100 Hz sawtooth on A00", ret, actual);
}

/****************************************************************************
 * Name: dac_test_wave_generator
 *
 * Description:
 *   -v 10 and -v 11: the DAC's triangle or noise generator on A00, from a
 *   base of 0 (a fresh open starts the output at 0).
 *
 ****************************************************************************/

static void dac_test_wave_generator(int test, uint32_t type,
                                    uint32_t rate_hz, const char *what)
{
  struct meadow_dac_wave_s wave;
  int ret;

  meadow_dac_open(DAC_TEST_PIN_A00, 0);
  dac_test_wave_init(&wave, type, rate_hz);
  wave.amplitude = DAC_TEST_MAX_VALUE;

  ret = meadow_dac_wave_start(DAC_TEST_PIN_A00, &wave);
  dac_test_wave_report(test, what, ret, wave.actual_hz);
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

      case 7:
        dac_test_wave_api();
        break;

      case 8:
        dac_test_wave_sine(false);
        break;

      case 9:
        dac_test_wave_saw();
        break;

      case 10:
        dac_test_wave_generator(10, MEADOW_DAC_WAVE_TRIANGLE,
                                DAC_TEST_TRI_RATE,
                                "triangle (about 122 Hz) on A00");
        break;

      case 11:
        dac_test_wave_generator(11, MEADOW_DAC_WAVE_NOISE,
                                DAC_TEST_NOISE_RATE, "noise on A00");
        break;

      case 12:
        dac_test_wave_sine(true);
        break;

      default:
        syslog(LOG_MTEST, "DAC test: unknown -v %lu. Use 1 (API checks), "
               "2-4 (level steps), 5 (hold A00), 6 (close all), "
               "7 (waveform API checks), 8-12 (waveforms)\n",
               (unsigned long)userdata);
        break;
    }
}

#endif /* CONFIG_DAC_TESTS */
