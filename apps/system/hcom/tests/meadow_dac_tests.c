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
 *
 * Streaming (issue #662), on A00 at 48,000 Hz. These run for up to 10
 * seconds, within the CLI's default 60-second test timeout.
 *
 *   -v 13 Streaming API checks. No scope needed.
 *   -v 14 Tones for 10 s (500 Hz, 1 kHz, 2 kHz, changing each second) with
 *         the default ring. Expect no gaps and 0 underruns.
 *   -v 15 Deliberate underrun: 2 s of 1 kHz, 2 s of nothing (the fill
 *         value, mid-scale), 2 s of 1 kHz.
 *   -v 16 The same tones as 14 with a 256-sample ring (about 5 ms).
 *   -v 17 The same with a 512-sample ring (about 11 ms).
 *   -v 18 The same with a 1024-sample ring (about 21 ms).
 *
 * Tests 16 to 18 measure how small a ring the test's writer (the HCOM
 * thread) keeps up with, so they report the underrun count rather than
 * passing or failing.
 *
 * One-shot clips (issue #664), played once at 16,000 Hz unless noted. With
 * an amplified speaker on the pin, each test logs what should be heard.
 * They leave the channels open; -v 6 closes them.
 *
 *   -v 19 One-shot API checks, on A00 and A01. No scope needed.
 *   -v 20 A 0.25 s, 1 kHz beep on A00.
 *   -v 21 A 0.25 s rising chirp on A00 at 8,000, 16,000 and 32,000 Hz:
 *         half, normal and double speed.
 *   -v 22 A 0.5 s low tone on A00, replaced after 0.2 s by a high one.
 *   -v 23 A 500 Hz sine loops on A01 at 32,000 Hz while the chirp plays
 *         on A00 at 16,000 Hz, resampled to 32,000 Hz.
 *   -v 24 20 short clips on A00, each replacing the last, 50 to 300 ms
 *         apart.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_DAC_TESTS)

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

/* Streaming tests */

#define DAC_TEST_STREAM_RATE      48000     /* Divides 108 MHz exactly */
#define DAC_TEST_STREAM_FILL      2048      /* Mid-scale: silence */
#define DAC_TEST_STREAM_SMALL     256       /* Smallest ring */
#define DAC_TEST_STREAM_512       512
#define DAC_TEST_STREAM_1024      1024
#define DAC_TEST_STREAM_CHUNK     480       /* 10 ms at 48,000 Hz */
#define DAC_TEST_STREAM_TIMEOUT   100       /* ms per write */
#define DAC_TEST_TONE_SAMPLES     96        /* One 500 Hz cycle at 48 kHz */

/* One-shot tests. The rates divide 108 MHz exactly. */

#define DAC_TEST_CLIP_RATE        16000
#define DAC_TEST_CLIP_FILL        2048      /* Mid-scale: silence */
#define DAC_TEST_BEEP_SAMPLES     4000      /* 0.25 s at 16,000 Hz */
#define DAC_TEST_LOW_SAMPLES      8000      /* 0.5 s */
#define DAC_TEST_BURST_SAMPLES    2400      /* 0.15 s */
#define DAC_TEST_CHIRP_SAMPLES    4000      /* 0.25 s at normal speed */
#define DAC_TEST_CHIRP_FROM       300       /* Hz, at normal speed */
#define DAC_TEST_CHIRP_TO         3000
#define DAC_TEST_RESAMPLE_RATE    48000     /* A01's rate in -v 19 */
#define DAC_TEST_TWO_RATE         32000     /* A01's rate in -v 23 */
#define DAC_TEST_TWO_SINE_SAMPLES 64        /* 500 Hz at 32,000 Hz */

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

/* One cycle of a full-scale sine wave, 96 samples: 500 Hz at 48,000 Hz.
 * Stepping through it 2 or 4 samples at a time gives 1 kHz or 2 kHz.
 */

static const uint16_t g_dac_test_tone[DAC_TEST_TONE_SAMPLES] =
{
  2048, 2181, 2315, 2447, 2577, 2706, 2831, 2953, 3071, 3185, 3294, 3398,
  3495, 3587, 3672, 3750, 3821, 3884, 3939, 3986, 4025, 4056, 4077, 4091,
  4095, 4091, 4077, 4056, 4025, 3986, 3939, 3884, 3821, 3750, 3672, 3587,
  3495, 3398, 3294, 3185, 3071, 2953, 2831, 2706, 2577, 2447, 2315, 2181,
  2048, 1914, 1780, 1648, 1518, 1389, 1264, 1142, 1024, 910, 801, 697,
  600, 508, 423, 345, 274, 211, 156, 109, 70, 39, 18, 4,
  0, 4, 18, 39, 70, 109, 156, 211, 274, 345, 423, 508,
  600, 697, 801, 910, 1024, 1142, 1264, 1389, 1518, 1648, 1780, 1914
};

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
 * Name: dac_test_stream_start
 *
 * Description:
 *   Open A00 and start a stream on it at 48,000 Hz, as Meadow.Core would.
 *
 ****************************************************************************/

static int dac_test_stream_start(uint32_t ring)
{
  struct meadow_dac_wave_s wave;
  int ret;

  ret = meadow_dac_open(DAC_TEST_PIN_A00, 0);
  if (ret < 0)
    {
      return ret;
    }

  dac_test_wave_init(&wave, MEADOW_DAC_WAVE_STREAM, DAC_TEST_STREAM_RATE);
  wave.ring = ring;
  wave.fill = DAC_TEST_STREAM_FILL;
  return meadow_dac_wave_start(DAC_TEST_PIN_A00, &wave);
}

/****************************************************************************
 * Name: dac_test_stream_status
 ****************************************************************************/

