/****************************************************************************
 * Meadow.OS/kerneltests/meadow_pwm_tests.c
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

// Per-pin PWM output tests. Each test drives one Meadow F7 pin with a PWM
// signal at a fixed duty cycle (20/40/60/80%) and a frequency supplied via
// '-v' (in kHz), for a short window so the signal can be verified with a
// scope or logic analyzer.
//
// Generation calls upd_handle_pwm() (F7MeadowOS/meadow_os/meadow-upd.c)
// directly - the same function that already drives PWM from host/app
// requests in this codebase, rather than duplicating its
// stm32_pwminitialize()/pwm_lowerhalf_s ops sequence here. Earlier attempts
// used stm32_tim_init()/STM32_TIM_*() in
// nuttx/arch/arm/src/stm32f7/stm32_tim.c, whose per-timer "already
// acquired" bookkeeping turned out to read as already-in-use for every
// timer on the very first call after a cold boot on this board (confirmed
// via a compiled-timer bitmap check - see PwmTest.md); a second attempt
// reimplemented upd_handle_pwm()'s logic locally via manual register pokes,
// which worked but needlessly duplicated already-proven code.
//
// This does not open a /dev/pwmN character device or use PWMIOC_* ioctls -
// upd_handle_pwm() is called directly as a C function (it was made
// non-static for this), same as it's called internally from upd_ioctl().

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

// Only build if configured
#if defined(CONFIG_PWM_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)

#pragma message "(--) meadow_pwm_tests.c"

#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>

#include "../hcom_nx/hcom_nx_common.h"
#include <meadow/hcom_shared_common.h>
#include <meadow/meadow_kernel_tests.h>
#include <meadow/meadow_unit_test_framework.h>
#include <meadow/meadow_hw_version.h>
#include <meadow/meadow-upd.h>
#include "stm32_gpio.h"
#include "meadow_kt_log.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

// How long to hold the signal so it can be observed with a scope/logic
// analyzer before the test stops it.
#define MEADOW_PWM_TEST_HOLD_TIME_US (5 * 1000 * 1000)

// Fixed frequency/duty for the "all channels on" test.
#define MEADOW_PWM_ALL_ON_FREQ_KHZ (10)
#define MEADOW_PWM_ALL_ON_DUTY_PCT (50)

// Fixed frequency/duty for the "cycle through all channels" test - only
// the per-channel hold time is configurable, via -v (in seconds).
#define MEADOW_PWM_CYCLE_TEST_FREQ_KHZ (10)
#define MEADOW_PWM_CYCLE_TEST_DUTY_PCT (30)

/****************************************************************************
 * Private Types
 ****************************************************************************/

// One entry per Meadow F7 logical pin that has a PWM-capable timer channel
// on at least one hardware revision.
enum meadow_pwm_pin_e {
    MEADOW_PWM_PIN_D02 = 0,
    MEADOW_PWM_PIN_D03,
    MEADOW_PWM_PIN_D04,
    MEADOW_PWM_PIN_D05,
    MEADOW_PWM_PIN_D06,
    MEADOW_PWM_PIN_D07,
    MEADOW_PWM_PIN_D08,
    MEADOW_PWM_PIN_D09,
    MEADOW_PWM_PIN_D10,
    MEADOW_PWM_PIN_D11,
    MEADOW_PWM_PIN_D12,
    MEADOW_PWM_PIN_D13,
    MEADOW_PWM_PIN_D15,
    MEADOW_PWM_PIN_A01,
    MEADOW_PWM_PIN_A02,
    MEADOW_PWM_PIN_A03,
    MEADOW_PWM_PIN_A04,
    MEADOW_PWM_PIN_COUNT
};

// Indices into the per-pin hardware revision table below. These line up
// with meadow_hw_version_get()'s MEADOW_F7_HW_VERSION_NUMB_* values, minus
// one.
enum meadow_pwm_hw_index_e { MEADOW_PWM_HW_INDEX_F7V1 = 0, MEADOW_PWM_HW_INDEX_F7V2, MEADOW_PWM_HW_INDEX_CCMV2, MEADOW_PWM_HW_INDEX_COUNT };

