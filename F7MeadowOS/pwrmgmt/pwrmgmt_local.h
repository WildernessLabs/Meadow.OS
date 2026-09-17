/****************************************************************************
 * Meadow.OS/pwrmgmt/pwrmgmt_local.h
 * 
 *   Copyright (C) 2022 Wilderness Labs. All rights reserved.
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

#ifndef __INCLUDE_MEADOW_POWER_MGMT_LOCAL__H
#define __INCLUDE_MEADOW_POWER_MGMT_LOCAL__H

#include <arch/board/board.h>
#include <meadow/hcom_shared_common.h>
#include <meadow/meadow_thread_config.h>

#if defined (CONFIG_MEADOW_PWR_MGMT_SUPPORT)

// This test provides a means to know, and display on syslog, when the clock
// feeding the RTC (HSE or LSI) has changed. Currently disabled: enabling it
// makes pwrmgmt_lsi_calc_prep_thread_func() (F7MeadowOS/pwrmgmt/
// pwrmgmt_calib_lsi_clock.c) never exit after calibration, instead looping
// forever from boot logging a line roughly once per second -- not something
// to switch on just because CONFIG_POWER_MANAGEMENT_TESTS/
// CONFIG_ALL_MEADOW_TESTS is set, since those are otherwise-normal dev
// build configs, not a signal that this specific diagnostic is wanted.
#define PWRMGMT_RTC_SOURCE_CLK_CHANGED_TESTING (0)

// This are defined here because they are not in Nuttx. In Nuttx they are
// hardcoded as 0xff and 7812 or 7182 (an error) in stm32_rtc.c etc.
//
// When HSE drives the RTC, the actual RTC input clock is
// HSE / RTCPRE, where RTCPRE is programmed by NuttX's own
// nuttx/arch/arm/src/stm32f7/stm32f76xx77xx_rcc.c as:
//   HSE_DIVISOR = (STM32_HSE_FREQUENCY + 500000) / 1000000
// That macro is private to that .c file (not exposed via a header), so
// it's mirrored here -- keep in sync if NuttX's own formula ever
// changes. At the current 25MHz crystal this rounds to RTCPRE=25,
// giving exactly 1MHz into the RTC (hence "_FOR_1_MHZ" below).
#define PWRMGMT_HSE_RTC_DIVISOR ((STM32_HSE_FREQUENCY + 500000) / 1000000)
#define PWRMGMT_HSE_RTC_INPUT_FREQUENCY \
  (STM32_HSE_FREQUENCY / PWRMGMT_HSE_RTC_DIVISOR)

// RTC prescalers: (PREDIV_A+1) * (PREDIV_S+1) must equal
// PWRMGMT_HSE_RTC_INPUT_FREQUENCY for an exact 1Hz calendar tick.
// PREDIV_A (the asynchronous prescaler) is kept at STMicro's AN4759
// table 7 choice of 124 -- an independent design choice (maximizing the
// async prescaler for lower power) unrelated to clock frequency.
// PREDIV_S is computed so the product comes out exact, rather than
// hardcoded, so this can't silently go stale the way #431/#594 did if
// the HSE crystal frequency (currently 25MHz, see STM32_BOARD_XTAL in
// board.h) is ever changed.
#define PWRMGMT_CLK_HSE_DIV_A_FACTOR_FOR_1_MHZ (124)    // STMicro's AN4759 table 7
#define PWRMGMT_CLK_HSE_DIV_S_FACTOR_FOR_1_MHZ \
  ((PWRMGMT_HSE_RTC_INPUT_FREQUENCY / \
    (PWRMGMT_CLK_HSE_DIV_A_FACTOR_FOR_1_MHZ + 1)) - 1)

// Which RTC Alarm to use 0 = Alarm A or 1 = Alarm B?
// Note: Alarm B has not been tested
#define PWRMGMT_LOW_PWR_0_USE_RTC_ALARM_A (0)

// Miscellaneous functions
void pwrmgmt_rtc_dumpregs(FAR const char *msg);
void pwrmgmt_rtc_wprunlock(void);
void pwrmgmt_rtc_wprlock(void);
int pwrmgmt_rtc_enterinit(void);
void pwrmgmt_rtc_exitinit(void);
int pwrmgmt_rtc_synchwait(void);
uint32_t pwrmgmt_rtc_bin2bcd(int value);

// Internal to power management
int pwrmgmt_enter_stop_mode(void);

int pwrmgmt_init_lsi_calib(void);
uint32_t pwrmgmt_get_lsi_calib_rtc_clk_value(void);
int pwrmgmt_init_rtc_clk_switch(void);
int pwrmgmt_config_rtc_alarm_wakeup_seconds(time_t secondsTillAlarm);
int pwrmgmt_config_rtc_alarm_wakeup_tm(struct tm tmAlarm);
void pwrmgmt_disable_rtc_alarm_wakeup(void);
int meadow_pwr_mgmt_use_hse_for_rtc(void);
int meadow_pwr_mgmt_use_lsi_for_rtc(void);

  // See PWRMGMT_RTC_SOURCE_CLK_CHANGED_TESTING above before enabling this --
  // it makes pwrmgmt_lsi_calc_prep_thread_func() (pwrmgmt_calib_lsi_clock.c)
  // never exit after calibration, looping forever from boot logging a line
  // roughly once per second.
  #if PWRMGMT_RTC_SOURCE_CLK_CHANGED_TESTING > 0
  void pwrmgmt_rtc_source_clk_changed_flag(bool dbgClkSwitched);
  #endif

#endif  // #if defined (CONFIG_MEADOW_PWR_MGMT_SUPPORT)

#endif // __INCLUDE_MEADOW_POWER_MGMT_LOCAL__H