static int dac_test_stream_status(struct meadow_dac_wave_status_s *status)
{
  memset(status, 0, sizeof(*status));
  status->size = sizeof(*status);
  return meadow_dac_wave_status(DAC_TEST_PIN_A00, status);
}

/****************************************************************************
 * Name: dac_test_stream_tones
 *
 * Description:
 *   Write 'seconds' of tones to the A00 stream: 500 Hz, 1 kHz and 2 kHz,
 *   changing each second (or 1 kHz only). Returns the number of samples
 *   written, or a negative error.
 *
 ****************************************************************************/

static int dac_test_stream_tones(int seconds, bool change)
{
  static uint16_t chunk[DAC_TEST_STREAM_CHUNK];
  uint32_t total = (uint32_t)seconds * DAC_TEST_STREAM_RATE;
  uint32_t done = 0;
  uint32_t phase = 0;
  uint32_t step;
  uint32_t i;
  int ret;

  while (done < total)
    {
      /* 500 Hz, 1 kHz, 2 kHz, one second each */

      step = change ? (1u << ((done / DAC_TEST_STREAM_RATE) % 3)) : 2;

      for (i = 0; i < DAC_TEST_STREAM_CHUNK; i++)
        {
          chunk[i] = g_dac_test_tone[phase];
          phase = (phase + step) % DAC_TEST_TONE_SAMPLES;
        }

      i = 0;
      while (i < DAC_TEST_STREAM_CHUNK)
        {
          ret = meadow_dac_wave_write(DAC_TEST_PIN_A00, chunk + i,
                                      DAC_TEST_STREAM_CHUNK - i,
                                      DAC_TEST_STREAM_TIMEOUT);
          if (ret < 0)
            {
              return ret;
            }

          i += ret;
        }

      done += DAC_TEST_STREAM_CHUNK;
    }

  return (int)done;
}

/****************************************************************************
 * Name: dac_test_stream_finish
 *
 * Description:
 *   Report the underruns counted while the test was writing, wait (up to a
 *   second) for everything written to play, report the underruns counted
 *   while it drained, then stop the stream and close A00.
 *
 *   The two are reported separately because they mean different things.
 *   'during' is the count when the last write returned: those are halves
 *   DMA reached before the writer had filled them, so the writer was late
 *   (or, for -v 15, the deliberate gap). Once the data runs out, every half
 *   DMA starts plays the fill value and counts as an underrun too, until
 *   the stream is stopped; how many depends only on how long the drain
 *   takes to notice, so those are expected and not judged.
 *
 *   If 'judge' is true the test passes only if 'during' is 0.
 *
 ****************************************************************************/

static void dac_test_stream_finish(int test, uint32_t during, bool judge,
                                   const char *during_note)
{
  struct meadow_dac_wave_status_s status;
  uint32_t after;
  int i;

  for (i = 0; i < 100; i++)
    {
      if (dac_test_stream_status(&status) < 0 || status.queued == 0)
        {
          break;
        }

      usleep(10000);
    }

  after = status.underruns > during ? status.underruns - during : 0;

  syslog(LOG_MTEST, "DAC test %d: %s, %lu underrun(s) while writing (%s); "
         "%lu more while the data drained (expected)\n", test,
         !judge ? "done" : (during == 0 ? "PASSED" : "FAILED"),
         (unsigned long)during, during_note, (unsigned long)after);

  meadow_dac_wave_stop(DAC_TEST_PIN_A00);
  meadow_dac_close(DAC_TEST_PIN_A00);
}

/****************************************************************************
 * Name: dac_test_stream_api
 *
 * Description:
 *   -v 13: check the streaming calls. No scope is needed.
 *
 ****************************************************************************/