// The STM32 timer/channel/GPIO a logical pin maps to on one hardware
// revision. timer == 0 means the pin has no PWM-capable path on that
// revision.
struct meadow_pwm_channel_s
{
    uint8_t timer;
    uint8_t channel;
    uint32_t gpioCfg;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

    #define MEADOW_PWM_UNSUPPORTED {0, 0, 0}

// Verified against nuttx/arch/arm/src/stm32f7/hardware/stm32f76xx77xx_pinmap.h,
// nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/include/board.h, and
// F7MeadowOS/include/meadow/meadow_debug_helpers.h - see PwmTest.md.
static const struct meadow_pwm_channel_s _meadowPwmChannelMap[MEADOW_PWM_PIN_COUNT][MEADOW_PWM_HW_INDEX_COUNT] = {
    //                F7v1                             F7v2                             CCMv2
    /* D02 */ {{8, 1, GPIO_TIM8_CH1OUT_1}, {5, 1, GPIO_TIM5_CH1OUT_2}, {5, 1, GPIO_TIM5_CH1OUT_2}},
    /* D03 */ {{4, 3, GPIO_TIM4_CH3OUT_1}, {4, 3, GPIO_TIM4_CH3OUT_1}, {4, 3, GPIO_TIM4_CH3OUT_1}},
    /* D04 */ {{4, 4, GPIO_TIM4_CH4OUT_1}, {4, 4, GPIO_TIM4_CH4OUT_1}, {4, 4, GPIO_TIM4_CH4OUT_1}},
    /* D05 */ {{3, 2, GPIO_TIM3_CH2OUT_3}, {3, 1, GPIO_TIM3_CH1OUT_2}, {3, 1, GPIO_TIM3_CH1OUT_2}},
    /* D06 */ {{3, 3, GPIO_TIM3_CH3OUT_1}, MEADOW_PWM_UNSUPPORTED, MEADOW_PWM_UNSUPPORTED},
    /* D07 */ {{4, 2, GPIO_TIM4_CH2OUT_1}, {4, 2, GPIO_TIM4_CH2OUT_1}, {4, 2, GPIO_TIM4_CH2OUT_1}},
    /* D08 */ {{4, 1, GPIO_TIM4_CH1OUT_1}, {4, 1, GPIO_TIM4_CH1OUT_1}, {4, 1, GPIO_TIM4_CH1OUT_1}},
    /* D09 */ {{3, 4, GPIO_TIM3_CH4OUT_1}, {8, 1, GPIO_TIM8_CH1OUT_1}, MEADOW_PWM_UNSUPPORTED},
    /* D10 */ {{5, 1, GPIO_TIM5_CH1OUT_2}, {3, 2, GPIO_TIM3_CH2OUT_3}, MEADOW_PWM_UNSUPPORTED},
    /* D11 */ {{8, 4, GPIO_TIM8_CH4OUT_1}, {8, 4, GPIO_TIM8_CH4OUT_1}, {1, 3, GPIO_TIM1_CH3OUT_1}},
    /* D12 */ {{12, 1, GPIO_TIM12_CH1OUT_1}, {12, 1, GPIO_TIM12_CH1OUT_1}, {12, 1, GPIO_TIM12_CH1OUT_1}},
    /* D13 */ {{12, 2, GPIO_TIM12_CH2OUT_1}, {12, 2, GPIO_TIM12_CH2OUT_1}, {12, 2, GPIO_TIM12_CH2OUT_1}},
    /* D15 */ {MEADOW_PWM_UNSUPPORTED, MEADOW_PWM_UNSUPPORTED, {8, 3, GPIO_TIM8_CH3OUT_1}},
    /* A01 */ {{2, 1, GPIO_TIM2_CH1OUT_3}, {2, 1, GPIO_TIM2_CH1OUT_3}, {2, 1, GPIO_TIM2_CH1OUT_3}},
    /* A02 */ {{9, 2, GPIO_TIM9_CH2OUT_1}, {9, 2, GPIO_TIM9_CH2OUT_1}, {9, 2, GPIO_TIM9_CH2OUT_1}},
    /* A03 */ {MEADOW_PWM_UNSUPPORTED, {3, 3, GPIO_TIM3_CH3OUT_1}, {3, 3, GPIO_TIM3_CH3OUT_1}},
    /* A04 */ {MEADOW_PWM_UNSUPPORTED, {3, 4, GPIO_TIM3_CH4OUT_1}, {3, 4, GPIO_TIM3_CH4OUT_1}},
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

//===============================================================
// Look up the pin's timer/channel/gpio config for the currently-detected
// hardware revision. Returns false (and logs why) if the pin has no
// PWM-capable path on this revision.
static bool meadow_pwm_lookup_channel(enum meadow_pwm_pin_e pin, const struct meadow_pwm_channel_s **outChan)
{
    enum meadow_pwm_hw_index_e hwIndex;

    switch (meadow_hw_version_get())
    {
        case MEADOW_F7_HW_VERSION_NUMB_F7V1:
            hwIndex = MEADOW_PWM_HW_INDEX_F7V1;
            break;
        case MEADOW_F7_HW_VERSION_NUMB_F7V2:
            hwIndex = MEADOW_PWM_HW_INDEX_F7V2;
            break;
        case MEADOW_F7_HW_VERSION_NUMB_CCMV2:
            hwIndex = MEADOW_PWM_HW_INDEX_CCMV2;
            break;
        default:
            syslog(LOG_MTEST, "PWM test: unable to determine hardware revision\n");
            return false;
    }

    const struct meadow_pwm_channel_s *chan = &_meadowPwmChannelMap[pin][hwIndex];
    if (chan->timer == 0)
    {
        syslog(LOG_MTEST, "PWM test: pin %" PRIu32 " has no PWM-capable timer channel on this hardware revision\n", (uint32_t) pin);
        return false;
    }

    *outChan = chan;
    return true;
}

//===============================================================
// Configure and start the requested pin's timer channel; leaves it
// running. Fills *outCmd with the timer/channel needed to stop it later.
//
// doSetup controls whether MUPD_PWM_SETUP is issued first. pwm_setup()
// (nuttx/arch/arm/src/stm32f7/stm32_pwm.c) explicitly disables the timer's
// counter as part of its documented contract - fine for a single
// standalone pin, but calling it again for a second pin that shares the
// same physical timer (e.g. D03/D04/D07/D08 all share TIM4) would stop the
// first pin's already-running output. Callers driving multiple channels of
// the same timer in one pass (see meadow_pwm_all_on()) must set up each
// distinct timer exactly once, then pass doSetup=false for the rest.
static int meadow_pwm_start_pin(enum meadow_pwm_pin_e pin, uint32_t freqKhz, uint8_t dutyPct, bool doSetup, struct upd_pwm_cmd *outCmd)
{
    if (freqKhz == 0)
    {
        syslog(LOG_MTEST, "PWM test: -v must be a non-zero frequency in kHz\n");
        return TEST_ERR_INVALID_CONFIG;
    }

    const struct meadow_pwm_channel_s *chan;
    if (!meadow_pwm_lookup_channel(pin, &chan))
    {
        return TEST_ERR_INVALID_CONFIG;
    }

    outCmd->timer = chan->timer;
    outCmd->channel = chan->channel;
    outCmd->frequency = freqKhz * 1000;
    outCmd->duty = 0;

    if (doSetup)
    {
        int ret = upd_handle_pwm(MUPD_PWM_SETUP, (unsigned long) outCmd);
        if (ret < 0)
        {
            syslog(LOG_MTEST, "PWM test: failed to set up timer %" PRIu32 "\n", (uint32_t) chan->timer);
            return TEST_ERR_FAILED;
        }
    }

    // MUPD_PWM_SETUP just applied board.h's compile-time default pin for this
    // channel, which is only correct for one hardware revision. Point the
    // channel at the pin this revision actually exposes.
    stm32_configgpio(chan->gpioCfg);

    outCmd->duty = ((uint32_t) dutyPct << 16) / 100; // ub16_t fraction

    syslog(LOG_MTEST, "PWM test: timer %" PRIu32 " channel %" PRIu32 ", %" PRIu32 " kHz, %" PRIu32 "%% duty - started\n",
                      (uint32_t) chan->timer, (uint32_t) chan->channel, freqKhz, (uint32_t) dutyPct);

    upd_handle_pwm(MUPD_PWM_START, (unsigned long) outCmd);

    return TEST_ERR_OK;
}

//===============================================================
// Shut down the requested pin's timer channel. Note this acts on the
// whole timer, not just one channel - any other pin sharing the same
// physical timer (e.g. D03/D04/D07/D08 all share TIM4) is stopped too.
static int meadow_pwm_stop_pin(enum meadow_pwm_pin_e pin)
{
    const struct meadow_pwm_channel_s *chan;
    if (!meadow_pwm_lookup_channel(pin, &chan))
    {
        return TEST_ERR_INVALID_CONFIG;
    }

    struct upd_pwm_cmd cmd;
    cmd.timer = chan->timer;
    cmd.channel = chan->channel;
    cmd.frequency = 0;
    cmd.duty = 0;

    syslog(LOG_MTEST, "PWM test: timer %" PRIu32 " channel %" PRIu32 " - stopped\n",
                      (uint32_t) chan->timer, (uint32_t) chan->channel);

    upd_handle_pwm(MUPD_PWM_SHUTDOWN, (unsigned long) &cmd);

    return TEST_ERR_OK;
}

//===============================================================
// Configure and drive the requested pin's timer channel for holdTimeUs,
// then stop and release it.
static int meadow_pwm_run_test(enum meadow_pwm_pin_e pin, uint32_t freqKhz, uint8_t dutyPct, uint32_t holdTimeUs)
{
    struct upd_pwm_cmd cmd;

    int ret = meadow_pwm_start_pin(pin, freqKhz, dutyPct, true, &cmd);
    if (ret != TEST_ERR_OK)
    {
        return ret;
    }

    usleep(holdTimeUs);

    upd_handle_pwm(MUPD_PWM_SHUTDOWN, (unsigned long) &cmd);

    return TEST_ERR_OK;
}

//===============================================================
// Start every PWM-capable pin at once, at a fixed frequency/duty, and
// leave them all running. Pins with no PWM path on this hardware revision
// are skipped (not a failure); an actual upd_handle_pwm() setup failure is.
static int meadow_pwm_all_on(uint32_t freqKhz, uint8_t dutyPct)
{
    int result = TEST_ERR_OK;

    // Indexed by STM32 timer number (1-14); tracks which timers have already
    // had MUPD_PWM_SETUP issued in this pass, so a second pin sharing an
    // already-set-up timer only gets its own channel started, not re-setup
    // (which would stop the counter the first pin just started).
    bool timerSetUp[15] = {false};

    for (int pin = 0; pin < MEADOW_PWM_PIN_COUNT; pin++)
    {
        const struct meadow_pwm_channel_s *chan;
        if (!meadow_pwm_lookup_channel((enum meadow_pwm_pin_e) pin, &chan))
        {
            continue; // not supported on this hardware revision - skip
        }

        struct upd_pwm_cmd cmd;
        int ret = meadow_pwm_start_pin((enum meadow_pwm_pin_e) pin, freqKhz, dutyPct, !timerSetUp[chan->timer], &cmd);
        if (ret == TEST_ERR_FAILED)
        {
            result = TEST_ERR_FAILED;
        }
        else if (ret == TEST_ERR_OK)
        {
            timerSetUp[chan->timer] = true;
        }
    }

    return result;
}

//===============================================================
// Stop every PWM-capable pin. Pins with no PWM path on this hardware
// revision are skipped.
static int meadow_pwm_all_off(void)
{
    for (int pin = 0; pin < MEADOW_PWM_PIN_COUNT; pin++)
    {
        meadow_pwm_stop_pin((enum meadow_pwm_pin_e) pin);
    }

    return TEST_ERR_OK;
}

//===============================================================
// Cycle through every PWM-capable pin one at a time, at a fixed
// frequency/duty, each held on for holdTimeUs before moving to the next.
static int meadow_pwm_cycle_all(uint32_t holdTimeUs)
{
    int result = TEST_ERR_OK;

    for (int pin = 0; pin < MEADOW_PWM_PIN_COUNT; pin++)
    {
        int ret = meadow_pwm_run_test((enum meadow_pwm_pin_e) pin, MEADOW_PWM_CYCLE_TEST_FREQ_KHZ, MEADOW_PWM_CYCLE_TEST_DUTY_PCT, holdTimeUs);
        if (ret == TEST_ERR_FAILED)
        {
            result = TEST_ERR_FAILED;
        }
    }

    return result;
}

//===============================================================
// Report the final pass/fail result to the CLI that initiated the test.
static void meadow_pwm_report_result(const char *testName, int ret)
{
    if (ret == TEST_ERR_OK)
    {
        syslog(LOG_MTEST, "%s: PASS\n", testName);
    }
    else
    {
        syslog(LOG_MTEST, "%s: FAIL (ret=%d)\n", testName, ret);
    }
}

// One thin wrapper per test ID: the dispatcher requires a distinct
// void(uint32_t) function pointer per test, so each pin+duty combination
// gets its own function that forwards to the shared worker above.
#define MEADOW_PWM_TEST_FUNC(funcName, pinId, dutyPct)                                       \
    void funcName(uint32_t value)                                                            \
    {                                                                                        \
        int ret = meadow_pwm_run_test(pinId, value, dutyPct, MEADOW_PWM_TEST_HOLD_TIME_US);  \
        meadow_pwm_report_result(#funcName, ret);                                            \
    }

/****************************************************************************
 * Public Functions
 ****************************************************************************/

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d02_duty20_test, MEADOW_PWM_PIN_D02, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d02_duty40_test, MEADOW_PWM_PIN_D02, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d02_duty60_test, MEADOW_PWM_PIN_D02, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d02_duty80_test, MEADOW_PWM_PIN_D02, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d03_duty20_test, MEADOW_PWM_PIN_D03, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d03_duty40_test, MEADOW_PWM_PIN_D03, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d03_duty60_test, MEADOW_PWM_PIN_D03, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d03_duty80_test, MEADOW_PWM_PIN_D03, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d04_duty20_test, MEADOW_PWM_PIN_D04, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d04_duty40_test, MEADOW_PWM_PIN_D04, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d04_duty60_test, MEADOW_PWM_PIN_D04, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d04_duty80_test, MEADOW_PWM_PIN_D04, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d05_duty20_test, MEADOW_PWM_PIN_D05, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d05_duty40_test, MEADOW_PWM_PIN_D05, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d05_duty60_test, MEADOW_PWM_PIN_D05, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d05_duty80_test, MEADOW_PWM_PIN_D05, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d06_duty20_test, MEADOW_PWM_PIN_D06, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d06_duty40_test, MEADOW_PWM_PIN_D06, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d06_duty60_test, MEADOW_PWM_PIN_D06, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d06_duty80_test, MEADOW_PWM_PIN_D06, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d07_duty20_test, MEADOW_PWM_PIN_D07, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d07_duty40_test, MEADOW_PWM_PIN_D07, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d07_duty60_test, MEADOW_PWM_PIN_D07, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d07_duty80_test, MEADOW_PWM_PIN_D07, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d08_duty20_test, MEADOW_PWM_PIN_D08, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d08_duty40_test, MEADOW_PWM_PIN_D08, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d08_duty60_test, MEADOW_PWM_PIN_D08, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d08_duty80_test, MEADOW_PWM_PIN_D08, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d09_duty20_test, MEADOW_PWM_PIN_D09, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d09_duty40_test, MEADOW_PWM_PIN_D09, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d09_duty60_test, MEADOW_PWM_PIN_D09, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d09_duty80_test, MEADOW_PWM_PIN_D09, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d10_duty20_test, MEADOW_PWM_PIN_D10, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d10_duty40_test, MEADOW_PWM_PIN_D10, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d10_duty60_test, MEADOW_PWM_PIN_D10, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d10_duty80_test, MEADOW_PWM_PIN_D10, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d11_duty20_test, MEADOW_PWM_PIN_D11, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d11_duty40_test, MEADOW_PWM_PIN_D11, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d11_duty60_test, MEADOW_PWM_PIN_D11, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d11_duty80_test, MEADOW_PWM_PIN_D11, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d12_duty20_test, MEADOW_PWM_PIN_D12, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d12_duty40_test, MEADOW_PWM_PIN_D12, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d12_duty60_test, MEADOW_PWM_PIN_D12, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d12_duty80_test, MEADOW_PWM_PIN_D12, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d13_duty20_test, MEADOW_PWM_PIN_D13, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d13_duty40_test, MEADOW_PWM_PIN_D13, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d13_duty60_test, MEADOW_PWM_PIN_D13, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d13_duty80_test, MEADOW_PWM_PIN_D13, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d15_duty20_test, MEADOW_PWM_PIN_D15, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d15_duty40_test, MEADOW_PWM_PIN_D15, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d15_duty60_test, MEADOW_PWM_PIN_D15, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_d15_duty80_test, MEADOW_PWM_PIN_D15, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a01_duty20_test, MEADOW_PWM_PIN_A01, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a01_duty40_test, MEADOW_PWM_PIN_A01, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a01_duty60_test, MEADOW_PWM_PIN_A01, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a01_duty80_test, MEADOW_PWM_PIN_A01, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a02_duty20_test, MEADOW_PWM_PIN_A02, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a02_duty40_test, MEADOW_PWM_PIN_A02, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a02_duty60_test, MEADOW_PWM_PIN_A02, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a02_duty80_test, MEADOW_PWM_PIN_A02, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a03_duty20_test, MEADOW_PWM_PIN_A03, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a03_duty40_test, MEADOW_PWM_PIN_A03, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a03_duty60_test, MEADOW_PWM_PIN_A03, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a03_duty80_test, MEADOW_PWM_PIN_A03, 80)

MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a04_duty20_test, MEADOW_PWM_PIN_A04, 20)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a04_duty40_test, MEADOW_PWM_PIN_A04, 40)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a04_duty60_test, MEADOW_PWM_PIN_A04, 60)
MEADOW_PWM_TEST_FUNC(meadow_kt_pwm_a04_duty80_test, MEADOW_PWM_PIN_A04, 80)