static void dac_test_stream_api(void)
{
  static uint16_t samples[DAC_TEST_STREAM_SMALL * 4];
  struct meadow_dac_wave_status_s status;
  struct meadow_dac_wave_s wave;
  int failures = 0;
  int ret;
  int i;

  syslog(LOG_MTEST, "DAC test 13: streaming API checks on A00\n");

  for (i = 0; i < DAC_TEST_STREAM_SMALL * 4; i++)
    {
      samples[i] = g_dac_test_tone[i % DAC_TEST_TONE_SAMPLES];
    }

  meadow_dac_close(DAC_TEST_PIN_A00);
  dac_test_check("open A00", meadow_dac_open(DAC_TEST_PIN_A00, 0), OK,
                 &failures);

  dac_test_check("write with no stream",
                 meadow_dac_wave_write(DAC_TEST_PIN_A00, samples, 10, 0),
                 -ENOTCONN, &failures);
  dac_test_check("status with no stream", dac_test_stream_status(&status),
                 OK, &failures);
  dac_test_check("  state is NONE", (int)status.state,
                 MEADOW_DAC_STREAM_NONE, &failures);

  status.size = 4;
  dac_test_check("status with wrong size",
                 meadow_dac_wave_status(DAC_TEST_PIN_A00, &status), -EINVAL,
                 &failures);

  /* Bad stream set-ups */

  dac_test_wave_init(&wave, MEADOW_DAC_WAVE_STREAM, DAC_TEST_STREAM_RATE);
  wave.fill = DAC_TEST_STREAM_FILL;
  wave.ring = DAC_TEST_STREAM_SMALL - 2;
  dac_test_check("ring below 256",
                 meadow_dac_wave_start(DAC_TEST_PIN_A00, &wave), -EINVAL,
                 &failures);
  wave.ring = 16386;
  dac_test_check("ring above 16384",
                 meadow_dac_wave_start(DAC_TEST_PIN_A00, &wave), -EINVAL,
                 &failures);
  wave.ring = DAC_TEST_STREAM_SMALL + 1;
  dac_test_check("odd ring size",
                 meadow_dac_wave_start(DAC_TEST_PIN_A00, &wave), -EINVAL,
                 &failures);
  wave.ring = DAC_TEST_STREAM_SMALL;
  wave.fill = DAC_TEST_MAX_VALUE + 1;
  dac_test_check("fill above 4095",
                 meadow_dac_wave_start(DAC_TEST_PIN_A00, &wave), -EINVAL,
                 &failures);

  /* Priming */

  dac_test_check("start a 256-sample stream",
                 dac_test_stream_start(DAC_TEST_STREAM_SMALL), OK,
                 &failures);
  dac_test_stream_status(&status);
  dac_test_check("  state is PRIMING", (int)status.state,
                 MEADOW_DAC_STREAM_PRIMING, &failures);
  dac_test_check("  space is 256", (int)status.space,
                 DAC_TEST_STREAM_SMALL, &failures);
  dac_test_check("write during a stream",
                 meadow_dac_write(DAC_TEST_PIN_A00, 0), -EBUSY, &failures);

  samples[5] = DAC_TEST_MAX_VALUE + 1;
  dac_test_check("write with a sample above 4095",
                 meadow_dac_wave_write(DAC_TEST_PIN_A00, samples, 10, 0),
                 -EINVAL, &failures);
  samples[5] = g_dac_test_tone[5];
  dac_test_stream_status(&status);
  dac_test_check("  nothing was taken", (int)status.queued, 0, &failures);

  dac_test_check("write 100, timeout 0",
                 meadow_dac_wave_write(DAC_TEST_PIN_A00, samples, 100, 0),
                 100, &failures);
  dac_test_stream_status(&status);
  dac_test_check("  queued is 100", (int)status.queued, 100, &failures);
  dac_test_check("  still PRIMING", (int)status.state,
                 MEADOW_DAC_STREAM_PRIMING, &failures);

  /* Filling the ring starts output; a full ring takes nothing more */

  dac_test_check("write 156 more, filling the ring",
                 meadow_dac_wave_write(DAC_TEST_PIN_A00, samples + 100, 156,
                                       0),
                 156, &failures);
  dac_test_stream_status(&status);
  dac_test_check("  state is RUNNING", (int)status.state,
                 MEADOW_DAC_STREAM_RUNNING, &failures);

  ret = meadow_dac_wave_write(DAC_TEST_PIN_A00, samples, 1000, 0);
  dac_test_check("write 1000, timeout 0, takes less than 1000",
                 ret >= 0 && ret < 1000, true, &failures);

  dac_test_check("write 512, waiting up to 1 s",
                 meadow_dac_wave_write(DAC_TEST_PIN_A00, samples, 512,
                                       1000),
                 512, &failures);

  /* Stop */

  dac_test_check("stop", meadow_dac_wave_stop(DAC_TEST_PIN_A00), OK,
                 &failures);
  dac_test_check("write after stop",
                 meadow_dac_wave_write(DAC_TEST_PIN_A00, samples, 10, 0),
                 -ENOTCONN, &failures);
  dac_test_check("static write after stop",
                 meadow_dac_write(DAC_TEST_PIN_A00, 1234), OK, &failures);

  /* A zero-length write starts a stream that is not full */

  dac_test_check("start another stream",
                 dac_test_stream_start(DAC_TEST_STREAM_SMALL), OK,
                 &failures);
  meadow_dac_wave_write(DAC_TEST_PIN_A00, samples, 50, 0);
  dac_test_check("write 0 starts output",
                 meadow_dac_wave_write(DAC_TEST_PIN_A00, samples, 0, 0), 0,
                 &failures);
  dac_test_stream_status(&status);
  dac_test_check("  state is RUNNING", (int)status.state,
                 MEADOW_DAC_STREAM_RUNNING, &failures);

  dac_test_check("close while streaming",
                 meadow_dac_close(DAC_TEST_PIN_A00), OK, &failures);

  if (failures == 0)
    {
      syslog(LOG_MTEST, "DAC test 13: PASSED\n");
    }
  else
    {
      syslog(LOG_MTEST, "DAC test 13: FAILED, %d check(s) failed\n",
             failures);
    }
}

/****************************************************************************
 * Name: dac_test_stream_play
 *
 * Description:
 *   -v 14 and -v 16 to -v 18: stream changing tones for 10 seconds with the
 *   given ring. -v 15: 2 s of tone, 2 s with nothing written, 2 s of tone.
 *
 ****************************************************************************/

static void dac_test_stream_play(int test, uint32_t ring, bool gap,
                                 bool judge)
{
  struct meadow_dac_wave_status_s status;
  uint32_t size = ring == 0 ? 8192 : ring;
  const char *note;
  int ret;

  syslog(LOG_MTEST, "DAC test %d: streaming on A00 at %d Hz, ring %lu\n",
         test, DAC_TEST_STREAM_RATE, (unsigned long)size);

  ret = dac_test_stream_start(ring);
  if (ret < 0)
    {
      syslog(LOG_MTEST, "DAC test %d: FAILED to start, returned %d\n",
             test, ret);
      meadow_dac_close(DAC_TEST_PIN_A00);
      return;
    }

  if (!gap)
    {
      ret  = dac_test_stream_tones(10, true);
      note = "times the writer was late";
    }
  else
    {
      ret = dac_test_stream_tones(2, false);
      if (ret >= 0)
        {
          syslog(LOG_MTEST, "    Nothing written for 2 s: expect a flat "
                 "line at mid-scale, and about %lu underruns\n",
                 (unsigned long)((2 * DAC_TEST_STREAM_RATE) /
                                 (size / 2)));
          sleep(2);
          ret = dac_test_stream_tones(2, false);
        }

      note = "mostly the 2 s gap";
    }

  if (ret < 0)
    {
      syslog(LOG_MTEST, "DAC test %d: FAILED, write returned %d\n",
             test, ret);
    }

  /* The count now, as the last write returns: before any drain */

  if (dac_test_stream_status(&status) < 0)
    {
      status.underruns = 0;
    }

  dac_test_stream_finish(test, status.underruns, judge, note);
}