//===============================================================
// Start every PWM-capable pin at MEADOW_PWM_ALL_ON_FREQ_KHZ /
// MEADOW_PWM_ALL_ON_DUTY_PCT and leave them all running. -v is ignored -
// frequency/duty are fixed by design.
void meadow_kt_pwm_all_on_test(uint32_t value)
{
    (void) value;
    int ret = meadow_pwm_all_on(MEADOW_PWM_ALL_ON_FREQ_KHZ, MEADOW_PWM_ALL_ON_DUTY_PCT);
    meadow_pwm_report_result("meadow_kt_pwm_all_on_test", ret);
}

//===============================================================
// Stop every PWM-capable pin. -v is ignored.
void meadow_kt_pwm_all_off_test(uint32_t value)
{
    (void) value;
    int ret = meadow_pwm_all_off();
    meadow_pwm_report_result("meadow_kt_pwm_all_off_test", ret);
}

//===============================================================
// Cycle through every PWM-capable pin one at a time at
// MEADOW_PWM_CYCLE_TEST_FREQ_KHZ / MEADOW_PWM_CYCLE_TEST_DUTY_PCT, each
// held on for -v seconds before moving to the next.
void meadow_kt_pwm_cycle_all_test(uint32_t value)
{
    if (value == 0)
    {
        syslog(LOG_MTEST, "PWM test: -v must be a non-zero hold time in seconds\n");
        meadow_pwm_report_result("meadow_kt_pwm_cycle_all_test", TEST_ERR_INVALID_CONFIG);
        return;
    }

    int ret = meadow_pwm_cycle_all(value * 1000000);
    meadow_pwm_report_result("meadow_kt_pwm_cycle_all_test", ret);
}

#endif // #if defined(CONFIG_PWM_TESTS)