/****************************************************************************
 * Name: dac_test_make_tone
 *
 * Description:
 *   Fill a clip with a full-scale tone that sweeps from f0 to f1 Hz (a
 *   steady tone if they are equal) at rate_hz, from the 96-sample sine
 *   table. The first and last 5 ms fade in and out from mid-scale, so the
 *   clip starts and ends without a click.
 *
 ****************************************************************************/

static void dac_test_make_tone(uint16_t *clip, uint32_t count, uint32_t f0,
                               uint32_t f1, uint32_t rate_hz)
{
  uint32_t fade = rate_hz / 200;
  uint32_t phase = 0;   /* Table position, 16.16 fixed point */
  uint32_t freq;
  uint32_t edge;
  uint32_t j;
  int32_t  s;

  for (j = 0; j < count; j++)
    {
      s = g_dac_test_tone[(phase >> 16) % DAC_TEST_TONE_SAMPLES];

      edge = j < count - 1 - j ? j : count - 1 - j;
      if (edge < fade)
        {
          s = DAC_TEST_MID_SCALE +
              ((s - DAC_TEST_MID_SCALE) * (int32_t)edge) / (int32_t)fade;
        }

      clip[j] = (uint16_t)s;

      freq  = f0 + (uint32_t)(((int64_t)f1 - f0) * j / count);
      phase += (uint32_t)(((uint64_t)freq * DAC_TEST_TONE_SAMPLES << 16) /
                          rate_hz);
      phase %= DAC_TEST_TONE_SAMPLES << 16;
    }
}

/****************************************************************************
 * Name: dac_test_new_clip
 *
 * Description:
 *   Allocate and fill a clip; NULL if there is no memory. The kernel copies
 *   a clip when it starts, so the caller frees it right after.
 *
 ****************************************************************************/

static uint16_t *dac_test_new_clip(uint32_t count, uint32_t f0, uint32_t f1,
                                   uint32_t rate_hz)
{
  uint16_t *clip = malloc(count * sizeof(uint16_t));

  if (clip == NULL)
    {
      syslog(LOG_MTEST, "    FAILED to allocate a %lu-sample clip\n",
             (unsigned long)count);
      return NULL;
    }

  dac_test_make_tone(clip, count, f0, f1, rate_hz);
  return clip;
}

/****************************************************************************
 * Name: dac_test_start_oneshot
 *
 * Description:
 *   Start a one-shot and return meadow_dac_wave_start()'s result. The
 *   actual rate is stored in *actual_hz if it is not NULL.
 *
 ****************************************************************************/

static int dac_test_start_oneshot(uint8_t pinid, const uint16_t *clip,
                                  uint32_t count, uint32_t rate_hz,
                                  uint32_t fill, uint32_t *actual_hz)
{
  struct meadow_dac_wave_s wave;
  int ret;

  dac_test_wave_init(&wave, MEADOW_DAC_WAVE_ONESHOT, rate_hz);
  wave.count   = count;
  wave.samples = clip;
  wave.fill    = fill;

  ret = meadow_dac_wave_start(pinid, &wave);
  if (actual_hz != NULL)
    {
      *actual_hz = wave.actual_hz;
    }

  return ret;
}

/****************************************************************************
 * Name: dac_test_pin_state
 *
 * Description:
 *   Return a channel's waveform state (MEADOW_DAC_STREAM_NONE or
 *   MEADOW_DAC_ONESHOT_PLAYING for a one-shot), or a negative error.
 *
 ****************************************************************************/

static int dac_test_pin_state(uint8_t pinid)
{
  struct meadow_dac_wave_status_s status;
  int ret;

  memset(&status, 0, sizeof(status));
  status.size = sizeof(status);

  ret = meadow_dac_wave_status(pinid, &status);
  return ret < 0 ? ret : (int)status.state;
}

/****************************************************************************
 * Name: dac_test_oneshot_wait
 *
 * Description:
 *   Wait up to timeout_ms for the one-shot on a channel to end. Returns its
 *   final state (MEADOW_DAC_STREAM_NONE once it has ended) and, in
 *   *elapsed_ms, about how long it took, to the nearest 5 ms.
 *
 ****************************************************************************/

static int dac_test_oneshot_wait(uint8_t pinid, uint32_t timeout_ms,
                                 uint32_t *elapsed_ms)
{
  struct timespec start;
  struct timespec now;
  uint32_t ms = 0;
  int state;

  clock_gettime(CLOCK_MONOTONIC, &start);

  for (; ; )
    {
      state = dac_test_pin_state(pinid);

      clock_gettime(CLOCK_MONOTONIC, &now);
      ms = (uint32_t)((now.tv_sec - start.tv_sec) * 1000 +
                      (now.tv_nsec - start.tv_nsec) / 1000000);

      if (state != MEADOW_DAC_ONESHOT_PLAYING || ms >= timeout_ms)
        {
          break;
        }

      usleep(5000);
    }

  if (elapsed_ms != NULL)
    {
      *elapsed_ms = ms;
    }

  return state;
}

/****************************************************************************
 * Name: dac_test_open_audio
 *
 * Description:
 *   Open a channel for audio: buffer on, output at mid-scale (silence), so
 *   the first clip does not start with a click.
 *
 ****************************************************************************/

static int dac_test_open_audio(uint8_t pinid)
{
  int ret = meadow_dac_open(pinid, 0);

  if (ret == OK)
    {
      ret = meadow_dac_write(pinid, DAC_TEST_MID_SCALE);
    }

  return ret;
}

/****************************************************************************
 * Name: dac_test_report_end
 *
 * Description:
 *   Log PASS or FAIL for a one-shot test from its failure count.
 *
 ****************************************************************************/

static void dac_test_report_end(int test, int failures, const char *note)
{
  if (failures == 0)
    {
      syslog(LOG_MTEST, "DAC test %d: PASSED%s\n", test, note);
    }
  else
    {
      syslog(LOG_MTEST, "DAC test %d: FAILED, %d check(s) failed\n", test,
             failures);
    }
}

/****************************************************************************
 * Name: dac_test_oneshot_api
 *
 * Description:
 *   -v 19: check the one-shot calls' error handling and basic behaviour. No
 *   scope is needed.
 *
 ****************************************************************************/

static void dac_test_oneshot_api(void)
{
  static uint16_t bad[4] =
  {
    2048, 1000, DAC_TEST_MAX_VALUE + 1, 2048
  };

  uint16_t *clip;
  uint32_t actual = 0;
  uint32_t ms = 0;
  int failures = 0;
  int ret;

  syslog(LOG_MTEST, "DAC test 19: one-shot API checks on A00 and A01\n");

  meadow_dac_close(DAC_TEST_PIN_A00);
  meadow_dac_close(DAC_TEST_PIN_A01);

  clip = dac_test_new_clip(DAC_TEST_BEEP_SAMPLES, 1000, 1000,
                           DAC_TEST_CLIP_RATE);
  if (clip == NULL)
    {
      return;
    }

  dac_test_check("open A00", dac_test_open_audio(DAC_TEST_PIN_A00), OK,
                 &failures);

  /* Bad arguments */

  dac_test_check("fill above 4095",
                 dac_test_start_oneshot(DAC_TEST_PIN_A00, clip,
                                        DAC_TEST_BEEP_SAMPLES,
                                        DAC_TEST_CLIP_RATE,
                                        DAC_TEST_MAX_VALUE + 1, NULL),
                 -EINVAL, &failures);
  dac_test_check("count 0",
                 dac_test_start_oneshot(DAC_TEST_PIN_A00, clip, 0,
                                        DAC_TEST_CLIP_RATE,
                                        DAC_TEST_CLIP_FILL, NULL),
                 -EINVAL, &failures);
  dac_test_check("count above 8192",
                 dac_test_start_oneshot(DAC_TEST_PIN_A00, clip,
                                        DAC_TEST_WAVE_MAX_SAMPLES + 1,
                                        DAC_TEST_CLIP_RATE,
                                        DAC_TEST_CLIP_FILL, NULL),
                 -EINVAL, &failures);
  dac_test_check("NULL samples",
                 dac_test_start_oneshot(DAC_TEST_PIN_A00, NULL,
                                        DAC_TEST_BEEP_SAMPLES,
                                        DAC_TEST_CLIP_RATE,
                                        DAC_TEST_CLIP_FILL, NULL),
                 -EINVAL, &failures);
  dac_test_check("a sample above 4095",
                 dac_test_start_oneshot(DAC_TEST_PIN_A00, bad, 4,
                                        DAC_TEST_CLIP_RATE,
                                        DAC_TEST_CLIP_FILL, NULL),
                 -EINVAL, &failures);

  /* A clip, left at fill value 1000 so the end is visible on readback */

  dac_test_check("start a 0.25 s clip on A00",
                 dac_test_start_oneshot(DAC_TEST_PIN_A00, clip,
                                        DAC_TEST_BEEP_SAMPLES,
                                        DAC_TEST_CLIP_RATE, 1000, &actual),
                 OK, &failures);
  dac_test_check("actual rate is 16000", (int)actual, DAC_TEST_CLIP_RATE,
                 &failures);
  dac_test_check("status while playing",
                 dac_test_pin_state(DAC_TEST_PIN_A00),
                 MEADOW_DAC_ONESHOT_PLAYING, &failures);
  dac_test_check("write during a clip",
                 meadow_dac_write(DAC_TEST_PIN_A00, 0), -EBUSY, &failures);
  dac_test_check("start a buffer loop over a clip",
                 dac_test_start_buffer(DAC_TEST_PIN_A00, g_dac_test_sine,
                                       DAC_TEST_SINE_SAMPLES,
                                       DAC_TEST_SINE_RATE, NULL),
                 -EBUSY, &failures);
  dac_test_check("replace the playing clip",
                 dac_test_start_oneshot(DAC_TEST_PIN_A00, clip,
                                        DAC_TEST_BEEP_SAMPLES,
                                        DAC_TEST_CLIP_RATE, 1000, NULL),
                 OK, &failures);

  ret = dac_test_oneshot_wait(DAC_TEST_PIN_A00, 1000, &ms);
  dac_test_check("status after the clip ends", ret, MEADOW_DAC_STREAM_NONE,
                 &failures);
  syslog(LOG_MTEST, "    The clip played for about %lu ms (expect about "
         "250)\n", (unsigned long)ms);
  dac_test_check_value("output left at the fill value", DAC_TEST_PIN_A00,
                       1000, &failures);
  dac_test_check("stop after the clip ended",
                 meadow_dac_wave_stop(DAC_TEST_PIN_A00), -EINVAL,
                 &failures);
  dac_test_check("write after the clip ended",
                 meadow_dac_write(DAC_TEST_PIN_A00, DAC_TEST_MID_SCALE), OK,
                 &failures);

  /* The clip's rate was released when it ended: A01 can use another */

  dac_test_check("open A01", meadow_dac_open(DAC_TEST_PIN_A01, 0), OK,
                 &failures);
  dac_test_check("A01 at another rate after the clip",
                 dac_test_start_buffer(DAC_TEST_PIN_A01, g_dac_test_sine,
                                       DAC_TEST_SINE_SAMPLES,
                                       DAC_TEST_RESAMPLE_RATE, NULL),
                 OK, &failures);

  /* A01 now holds TIM6 at 48,000 Hz: a clip at 16,000 Hz is resampled */

  dac_test_check("4000-sample clip too long once resampled (12000)",
                 dac_test_start_oneshot(DAC_TEST_PIN_A00, clip,
                                        DAC_TEST_BEEP_SAMPLES,
                                        DAC_TEST_CLIP_RATE,
                                        DAC_TEST_CLIP_FILL, NULL),
                 -EINVAL, &failures);
  dac_test_check("2000-sample clip resampled (6000)",
                 dac_test_start_oneshot(DAC_TEST_PIN_A00, clip, 2000,
                                        DAC_TEST_CLIP_RATE,
                                        DAC_TEST_CLIP_FILL, &actual),
                 OK, &failures);
  dac_test_check("actual rate is A01's 48000", (int)actual,
                 DAC_TEST_RESAMPLE_RATE, &failures);

  ret = dac_test_oneshot_wait(DAC_TEST_PIN_A00, 1000, &ms);
  dac_test_check("resampled clip ends", ret, MEADOW_DAC_STREAM_NONE,
                 &failures);
  syslog(LOG_MTEST, "    The resampled clip played for about %lu ms "
         "(expect about 125)\n", (unsigned long)ms);
  dac_test_check("A01 still playing after the clip",
                 dac_test_output_changes(DAC_TEST_PIN_A01), true,
                 &failures);

  /* A buffer loop at A01's rate on A00, then a clip over it: -EBUSY */

  dac_test_check("buffer loop on A00",
                 dac_test_start_buffer(DAC_TEST_PIN_A00, g_dac_test_sine,
                                       DAC_TEST_SINE_SAMPLES,
                                       DAC_TEST_RESAMPLE_RATE, NULL),
                 OK, &failures);
  dac_test_check("clip over a buffer loop",
                 dac_test_start_oneshot(DAC_TEST_PIN_A00, clip, 2000,
                                        DAC_TEST_CLIP_RATE,
                                        DAC_TEST_CLIP_FILL, NULL),
                 -EBUSY, &failures);

  /* A clip still playing when its channel closes */

  meadow_dac_wave_stop(DAC_TEST_PIN_A00);
  dac_test_check("start a clip to close",
                 dac_test_start_oneshot(DAC_TEST_PIN_A00, clip, 2000,
                                        DAC_TEST_CLIP_RATE,
                                        DAC_TEST_CLIP_FILL, NULL),
                 OK, &failures);
  dac_test_check("close A00 while a clip plays",
                 meadow_dac_close(DAC_TEST_PIN_A00), OK, &failures);
  dac_test_check("close A01", meadow_dac_close(DAC_TEST_PIN_A01), OK,
                 &failures);

  free(clip);
  dac_test_report_end(19, failures, "");
}

/****************************************************************************
 * Name: dac_test_oneshot_beep
 *
 * Description:
 *   -v 20: one 0.25 s, 1 kHz beep on A00.
 *
 ****************************************************************************/

static void dac_test_oneshot_beep(void)
{
  uint16_t *clip;
  uint32_t actual = 0;
  uint32_t ms = 0;
  int failures = 0;
  int ret;

  syslog(LOG_MTEST, "DAC test 20: a 0.25 s, 1 kHz beep on A00 at %d Hz. "
         "Listen: one short beep. Scope: the burst, then flat at "
         "mid-scale\n", DAC_TEST_CLIP_RATE);

  clip = dac_test_new_clip(DAC_TEST_BEEP_SAMPLES, 1000, 1000,
                           DAC_TEST_CLIP_RATE);
  if (clip == NULL)
    {
      return;
    }

  dac_test_check("open A00", dac_test_open_audio(DAC_TEST_PIN_A00), OK,
                 &failures);
  dac_test_check("start the beep",
                 dac_test_start_oneshot(DAC_TEST_PIN_A00, clip,
                                        DAC_TEST_BEEP_SAMPLES,
                                        DAC_TEST_CLIP_RATE,
                                        DAC_TEST_CLIP_FILL, &actual),
                 OK, &failures);
  free(clip);

  ret = dac_test_oneshot_wait(DAC_TEST_PIN_A00, 1000, &ms);
  dac_test_check("the beep ends", ret, MEADOW_DAC_STREAM_NONE, &failures);
  syslog(LOG_MTEST, "    Played for about %lu ms (expect about 250) at "
         "%lu Hz\n", (unsigned long)ms, (unsigned long)actual);
  dac_test_check_value("output left at mid-scale", DAC_TEST_PIN_A00,
                       DAC_TEST_CLIP_FILL, &failures);

  dac_test_report_end(20, failures, ". Run -v 6 to close A00.");
}

/****************************************************************************
 * Name: dac_test_oneshot_speed
 *
 * Description:
 *   -v 21: the same rising chirp on A00 at half, normal and double speed,
 *   a second apart.
 *
 ****************************************************************************/

static void dac_test_oneshot_speed(void)
{
  static const uint32_t rates[3] =
  {
    DAC_TEST_CLIP_RATE / 2, DAC_TEST_CLIP_RATE, DAC_TEST_CLIP_RATE * 2
  };

  uint16_t *clip;
  uint32_t actual = 0;
  uint32_t ms = 0;
  int failures = 0;
  int ret;
  int i;

  syslog(LOG_MTEST, "DAC test 21: a chirp on A00 at 8000, 16000 and 32000 "
         "Hz. Listen: the same sound at half speed (an octave lower), "
         "normal, then double speed (an octave higher). Scope: bursts of "
         "about 500, 250 and 125 ms\n");

  clip = dac_test_new_clip(DAC_TEST_CHIRP_SAMPLES, DAC_TEST_CHIRP_FROM,
                           DAC_TEST_CHIRP_TO, DAC_TEST_CLIP_RATE);
  if (clip == NULL)
    {
      return;
    }

  dac_test_check("open A00", dac_test_open_audio(DAC_TEST_PIN_A00), OK,
                 &failures);

  for (i = 0; i < 3; i++)
    {
      ret = dac_test_start_oneshot(DAC_TEST_PIN_A00, clip,
                                   DAC_TEST_CHIRP_SAMPLES, rates[i],
                                   DAC_TEST_CLIP_FILL, &actual);
      dac_test_check("start the chirp", ret, OK, &failures);
      dac_test_check("actual rate as asked", (int)actual, (int)rates[i],
                     &failures);

      ret = dac_test_oneshot_wait(DAC_TEST_PIN_A00, 2000, &ms);
      dac_test_check("the chirp ends", ret, MEADOW_DAC_STREAM_NONE,
                     &failures);
      syslog(LOG_MTEST, "    At %lu Hz it played for about %lu ms "
             "(expect about %lu)\n", (unsigned long)rates[i],
             (unsigned long)ms,
             (unsigned long)((DAC_TEST_CHIRP_SAMPLES * 1000) / rates[i]));

      sleep(1);
    }

  free(clip);
  dac_test_report_end(21, failures, ". Run -v 6 to close A00.");
}

/****************************************************************************
 * Name: dac_test_oneshot_replace
 *
 * Description:
 *   -v 22: a 0.5 s low tone on A00, replaced after 0.2 s by a 0.25 s high
 *   tone.
 *
 ****************************************************************************/

static void dac_test_oneshot_replace(void)
{
  uint16_t *low;
  uint16_t *high;
  uint32_t ms = 0;
  int failures = 0;
  int ret;

  syslog(LOG_MTEST, "DAC test 22: a 0.5 s, 400 Hz tone on A00, replaced "
         "after 0.2 s by a 0.25 s, 1.5 kHz tone. Listen: the low tone cut "
         "off by the high one. Scope: about 0.2 s low, 0.25 s high, then "
         "mid-scale\n");

  low  = dac_test_new_clip(DAC_TEST_LOW_SAMPLES, 400, 400,
                           DAC_TEST_CLIP_RATE);
  high = dac_test_new_clip(DAC_TEST_BEEP_SAMPLES, 1500, 1500,
                           DAC_TEST_CLIP_RATE);
  if (low == NULL || high == NULL)
    {
      free(low);
      free(high);
      return;
    }

  dac_test_check("open A00", dac_test_open_audio(DAC_TEST_PIN_A00), OK,
                 &failures);
  dac_test_check("start the low tone",
                 dac_test_start_oneshot(DAC_TEST_PIN_A00, low,
                                        DAC_TEST_LOW_SAMPLES,
                                        DAC_TEST_CLIP_RATE,
                                        DAC_TEST_CLIP_FILL, NULL),
                 OK, &failures);

  usleep(200 * 1000);

  dac_test_check("replace it with the high tone",
                 dac_test_start_oneshot(DAC_TEST_PIN_A00, high,
                                        DAC_TEST_BEEP_SAMPLES,
                                        DAC_TEST_CLIP_RATE,
                                        DAC_TEST_CLIP_FILL, NULL),
                 OK, &failures);
  free(low);
  free(high);

  ret = dac_test_oneshot_wait(DAC_TEST_PIN_A00, 1000, &ms);
  dac_test_check("the high tone ends", ret, MEADOW_DAC_STREAM_NONE,
                 &failures);
  syslog(LOG_MTEST, "    The high tone played for about %lu ms (expect "
         "about 250: the whole clip)\n", (unsigned long)ms);

  dac_test_report_end(22, failures, ". Run -v 6 to close A00.");
}

/****************************************************************************
 * Name: dac_test_oneshot_two_rates
 *
 * Description:
 *   -v 23: a 500 Hz sine loops on A01 at 32,000 Hz; the chirp from -v 21 is
 *   played on A00 at 16,000 Hz, resampled to 32,000 Hz.
 *
 ****************************************************************************/

static void dac_test_oneshot_two_rates(void)
{
  uint16_t *chirp;
  uint16_t *sine;
  uint32_t actual = 0;
  uint32_t ms = 0;
  int failures = 0;
  int ret;
  int i;

  syslog(LOG_MTEST, "DAC test 23: a 500 Hz sine loops on A01 at %d Hz, "
         "then the chirp from -v 21 plays on A00 at %d Hz, resampled. "
         "Listen on A00: the same as the normal-speed chirp in 21. Scope: "
         "A01's sine undisturbed\n", DAC_TEST_TWO_RATE, DAC_TEST_CLIP_RATE);

  chirp = dac_test_new_clip(DAC_TEST_CHIRP_SAMPLES, DAC_TEST_CHIRP_FROM,
                            DAC_TEST_CHIRP_TO, DAC_TEST_CLIP_RATE);
  sine  = malloc(DAC_TEST_TWO_SINE_SAMPLES * sizeof(uint16_t));
  if (chirp == NULL || sine == NULL)
    {
      syslog(LOG_MTEST, "DAC test 23: FAILED to allocate the clips\n");
      free(chirp);
      free(sine);
      return;
    }

  /* One full 500 Hz cycle at 32,000 Hz (64 samples, every 1.5th entry of
   * the 96-sample table) with no fade, so the loop is seamless.
   */

  for (i = 0; i < DAC_TEST_TWO_SINE_SAMPLES; i++)
    {
      sine[i] = g_dac_test_tone[(i * 3 / 2) % DAC_TEST_TONE_SAMPLES];
    }

  dac_test_check("open A01", meadow_dac_open(DAC_TEST_PIN_A01, 0), OK,
                 &failures);
  dac_test_check("start the sine on A01",
                 dac_test_start_buffer(DAC_TEST_PIN_A01, sine,
                                       DAC_TEST_TWO_SINE_SAMPLES,
                                       DAC_TEST_TWO_RATE, NULL),
                 OK, &failures);

  dac_test_check("open A00", dac_test_open_audio(DAC_TEST_PIN_A00), OK,
                 &failures);
  dac_test_check("start the chirp on A00",
                 dac_test_start_oneshot(DAC_TEST_PIN_A00, chirp,
                                        DAC_TEST_CHIRP_SAMPLES,
                                        DAC_TEST_CLIP_RATE,
                                        DAC_TEST_CLIP_FILL, &actual),
                 OK, &failures);
  dac_test_check("A00 plays at A01's rate", (int)actual, DAC_TEST_TWO_RATE,
                 &failures);
  free(chirp);
  free(sine);

  ret = dac_test_oneshot_wait(DAC_TEST_PIN_A00, 2000, &ms);
  dac_test_check("the chirp ends", ret, MEADOW_DAC_STREAM_NONE, &failures);
  syslog(LOG_MTEST, "    The chirp played for about %lu ms (expect about "
         "250, as at normal speed)\n", (unsigned long)ms);
  dac_test_check("A01 still playing",
                 dac_test_output_changes(DAC_TEST_PIN_A01), true,
                 &failures);

  dac_test_report_end(23, failures,
                      ". A01's sine keeps playing; run -v 6 to stop it.");
}

/****************************************************************************
 * Name: dac_test_oneshot_burst
 *
 * Description:
 *   -v 24: 20 short clips on A00, each replacing the last, at 50 to 300 ms
 *   intervals, as a game would fire effects.
 *
 ****************************************************************************/

static void dac_test_oneshot_burst(void)
{
  uint16_t *clip;
  uint32_t seed = 12345;
  uint32_t wait_ms;
  uint32_t f;
  int failures = 0;
  int ret;
  int i;

  syslog(LOG_MTEST, "DAC test 24: 20 clips on A00, each replacing the "
         "last, 50 to 300 ms apart. Listen: a rapid, uninterrupted series "
         "of beeps\n");

  clip = malloc(DAC_TEST_BURST_SAMPLES * sizeof(uint16_t));
  if (clip == NULL)
    {
      syslog(LOG_MTEST, "DAC test 24: FAILED to allocate the clip\n");
      return;
    }

  dac_test_check("open A00", dac_test_open_audio(DAC_TEST_PIN_A00), OK,
                 &failures);

  for (i = 0; i < 20; i++)
    {
      /* A simple pseudo-random pitch and interval */

      seed    = seed * 1103515245 + 12345;
      f       = 600 + ((seed >> 16) % 1800);
      seed    = seed * 1103515245 + 12345;
      wait_ms = 50 + ((seed >> 16) % 251);

      dac_test_make_tone(clip, DAC_TEST_BURST_SAMPLES, f, f,
                         DAC_TEST_CLIP_RATE);
      ret = dac_test_start_oneshot(DAC_TEST_PIN_A00, clip,
                                   DAC_TEST_BURST_SAMPLES,
                                   DAC_TEST_CLIP_RATE, DAC_TEST_CLIP_FILL,
                                   NULL);
      if (ret != OK)
        {
          syslog(LOG_MTEST, "    FAIL: clip %d returned %d\n", i + 1, ret);
          failures++;
        }

      usleep(wait_ms * 1000);
    }

  free(clip);

  sleep(1);
  dac_test_check("no clip playing 1 s after the last",
                 dac_test_pin_state(DAC_TEST_PIN_A00),
                 MEADOW_DAC_STREAM_NONE, &failures);

  dac_test_report_end(24, failures, ". Run -v 6 to close A00.");
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

      case 13:
        dac_test_stream_api();
        break;

      case 14:
        dac_test_stream_play(14, 0, false, true);
        break;

      case 15:
        dac_test_stream_play(15, 0, true, false);
        break;

      case 16:
        dac_test_stream_play(16, DAC_TEST_STREAM_SMALL, false, false);
        break;

      case 17:
        dac_test_stream_play(17, DAC_TEST_STREAM_512, false, false);
        break;

      case 18:
        dac_test_stream_play(18, DAC_TEST_STREAM_1024, false, false);
        break;

      case 19:
        dac_test_oneshot_api();
        break;

      case 20:
        dac_test_oneshot_beep();
        break;

      case 21:
        dac_test_oneshot_speed();
        break;

      case 22:
        dac_test_oneshot_replace();
        break;

      case 23:
        dac_test_oneshot_two_rates();
        break;

      case 24:
        dac_test_oneshot_burst();
        break;

      default:
        syslog(LOG_MTEST, "DAC test: unknown -v %lu. Use 1 (API checks), "
               "2-4 (level steps), 5 (hold A00), 6 (close all), "
               "7 (waveform API checks), 8-12 (waveforms), "
               "13 (streaming API checks), 14-18 (streaming), "
               "19 (one-shot API checks), 20-24 (one-shots)\n",
               (unsigned long)userdata);
        break;
    }
}

#endif /* CONFIG_DAC_TESTS */
