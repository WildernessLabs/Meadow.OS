/****************************************************************************
 * Meadow.OS/specialized/meadow_measure_freq.c
 * 
 *   Copyright (C) 2024, 2025 Wilderness Labs. All rights reserved.
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

// This module, uses timers to calculate frequency. It is not designed for
// measuring higher frequencies.
// Since this implementation is fairly simple, and uses interrutps to
// measure the frequency, and since interrupts are called more often for
// higher frequencies there are limitations:
// 1. Frequency / Resolution Limit
//   The maximum frequency for a single channel is about 40 kHz. At this
//   frequency resolution is very limited. At these higher frequencies,
//   quantization causes steps in resolution that get worse as the frequency
//   rises. For example, from 39589 Hz to 40205 Hz, it reads 40000.00Hz. But.
//   at lower frequencies it works perfectly. For example an input of:
//   - 100.00 Hz reads 100.00Hz and an input of 101.00Hz reads 101.00 Hz
//   - 1000.00 Hz reads 1,000.00Hz and an input of 1,010.00Hz reads 1,009.35 Hz
//   - 10,000.00 Hz reads 10,000.00Hz and an input of 10,100.00Hz reads 10,093.46Hz
// 2. Multiple Input Limitions 
//   While several GPIOs can be configured each one generates interrupts.
//   Since the MCU has a fininite amount of processing power, the combination
//   of freqency and the number of inputs is limited. It is reasonable to
//   expect that if the maximum frequency is 40 kHz for 1 channel, then
//   maximum for multiple channels would be there sum, and this should not
//   exceed 40 kHz total.
//   
// Note: a more complex scheme could be designed using DMA or using 2 timers
// per channel, etc.


/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <nuttx/arch.h>
#include <nuttx/mutex.h>
#include <arch/board/board.h>
#include <sys/ioctl.h>
#include <nuttx/timers/timer.h>
#include "stm32_tim.h"
#include "stm32_gpio.h"
#include "stm32_rcc.h"
#include "arm_internal.h"
#include "../hcom_nx/hcom_nx_common.h"
#include "specialized/meadow_measure_freq_local.h"
#include <meadow/meadow_hw_version.h>
#include <meadow/hcom_shared_common.h>
#include <meadow/meadow_measure_freq_shared.h>

#if defined(CONFIG_MEASURE_FREQUENCY_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
// #define USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>

#pragma message "(--) meadow_measure_freq.c"
#endif

// #pragma GCC optimize("O0")    // Prevent compiler from changing the code

/****************************************************************************
 * Private Data
 ****************************************************************************/
// Serializes meadow_measure_freq_configure() and
// meadow_measure_freq_return_freq_info() against each other and against
// themselves. meadow_measure_freq_unconfigure() is static and only ever
// reached through configure()'s MEADOW_MEAS_FREQ_CONF_OPTION_UNCFG branch,
// so it doesn't need its own lock -- its only caller already holds this
// one. Without this, concurrent configure()/unconfigure() calls on the
// same channel could double-free mdwFreqChanData, and return_freq_info()
// could read/write through a pointer freed out from under it by a
// concurrent unconfigure(). The ISR's own enter_critical_section() use is
// unaffected -- it guards against the ISR specifically, not other tasks.
static mutex_t _measureFreqLock = NXMUTEX_INITIALIZER;

/************************************************************************************
 * Private Function Prototypes
 ************************************************************************************/

static int meadow_measure_freq_isr(int irq, void *context, void *arg);
static int meadow_measure_freq_unconfigure(mdwFreqCfgTimer_t *mdwCfgTimerChan);
static int meadow_measure_freq_cfg_timer_hardware(mdwFreqTimerInfo_t *mdwFreqTimerInfo);
static int meadow_measure_freq_cfg_channel_hardware(mdwFreqTimerInfo_t *mdwFreqTimerInfo);
#if MEADOW_MEASURE_FREQ_INCLUDE_REG_DUMP > 0
static void meadow_measure_freq_diag_dump_timer_regs(char *label,
            mdwFreqTimerInfo_t *mdwFreqTimerInfo);
#endif
/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/
// This ISR is called for all frequency measurement interrupts.
// There is a unique ISR vector for each timer. But, each timer must process
// 1 - 4 inputs. On entry we don't know which input(s) is/are involved.
// Measuring 100kHz, average time in ISR measured 1 microsec
int meadow_measure_freq_isr(int irq, void *context, void *arg)
{
#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_ISR_TIME > 0)
  stm32_gpiowrite(DEBUG_GPIO_V2_D06, true);    // Diagnostic only
#endif

  mdwFreqTimerInfo_t *mdwFreqTimerInfo = (mdwFreqTimerInfo_t *)arg;
  uint32_t timerBase = mdwFreqTimerInfo->timerBase;

  // Get the current status to determine why ISR called
  uint16_t timStatusReg = getreg16(timerBase + STM32_GTIM_SR_OFFSET);

  //----------------------------------------------------------
  // Check UIF (Update Interrupt Flag), which indicates CNT changed.
  // In our case timer rollover. Rollover for 16-bit timer is:
  // 1.08 MHz / 65536 = 16.479 times/second.
  if(timStatusReg & GTIM_SR_UIF)
  {
    timStatusReg &= ~GTIM_SR_UIF;

    // What we have are both 16/32-bit timer counts.The 16-bit timers rollover
    // multiple times per second. Therefore, we use another 32-bit uint for
    // the overflow value. This approach effectively adds 32-bits to each
    // timer's length.
    mdwFreqTimerInfo->timerOverflow++;
  }

  // Any other interrupts to handle?
  if(timStatusReg == 0)
  {
    // No, exit
    putreg16(timStatusReg, timerBase + STM32_GTIM_SR_OFFSET);
    return OK;
  }

  //----------------------------------------------------------
  // This part of the ISR has optimized to remove an unneeded loop.

  // CC1IF..CC4IF occupy SR bits 1-4 (see GTIM_SR_CCxIF). Shifted right by 1
  // they line up exactly with ACTIVE_CHAN_BITFIELD_1..4 (bits 0-3), so a
  // single mask tells us which channels are both pending and configured.
  uint8_t pendingCCBits = (timStatusReg >> 1) & 0x0f;

  // All fired CCxIF flags must be cleared here, even for channels that
  // aren't configured, or the interrupt would immediately re-fire.
  timStatusReg &= ~(pendingCCBits << 1);

  uint8_t pendingActive = pendingCCBits & mdwFreqTimerInfo->chanActiveBits;

  // CCR1-CCR4 are contiguous 32-bit registers, so index them directly
  // instead of one getreg32() call per channel.
  volatile uint32_t *ccr =
            (volatile uint32_t *)(timerBase + STM32_GTIM_CCR1_OFFSET);

  while(pendingActive)
  {
    int chan = __builtin_ctz(pendingActive);
    pendingActive &= pendingActive - 1;    // Clear lowest set bit, move on

    mdwFreqChanData_t *mdwFreqChanData = mdwFreqTimerInfo->mdwFreqChanData[chan];

    // chanActiveBits is only set after mdwFreqChanData[chan] is allocated
    // (see meadow_measure_freq_configure), so this should never be NULL.
    DEBUGASSERT(mdwFreqChanData != NULL);

    uint32_t isrCapCount = ccr[chan];

    //----------------------------------------------------------
    // Lastly capture the needed data for each register. Without duty cycle
    // every capture is a rising edge - shift it in unconditionally. With
    // duty cycle, only do so on the rising edge (found by reading the pin
    // level); the falling edge just records the mid-point for the duty
    // cycle ratio. Short-circuit evaluation means stm32_gpioread() is only
    // ever called when duty cycle is actually in use.
    if(!mdwFreqChanData->useDutyCycle ||
       stm32_gpioread(mdwFreqChanData->inputConfig))
    {
      // The end of the previous edge is now the beginning of this count.
      // These are captured so that when the calculations are executed
      // it is free to be unconcerned about changing values.
      mdwFreqChanData->bgnResultCnt = mdwFreqChanData->endResultCnt;
      mdwFreqChanData->bgnResultOvr = mdwFreqChanData->endResultOvr;
      mdwFreqChanData->midResultCnt = mdwFreqChanData->midCaptureCnt;
      mdwFreqChanData->midResultOvr = mdwFreqChanData->midCaptureOvr;

      // Capture end count and current overflow count
      mdwFreqChanData->endResultCnt = isrCapCount;
      mdwFreqChanData->endResultOvr = mdwFreqTimerInfo->timerOverflow;

      // For calculating average frequency
      mdwFreqChanData->gpioCountForAvg++;
    }
    else
    {
      // Duty cycle falling edge
      mdwFreqChanData->midCaptureCnt = isrCapCount;
      mdwFreqChanData->midCaptureOvr = mdwFreqTimerInfo->timerOverflow;
    }
  }

  putreg16(timStatusReg, timerBase + STM32_GTIM_SR_OFFSET);
#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_ISR_TIME > 0)
  stm32_gpiowrite(DEBUG_GPIO_V2_D06, false);    // Diagnostic only
#endif
  return OK;
}

//=============================================================
// Find the RCC clock for enable for the selected timer
static uint32_t meadow_measure_freq_get_apb_clock(
          mdwFreqTimerInfo_t *mdwFreqTimerInfo)
{
  if(mdwFreqTimerInfo->timerAPBClk)
    return STM32_RCC_APB2ENR;
  else
    return STM32_RCC_APB1ENR;
}

//=============================================================
// Timers use 2 different clock sources
static uint32_t meadow_measure_freq_get_max_clock(
          const mdwFreqTimerInfo_t *mdwFreqTimerInfo)
{
  if(mdwFreqTimerInfo->timerMaxClk)
    return STM32_APB2_TIM1_CLKIN;
  else
    return STM32_APB1_TIM2_CLKIN;
}

//=============================================================
// Disable the selected timer
static void meadow_measure_freq_disable(const uint32_t timerBase)
{
  uint16_t regval = getreg16(timerBase + STM32_BTIM_CR1_OFFSET);
  regval &= ~ATIM_CR1_CEN;
  putreg16(regval, timerBase + STM32_BTIM_CR1_OFFSET);
}

//=============================================================
// Enable the selected timer
static void meadow_measure_freq_enable(const uint32_t timerBase)
{
  // Enable timer Counter
  uint16_t cr1Val = getreg16(timerBase + STM32_GTIM_CR1_OFFSET);
  cr1Val |= GTIM_CR1_CEN;
  
  // Re-initialize the counter and generates an update of the registers
  uint16_t egrVal = getreg16(timerBase + STM32_GTIM_EGR_OFFSET);
  egrVal |= GTIM_EGR_UG;

  putreg16(egrVal, timerBase + STM32_GTIM_EGR_OFFSET);
  putreg16(cr1Val, timerBase + STM32_GTIM_CR1_OFFSET);
}

//=====================================================================
// The following returns the specified timer's information or NULL
static mdwFreqTimerInfo_t *meadow_measure_freq_chk_get_timer_info(
          const uint32_t timerNumb)
{
  mdwFreqTimerInfo_t *timerInfo = &(mdwFreqTimerInfoArray[timerNumb - 1]);
  if(timerInfo->timerUsable)
    return timerInfo;

  return NULL;
}

//=====================================================================
// Returns the correct bit field definition based on the timer's channel
static uint8_t meadow_measure_freq_get_channel_bit(const uint32_t timerChan)
{
  switch(timerChan)
  {
    case FREQ_CHANNEL_NUMBER_CHAN_1:
      return ACTIVE_CHAN_BITFIELD_1;

    case FREQ_CHANNEL_NUMBER_CHAN_2:
      return ACTIVE_CHAN_BITFIELD_2;

    case FREQ_CHANNEL_NUMBER_CHAN_3:
      return ACTIVE_CHAN_BITFIELD_3;

    case FREQ_CHANNEL_NUMBER_CHAN_4:
      return ACTIVE_CHAN_BITFIELD_4;

    default:
      return 0;
  }
}

//==================================================================
// Get the current time in nanoseconds. This is used for the average
// frequency calculations.
// Note: Since reading of frequency information is asynchronous with their
// calculation, the average frequence will vary a bit.
static uint64_t meadow_measure_freq_get_current_time(void)
{
  int ret;
  struct tm rtcTime;
  long nsecs;
  long prevNsecs;
  uint64_t returnTime;

#ifdef CONFIG_STM32F7_HAVE_RTC_SUBSECONDS
  do
  {
    // This function reads the date, time and sub-seconds from the MCU's
    // hardware into a struct tm. However, the STM32F77X Errata warns about a
    // possible problem in ES0334-Rev 9 2.12.1 related to the RTC calendar
    // register not locked properly. Therefore, we'll implement the workaround
    // by reading the nsec twice and compare, if different repeat till equal.
    ret = up_rtc_getdatetime_with_subseconds(&rtcTime, &prevNsecs);
    if(ret < 0)
    {
      return ret;
    }

    // Read a second time per Errata
    ret = up_rtc_getdatetime_with_subseconds(&rtcTime, &nsecs);
    if(ret < 0)
    {
      return ret;
    }

    // If they match we have good values
    if(prevNsecs == nsecs)
      break;
      
  } while (1);
#else
  // This function is used if no sub-seconds.
  ret = up_rtc_getdatetime(&rtcTime);
  nsecs = 0;
#endif

  returnTime = mktime(&rtcTime);
  returnTime *= 1000 * 1000 * 1000;
  returnTime += nsecs;

  return returnTime;
}

//=============================================================
// This function will evaluate the GPIO based on 3 tables containing the
// valid GPIOs. For F7v1 and F7v2 a version specific table is checked, and
// then the table for the STM32F7. This function returns the channel, 1-4
// or an error, then returns 0.
static uint8_t meadow_measure_freq_verify_portpin_get_chan(
          const uint32_t timerNumb, uint8_t portAndPin)
{
  int entryCnt;
  uint8_t *entryPtr;

#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT > 0)
  uint8_t dbgPortPin;
#endif

  int gpioArrayOff = timerNumb - 1;
  uint32_t hardwareVersion = meadow_hw_version_get();

  if(hardwareVersion == MEADOW_F7_HW_VERSION_NUMB_F7V1)
  {
#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT > 0)
    syslog(LOG_MDIAG, "--->%s@%d  This is a F7FeatherV1\n",
      __FILE__, __LINE__);
#endif
    // Find offset to top of array
    uint8_t *featherArrayTop = &validF7v1GpioArray[gpioArrayOff][0];

    // We now have the right channel look for the GPIO match. Bound the
    // scan by the real entry count, not the fixed row size -- GPIO_PORTA |
    // GPIO_PIN0 encodes as 0, identical to an unfilled slot's implicit
    // zero-fill, so scanning past the real entries risks a false PA0 match.
    uint8_t validEntryCount = validF7v1GpioArrayCount[gpioArrayOff];
    for(entryCnt = 0;
      entryCnt < validEntryCount;
      entryCnt++)
    {
      entryPtr = featherArrayTop + entryCnt;
      if(*entryPtr == portAndPin)
      {
#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT > 0)
        syslog(LOG_MDIAG,
          "--->%s@%d-Found v1 match for 0x%02x at entry:%d\n",
          __FILE__, __LINE__, portAndPin, entryCnt);
        dbgPortPin = *entryPtr;
#endif
        break;
      }
    }

    if(entryCnt == validEntryCount)
    {
      syslog(LOG_ERR, "Error: No port pin match found\n");
      return 0;
    }

#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT > 0)
    syslog(LOG_MDIAG,
      "--->%s@%d-F7v1 Timer%lu, Offset:%p, 0x%02x (P%c%d)\n\n",
      __FILE__, __LINE__,
      timerNumb, entryPtr, dbgPortPin,
      ((dbgPortPin) >> 4) + 'A', dbgPortPin & 0x0f);
#endif
  }
  else if (hardwareVersion == MEADOW_F7_HW_VERSION_NUMB_F7V2)
  {
#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT > 0)
    // syslog(LOG_MDIAG,
    //    "--->%s@%d All Entries for F7FeatherV2\n", __FILE__, __LINE__);
    // hcom_nx_diag_print_buffer((uint8_t *)&validF7v2GpioArray[0][0],
    //   MEADOW_MEAS_FREQ_NUMB_OF_F7_TIMERS *
    //      MEADOW_MEAS_FREQ_MAX_F7_BYTES_PER_TIMER, 1);
#endif

    // Find offset to top of array
    uint8_t *featherArrayTop = &validF7v2GpioArray[gpioArrayOff][0];

    // We know we have the right channel so we need to look for the GPIO
    // match. Bound the scan by the real entry count, not the fixed row
    // size -- see the matching comment in the F7v1 branch above.
    uint8_t validEntryCount = validF7v2GpioArrayCount[gpioArrayOff];
    for(entryCnt = 0;
      entryCnt < validEntryCount;
      entryCnt++)
    {
      entryPtr = featherArrayTop + entryCnt;
      if(*entryPtr == portAndPin)
      {
#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT > 0)
        syslog(LOG_MDIAG, "--->%s@%d-Found v2 match for 0x%02x at entry:%d\n",
            __FILE__, __LINE__, portAndPin, entryCnt);
        dbgPortPin = *entryPtr;
#endif
        break;
      }
    }

    if(entryCnt == validEntryCount)
    {
      syslog(LOG_ERR, "Error: No port pin match found\n");
      return 0;
    }

#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT > 0)
    syslog(LOG_MDIAG, "--->%s@%d-F7v2 Timer%lu, Offset:%p, 0x%02x (P%c%d)\n",
        __FILE__, __LINE__,
        timerNumb, entryPtr, dbgPortPin,
        ((dbgPortPin) >> 4) + 'A', dbgPortPin & 0x0f);
#endif
  }
  else if (hardwareVersion != MEADOW_F7_HW_VERSION_NUMB_CCMV2)
  {
    // Unsupported board type
    syslog(LOG_ERR, "%s@%d- Unknown board type\n", __FILE__, __LINE__);
    return 0;
  }

  //--------------------------------------------------
  // All hardware boards reach here. With Feather boards we know exactly which
  // GPIOs are available on which pin. But the CCM is used on a number of
  // different boards. Therefore, we cannot do the same level of verification.
  // We need to trust the the users have some idea of what they are doing.

#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT > 0)
  // syslog(LOG_MDIAG, "--->%s@%d All Entries for CCM\n", __FILE__, __LINE__);
  // syslog(LOG_MDIAG, "--->Timer:%lu, Top of CCM array:%p\n",
  //   timerNumb, &validStm32F7GpioArray[0][0]);
  //   hcom_nx_diag_print_buffer((uint8_t *)&validStm32F7GpioArray[0][0],
  //   MEADOW_MEAS_FREQ_NUMB_OF_F7_TIMERS *
  //     MEADOW_MEAS_FREQ_MAX_CCM_BYTES_PER_TIMER, 1);
#endif

  // Get top of array
  mdwFreqChanPortPin_t *ccmArrayTop = &validStm32F7GpioArray[gpioArrayOff][0];
  mdwFreqChanPortPin_t *ccmEntryPtr;

  // Look for a port and pin match
  for(entryCnt = 0;
    entryCnt < MEADOW_MEAS_FREQ_MAX_CCM_ENTRIES_PER_TIMER;
    entryCnt++)
  {
    ccmEntryPtr = ccmArrayTop + entryCnt;

    // Since PA0 is 0x00 need extra test to verify not compiler added padding
    if(ccmEntryPtr->portPin == portAndPin && ccmEntryPtr->chan != 0)
    {
#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT > 0)
      syslog(LOG_MDIAG, "--->%s@%d-Found CCM match for 0x%02x at entry:%d\n",
          __FILE__, __LINE__, portAndPin, entryCnt);
#endif          
      break;
    }
  }

  if(entryCnt == MEADOW_MEAS_FREQ_MAX_CCM_ENTRIES_PER_TIMER)
  {
    syslog(LOG_ERR, "Error: No port pin match found\n");
    return 0;
  }

#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT > 0)
  dbgPortPin = ccmEntryPtr->portPin;
  syslog(LOG_MDIAG,
    "--->EXITING %s@%d-Timer%lu, Offset:%p, 0x%02x (P%c%d), channel:%u\n",
    __FILE__, __LINE__, timerNumb, ccmEntryPtr,
    dbgPortPin, ((dbgPortPin) >> 4) + 'A', dbgPortPin & 0x0f,
    ccmEntryPtr->chan);
#endif

  // Return the channel
  return ccmEntryPtr->chan;
}

/****************************************************************************
 * Public Function
 ****************************************************************************/
// Called by Meadow.Core to configure a timer channel
// Timer numbers range from 1 - 14. However, some are not defined.
int meadow_measure_freq_configure(mdwFreqCfgTimer_t *mdwCfgTimerChan)
{
  int ret;
  uint32_t timerNumber = mdwCfgTimerChan->timerNumber;
  uint32_t channelNumber = mdwCfgTimerChan->channelNumber;
  uint8_t portAndPin = (uint8_t)(mdwCfgTimerChan->portAndPin & 0x000000ff);
  uint32_t inputGpioConfig;
  uint32_t channelOffset = channelNumber - 1;
  bool timerNeedsConfig;
  bool chanNeedsDuty;

#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT > 0)
  syslog(LOG_MDIAG,
    "START CFG %s@%d-TIM%lu, Chn:%lu, input Pin defn:0x%02x (P%c%d)\n",
    __FILE__, __LINE__, timerNumber,
    channelNumber, portAndPin,
    ((portAndPin) >> 4) + 'A', portAndPin & 0x0f);
#endif

  if(timerNumber > 14 || timerNumber < 1)
  {
    syslog(LOG_ERR, "%s@%d-Error:Timer must be 1 - 14\n", __FILE__, __LINE__);
    return MEADOW_MEAS_FREQ_CONF_TIM_NUMB_ILLEGAL;
  }
  
  if(channelNumber > 4 || channelNumber < 1)
  {
    syslog(LOG_ERR, "%s@%d-Error:Channel must be 1 - 4\n", __FILE__, __LINE__);
    return MEADOW_MEAS_FREQ_CONF_CHAN_NUMB_ILLEGAL;
  }

#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_ISR_TIME > 0)
  stm32_unconfiggpio(DEBUG_GPIO_V2_D06);     // Diagnostic only
  stm32_configgpio(DEBUG_GPIO_V2_D06);       // Diagnostic only
  stm32_gpiowrite(DEBUG_GPIO_V2_D06, false); // Diagnostic only
#endif

  // Serialize the remainder of this function -- and meadow_measure_freq_
  // unconfigure(), reached below and only ever called from here -- against
  // concurrent configure()/return_freq_info() calls. See _measureFreqLock.
  ret = nxmutex_lock(&_measureFreqLock);
  if(ret < 0)
  {
    return ret;
  }

  // Configure the timer and channel
  // 1 = Configure without Duty Cycle (reduces interrupts by 50%)
  // 2 = Configure with Duty Cycle
  // 3 = Unconfigure timer
  // All others error
  switch(mdwCfgTimerChan->configOption)
  {
    case MEADOW_MEAS_FREQ_CONF_OPTION_NO_DC:
      chanNeedsDuty = false;
      break;

    case MEADOW_MEAS_FREQ_CONF_OPTION_WITH_DC:
      chanNeedsDuty = true;
      break;

    case MEADOW_MEAS_FREQ_CONF_OPTION_UNCFG:
      // Unconfigure - Also removes Timer if no channels remain
      ret = meadow_measure_freq_unconfigure(mdwCfgTimerChan);
      nxmutex_unlock(&_measureFreqLock);
      return ret;   // Done with unconfigure

    default:
      syslog(LOG_ERR, "%s@%d-Error:Unknown configure option:%" PRIu32 "\n",
        __FILE__, __LINE__, mdwCfgTimerChan->configOption);
      nxmutex_unlock(&_measureFreqLock);
      return MEADOW_MEAS_FREQ_CONF_UNDEFINED_OPTION;
  }

  // Insure a valid timer, channel, port+pin combination was supplied.
  // Timers have, at most, 4 channels, each representing 1 GPIO. For the
  // specified timer we need to verify a proper port and pin.
  uint8_t chanValid4TimerPortPin =
    meadow_measure_freq_verify_portpin_get_chan(timerNumber, portAndPin);
  if(chanValid4TimerPortPin == 0)
  {
    syslog(LOG_ERR,
      "%s@%d-Error:The Port and Pin, not valid for timer %" PRIu32 "\n",
      __FILE__, __LINE__, timerNumber);
    nxmutex_unlock(&_measureFreqLock);
    return MEADOW_MEAS_FREQ_CONF_PORT_PIN_NOT_FOR_TIM;
  }

  // Is this the channel the user wanted?
  if(channelNumber != chanValid4TimerPortPin)
  {
    syslog(LOG_ERR,
      "%s@%d-Error:The Port/Pin/Timer/Channel combination, not valid\n",
      __FILE__, __LINE__);
    nxmutex_unlock(&_measureFreqLock);
    return MEADOW_MEAS_FREQ_CONF_PORT_PIN_TIM_CHAN_INVALID;
  }

  // Check if this timer is useable and get a pointer if it is
  mdwFreqTimerInfo_t *mdwFreqTimerInfo =
            meadow_measure_freq_chk_get_timer_info(timerNumber);
  if(mdwFreqTimerInfo == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Error:Timer %ld unusable\n",
              __FILE__, __LINE__, timerNumber);
    nxmutex_unlock(&_measureFreqLock);
    return MEADOW_MEAS_FREQ_CONF_TIM_NOT_USABLE;
  }

#if MEADOW_MEASURE_FREQ_INCLUDE_REG_DUMP > 0
  meadow_measure_freq_diag_dump_timer_regs(
            "Before Configuration", mdwFreqTimerInfo);
#endif

  // Are any channels already active? If not, timer needs to be initialized
  if(mdwFreqTimerInfo->chanActiveBits == 0)
    timerNeedsConfig = true;
  else
    timerNeedsConfig = false;

  // This timer is usable, but is this channel already being used?
  uint8_t chanBit = meadow_measure_freq_get_channel_bit(channelNumber);
  if(mdwFreqTimerInfo->chanActiveBits & chanBit)
  {
    syslog(LOG_ERR,
      "%s@%d-Error:Timer %" PRIu32 ", channel %" PRIu32 " already in use\n",
      __FILE__, __LINE__, timerNumber, channelNumber);
    nxmutex_unlock(&_measureFreqLock);
    return MEADOW_MEAS_FREQ_CONF_TIM_CHAN_IN_USE;
  }

  // Allocate a struct for each new channel on a timer
  mdwFreqTimerInfo->mdwFreqChanData[channelOffset] =
            (mdwFreqChanData_t*)zalloc(sizeof(mdwFreqChanData_t));
  if(mdwFreqTimerInfo->mdwFreqChanData[channelOffset] == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Error:Allocation for mdwFreqChanData_s\n",
      __FILE__, __LINE__);
    nxmutex_unlock(&_measureFreqLock);
    return MEADOW_MEAS_FREQ_CONF_CHAN_MEM_ALLOC_FAILED;
  }

  // Start populating the channel structure
  mdwFreqTimerInfo->mdwFreqChanData[channelOffset]->useDutyCycle = \
    chanNeedsDuty;

  // Init time and count used to calculate average frequency
  mdwFreqTimerInfo->mdwFreqChanData[channelOffset]->gpioCountForAvg = 0;
  mdwFreqTimerInfo->mdwFreqChanData[channelOffset]->startTimeForAvg =
            meadow_measure_freq_get_current_time();

  // Indicate this channel is being used
  mdwFreqTimerInfo->chanActiveBits |= chanBit;    // Set channel bit

  // Build the GPIO input configuration for Nuttx GPIO processing
  inputGpioConfig = MEADOW_TIMER_GPIO_CONST | portAndPin | \
            mdwFreqTimerInfo->timerAltFunc;

#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT > 0)
  syslog(LOG_MDIAG,
    "--->%s@%d-TIM%lu, Chn:%lu, input Pin defn:0x%02x (P%c%d), AF:%u Pin defn+AF=0x%08lx\n",
    __FILE__, __LINE__, timerNumber,
    channelNumber, portAndPin,
    ((portAndPin) >> 4) + 'A', portAndPin & 0x0f,
    mdwFreqTimerInfo->timerAltFunc >> GPIO_AF_SHIFT,
    inputGpioConfig);
#endif

  // Valided GPIO so, configure input for timer / channel.
  stm32_unconfiggpio(inputGpioConfig);
  ret = stm32_configgpio(inputGpioConfig);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error:stm32_configgpio() returned:%d\n",
              __FILE__, __LINE__, ret);
    free(mdwFreqTimerInfo->mdwFreqChanData[channelOffset]);
    mdwFreqTimerInfo->mdwFreqChanData[channelOffset] = NULL;
    mdwFreqTimerInfo->chanActiveBits &= ~chanBit;
    nxmutex_unlock(&_measureFreqLock);
    return MEADOW_MEAS_FREQ_CONF_CONFIGGPIO_ERR;   // Not supported
  }

  // Save GPIO configuration
  mdwFreqTimerInfo->mdwFreqChanData[channelOffset]->inputConfig = \
            inputGpioConfig;

  // Initialized the F7's channel hardware for this GPIO as input
  ret = meadow_measure_freq_cfg_channel_hardware(mdwFreqTimerInfo);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error:Channel hardware init failed:%d\n",
              __FILE__, __LINE__, ret);
    free(mdwFreqTimerInfo->mdwFreqChanData[channelOffset]);
    mdwFreqTimerInfo->mdwFreqChanData[channelOffset] = NULL;
    mdwFreqTimerInfo->chanActiveBits &= ~chanBit;
    nxmutex_unlock(&_measureFreqLock);
    return MEADOW_MEAS_FREQ_CONF_INIT_CHAN_HW_FAIL;
  }

  // Initialized the F7's timer hardware
  if(timerNeedsConfig)
  {
    // Initialized the F7's timer hardware
    ret = meadow_measure_freq_cfg_timer_hardware(mdwFreqTimerInfo);
    if(ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-Error:Timer hardware init failed:%d\n",
                __FILE__, __LINE__, ret);
      free(mdwFreqTimerInfo->mdwFreqChanData[channelOffset]);
      mdwFreqTimerInfo->mdwFreqChanData[channelOffset] = NULL;
      mdwFreqTimerInfo->chanActiveBits &= ~chanBit;
      nxmutex_unlock(&_measureFreqLock);
      return MEADOW_MEAS_FREQ_CONF_INIT_TIM_HW_FAIL;
    }
  }
#if MEADOW_MEASURE_FREQ_INCLUDE_REG_DUMP > 0
  meadow_measure_freq_diag_dump_timer_regs(
          "After Configuration", mdwFreqTimerInfo);
#endif

  nxmutex_unlock(&_measureFreqLock);
  return MEADOW_MEAS_FREQ_CONF_SUCCESSFUL;
}

//============================================================
// Configuration of timer channel hardware registers.
int meadow_measure_freq_cfg_channel_hardware(
          mdwFreqTimerInfo_t *mdwFreqTimerInfo)
{
  uint32_t regVal32;
  uint16_t ccerRegVal;    // Capture/Compare Enable Register
  uint16_t dierRegVal;    // DMA/Interrupt Enable Register
  uint32_t timerBase = mdwFreqTimerInfo->timerBase;

  ccerRegVal = getreg16(timerBase + STM32_GTIM_CCER_OFFSET);
  dierRegVal = getreg16(timerBase + STM32_GTIM_DIER_OFFSET);
  
  // From STM32F7 Ref man: "Note: CC1S bits are writable only when the
  //    channel is OFF (CC1E = 0 in TIMx_CCER)."
  // We'll just set the entire CCER register to 0.
  putreg16((int)0, timerBase + STM32_GTIM_CCER_OFFSET);

  // Enable timer overrun (overflow)
  dierRegVal |= GTIM_DIER_UIE;

  // Enable the timer inputs for all active channels (all were disabled
  // earlier)
  if(mdwFreqTimerInfo->chanActiveBits & ACTIVE_CHAN_BITFIELD_1)
  {
    // Capture/compare mode reg 1, chan 1
    regVal32 = getreg32(timerBase + STM32_GTIM_CCMR1_OFFSET);
    regVal32 &= ~(GTIM_CCMR1_CC1S_MASK);
    regVal32 |= 0x00000001;       // 1 = 01, set bits 1:0
    putreg32(regVal32, timerBase + STM32_GTIM_CCMR1_OFFSET);

    // Set the input triggers, rising, falling or both for channel. Each
    // active channel's own useDutyCycle drives its polarity bits -- using
    // a single value for all channels would silently reprogram another
    // already-active channel's polarity to match this one.
    if(mdwFreqTimerInfo->mdwFreqChanData[0]->useDutyCycle)
      ccerRegVal |= (GTIM_CCER_CC1P | GTIM_CCER_CC1NP);   // 11 both edges
    else
      ccerRegVal &= ~(GTIM_CCER_CC1P | GTIM_CCER_CC1NP);  // 00 rising only
    ccerRegVal |= GTIM_CCER_CC1E;   // Enable capture channel 1
    dierRegVal |= GTIM_DIER_CC1IE;  // DMA/Interrupt enable
  }

  if(mdwFreqTimerInfo->chanActiveBits & ACTIVE_CHAN_BITFIELD_2)
  {
    // Capture/compare mode reg 1, chan 2
    regVal32 = getreg32(timerBase + STM32_GTIM_CCMR1_OFFSET);
    regVal32 &= ~(GTIM_CCMR1_CC2S_MASK);
    regVal32 |= 0x00000100;   // 0x0100 = 0100, set bits 9:8
    putreg32(regVal32, timerBase + STM32_GTIM_CCMR1_OFFSET);

    if(mdwFreqTimerInfo->mdwFreqChanData[1]->useDutyCycle)
      ccerRegVal |= (GTIM_CCER_CC2P | GTIM_CCER_CC2NP);   // 11 both edges
    else
      ccerRegVal &= ~(GTIM_CCER_CC2P | GTIM_CCER_CC2NP);  // 00 rising only
    ccerRegVal |= GTIM_CCER_CC2E;   // Enable capture channel 2
    dierRegVal |= GTIM_DIER_CC2IE;  // DMA/Interrupt enable
  }

  if(mdwFreqTimerInfo->chanActiveBits & ACTIVE_CHAN_BITFIELD_3)
  {
    // Capture/compare mode reg 2, chan 3
    regVal32 = getreg32(timerBase + STM32_GTIM_CCMR2_OFFSET);
    regVal32 &= ~(GTIM_CCMR2_CC3S_MASK);
    regVal32 |= 0x00000001;   // 1 = 01, set bits 1:0
    putreg32(regVal32, timerBase + STM32_GTIM_CCMR2_OFFSET);

    if(mdwFreqTimerInfo->mdwFreqChanData[2]->useDutyCycle)
      ccerRegVal |= (GTIM_CCER_CC3P | GTIM_CCER_CC3NP);   // 11 both edges
    else
      ccerRegVal &= ~(GTIM_CCER_CC3P | GTIM_CCER_CC3NP);  // 00 rising only
    ccerRegVal |= GTIM_CCER_CC3E;   // Enable capture channel 3
    dierRegVal |= GTIM_DIER_CC3IE;  // DMA/Interrupt enable
  }

  if(mdwFreqTimerInfo->chanActiveBits & ACTIVE_CHAN_BITFIELD_4)
  {
    // Capture/compare mode reg 2, chan 4
    regVal32 = getreg32(timerBase + STM32_GTIM_CCMR2_OFFSET);
    regVal32 &= ~(GTIM_CCMR2_CC4S_MASK);
    regVal32 |= 0x00000100;   // 0x0100 = 0100, set bits 9:8
    putreg32(regVal32, timerBase + STM32_GTIM_CCMR2_OFFSET);

    if(mdwFreqTimerInfo->mdwFreqChanData[3]->useDutyCycle)
      ccerRegVal |= (GTIM_CCER_CC4P | GTIM_CCER_CC4NP);   // 11 both edges
    else
      ccerRegVal &= ~(GTIM_CCER_CC4P | GTIM_CCER_CC4NP);  // 00 rising only
    ccerRegVal |= GTIM_CCER_CC4E;   // Enable capture channel 4
    dierRegVal |= GTIM_DIER_CC4IE;  // DMA/Interrupt enable
  }

  // Clear all interrupt sources and then set the needed ones in the
  // DMA/Interrupt Enable Register.
  // Note: Unsupported Advanced timers 1 & 8 add ATIM_DIER_COMIE,
  // ATIM_DIER_BIE and ATIM_DIER_COMDE.
  modifyreg16(timerBase + STM32_GTIM_DIER_OFFSET,
          (GTIM_DIER_TDE   | GTIM_DIER_CC4DE | GTIM_DIER_CC3DE | GTIM_DIER_CC2DE |
           GTIM_DIER_CC1DE | GTIM_DIER_UDE   | GTIM_DIER_TIE   | GTIM_DIER_CC4IE |
           GTIM_DIER_CC3IE | GTIM_DIER_CC2IE | GTIM_DIER_CC1IE | GTIM_DIER_UIE),
          dierRegVal);

  // Capture/Compare Enable Register 
  putreg16(ccerRegVal, timerBase + STM32_GTIM_CCER_OFFSET);

  return OK;
}

//=============================================================
// Configuration of timer registers.
int meadow_measure_freq_cfg_timer_hardware(mdwFreqTimerInfo_t *mdwFreqTimerInfo)
{
  int ret;
  uint32_t apbClock;
  uint32_t regVal32;
  uint32_t timerBase = mdwFreqTimerInfo->timerBase;
  
  // Slave mode control register
  regVal32 = getreg32(timerBase + STM32_GTIM_SMCR_OFFSET);
  regVal32 &= ~(GTIM_SMCR_ECE | GTIM_SMCR_SMS);
  regVal32 |= GTIM_SMCR_DISAB;
  putreg32(regVal32, timerBase + STM32_GTIM_SMCR_OFFSET);

  // To enable the timer we needed to know which clock enable register to use.
  // And we need to know which bit to set in the register
  apbClock = meadow_measure_freq_get_apb_clock(mdwFreqTimerInfo);
  modifyreg32(apbClock, 0, mdwFreqTimerInfo->timerClkEn);

  // Must be between 0 and 0xffff. Setting the prescaler value to 0 to allow
  // highest speed.
  //Note: A prescaler value of 1 will divide the clock by 2.
  uint16_t prescaler = (meadow_measure_freq_get_max_clock(mdwFreqTimerInfo)/ \
            (MEADOW_FREQ_CLOCK_FREQ) - 1);
  putreg16(prescaler, timerBase + STM32_GTIM_PSC_OFFSET);
#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT > 0)
    syslog(LOG_MDIAG, "--->%s@%d prescaler value:%u\n",
      __FILE__, __LINE__, prescaler);
#endif

  // The value put into the ARR is maximum allowed for the timer. Either
  // 32-bit or 16-bit ARR register.
  uint32_t maxARRValue = mdwFreqTimerInfo->timerWidth ==
            MEADOW_FREQ_TIMER_WIDTH_16 ? 0xffff : 0xffffffff;
  putreg32(maxARRValue, timerBase + STM32_GTIM_ARR_OFFSET);

  // Control Register 1
  uint16_t regval = getreg16(timerBase + STM32_GTIM_CR1_OFFSET);
  regval |= GTIM_CR1_ARPE;    // Auto Reload Pre-Load enable bit
  putreg16(regval, timerBase + STM32_GTIM_CR1_OFFSET);

  // All interrupts are handled by the same ISR code, but each timer has a
  // different interrupt vector.
  ret = irq_attach(mdwFreqTimerInfo->timerIrqVec,
            meadow_measure_freq_isr,  // ISR address
            mdwFreqTimerInfo);        // Argument to ISR
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error:irq_attach failed, ret:%d, errno:%d\n",
          __FILE__, __LINE__, ret, errno);
    return ret;
  }

  // Enable timer
  meadow_measure_freq_enable(timerBase);

  // Lastly enable IRQ
  up_enable_irq(mdwFreqTimerInfo->timerIrqVec);

  return OK;
}

//=============================================================
// Called to unconfigure a timer
// It needs channelNumber etc.
int meadow_measure_freq_unconfigure(mdwFreqCfgTimer_t *mdwCfgTimerChan)
{
  int ret;
  uint32_t apbClock;
  uint32_t regVal32;
  uint32_t channelOffset = mdwCfgTimerChan->channelNumber - 1;
  uint16_t ccerRegVal;    // Capture/Compare Enable Register
  uint16_t dierRegVal;    // DMA/Interrupt Enable Register

  mdwFreqTimerInfo_t *mdwFreqTimerInfo =
    meadow_measure_freq_chk_get_timer_info(mdwCfgTimerChan->timerNumber);
  if(mdwFreqTimerInfo == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Error:Couldn't get TimerInfo via timer number:%lu\n",
      __FILE__, __LINE__, mdwCfgTimerChan->timerNumber);
    return MEADOW_MEAS_FREQ_UNCFG_TIMER_ACCESS_NULL;
  }

  uint32_t timerBase = mdwFreqTimerInfo->timerBase;

  // Is the channel configured?
  uint8_t channelBit = meadow_measure_freq_get_channel_bit(
          mdwCfgTimerChan->channelNumber);
  if((mdwFreqTimerInfo->chanActiveBits & channelBit) == 0)
  {
    syslog(LOG_ERR, "%s@%d-Error:Unconfigure, channel not configured\n",
              __FILE__, __LINE__);
    return MEADOW_MEAS_FREQ_UNCFG_CHAN_NOT_CONFIG;
  }

  mdwFreqChanData_t *mdwFreqChanData =
    mdwFreqTimerInfo->mdwFreqChanData[channelOffset];
  if(mdwFreqChanData == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Error:Channel data NULL\n", __FILE__, __LINE__);
    return MEADOW_MEAS_FREQ_UNCFG_NO_CHANNEL;
  }

  // Stop the timer's clock so values can be written to its registers. Done
  // only after the validity checks above (rather than immediately after
  // resolving mdwFreqTimerInfo) so that rejecting an invalid/unconfigured
  // channel never gates off the clock -- and therefore any other
  // already-active channel on this same timer -- as a side effect.
  apbClock = meadow_measure_freq_get_apb_clock(mdwFreqTimerInfo);
  modifyreg32(apbClock, mdwFreqTimerInfo->timerClkEn, 0);

  // Unconfigure channel hardware
  ccerRegVal = getreg16(timerBase + STM32_GTIM_CCER_OFFSET);
  dierRegVal = getreg16(timerBase + STM32_GTIM_DIER_OFFSET);
  channelBit = mdwFreqTimerInfo->chanActiveBits;

  // Modifies the requested channel
  switch(mdwCfgTimerChan->channelNumber)
  {
    case FREQ_CHANNEL_NUMBER_CHAN_1:
      regVal32 = getreg32(timerBase + STM32_GTIM_CCMR1_OFFSET);
      regVal32 &= ~(GTIM_CCMR1_CC1S_MASK);
      putreg32(regVal32, timerBase + STM32_GTIM_CCMR1_OFFSET);

      channelBit &= ~ACTIVE_CHAN_BITFIELD_1;  // Clear channel field
      // Reset Capture/Compare Enable Register
      ccerRegVal &= ~(GTIM_CCER_CC1E | GTIM_CCER_CC1P | GTIM_CCER_CC1NP);
      dierRegVal &= ~GTIM_DIER_CC1IE;         // DMA/Interrupt disable
      break;

    case FREQ_CHANNEL_NUMBER_CHAN_2:
      // Capture/compare mode reg 1, chan 2
      regVal32 = getreg32(timerBase + STM32_GTIM_CCMR1_OFFSET);
      regVal32 &= ~(GTIM_CCMR1_CC2S_MASK);
      putreg32(regVal32, timerBase + STM32_GTIM_CCMR1_OFFSET);

      channelBit &= ~ACTIVE_CHAN_BITFIELD_2;
      ccerRegVal &= ~(GTIM_CCER_CC2E | GTIM_CCER_CC2P | GTIM_CCER_CC2NP);
      dierRegVal &= ~GTIM_DIER_CC2IE;
      break;

    case FREQ_CHANNEL_NUMBER_CHAN_3:
      // Capture/compare mode reg 2, chan 3
      regVal32 = getreg32(timerBase + STM32_GTIM_CCMR2_OFFSET);
      regVal32 &= ~(GTIM_CCMR2_CC3S_MASK);
      putreg32(regVal32, timerBase + STM32_GTIM_CCMR2_OFFSET);

      channelBit &= ~ACTIVE_CHAN_BITFIELD_3;
      ccerRegVal &= ~(GTIM_CCER_CC3E | GTIM_CCER_CC3P | GTIM_CCER_CC3NP);
      dierRegVal &= ~GTIM_DIER_CC3IE;
      break;

    case FREQ_CHANNEL_NUMBER_CHAN_4:
      // Capture/compare mode reg 2, chan 4
      regVal32 = getreg32(timerBase + STM32_GTIM_CCMR2_OFFSET);
      regVal32 &= ~(GTIM_CCMR2_CC4S_MASK);
      putreg32(regVal32, timerBase + STM32_GTIM_CCMR2_OFFSET);

      channelBit &= ~ACTIVE_CHAN_BITFIELD_4;
      ccerRegVal &= ~(GTIM_CCER_CC4E | GTIM_CCER_CC4P | GTIM_CCER_CC4NP);
      dierRegVal &= ~GTIM_DIER_CC4IE;
      break;
  }

  // Update the new register values
  putreg16(ccerRegVal, timerBase + STM32_GTIM_CCER_OFFSET);
  putreg16(dierRegVal, timerBase + STM32_GTIM_DIER_OFFSET);

  // Save the remaining active channels, if there are any.
  mdwFreqTimerInfo->chanActiveBits = channelBit;

  // Unconfigure no longer needed GPIO
  stm32_unconfiggpio(mdwFreqChanData->inputConfig);

  // If there are no active channels, unconfigure the timer itself.
  // It's clock was already turned off.
  if(channelBit == 0)
  {
    syslog(LOG_INFO, "-->%s@%d-Removing Timer, no remaining channels\n",
              __FILE__, __LINE__);

    // Stop interrupts
    up_disable_irq(mdwFreqTimerInfo->timerIrqVec);

    // Since no remaining channels, stop calling the ISR for overflows
    dierRegVal &= ~GTIM_DIER_UIE;
    putreg16(dierRegVal, timerBase + STM32_GTIM_DIER_OFFSET);

    // No need to auto-reload CNT
    uint16_t regval = getreg16(timerBase + STM32_GTIM_CR1_OFFSET);
    regval &= ~GTIM_CR1_ARPE;    // Auto Reload Pre-Load enable bit
    putreg16(regval, timerBase + STM32_GTIM_CR1_OFFSET);

    // Detach ISR
    ret = irq_detach(mdwFreqTimerInfo->timerIrqVec);
    if(ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-irq_detach failed, ret:%d, errno:%d\n",
            __FILE__, __LINE__, ret, errno);
      return MEADOW_MEAS_FREQ_UNCFG_IRQ_DETACH_ERR;
    }

    // Disable timer
    meadow_measure_freq_disable(mdwFreqTimerInfo->timerBase);

    // Clear modifiable timer fields
    mdwFreqTimerInfo->timerOverflow  = 0;
    mdwFreqTimerInfo->chanActiveBits = 0;
  }
  else
  {
    // Still active channels so restart the timer clock
    apbClock = meadow_measure_freq_get_apb_clock(mdwFreqTimerInfo);
    modifyreg32(apbClock, 0, mdwFreqTimerInfo->timerClkEn);
  }

  // Free channel runtime memory
  free(mdwFreqChanData);
  mdwFreqTimerInfo->mdwFreqChanData[channelOffset] = NULL;

  return MEADOW_MEAS_FREQ_UNCFG_SUCCESSFUL;
}

//================================================================
// Public entry point
// Return Frequency and Duty Cycle information to caller.
int meadow_measure_freq_return_freq_info(mdwFreqReturnData_t *returnData)
{
  int ret;
  // 64 bytes of stack space
  double dblFrequency;
  double dblDutyCycle;
  double dblTotalInputCount;
  double dblAvgFreq;
  uint64_t halfCycle;
  uint64_t fullCycle;
  uint64_t regOvrFloTimSize;
  uint64_t bgnCapture;
  uint64_t midCapture;
  uint64_t endCapture;
  uint64_t totalCaptureTime;

  // Verify that provided timer and channel are valid
  if(returnData->timerNumber > 14 || returnData->timerNumber < 1)
  {
    syslog(LOG_ERR, "%s@%d-Timer must be 1 - 14, was:%lu\n",
      __FILE__, __LINE__, returnData->timerNumber);
    return MEADOW_MEAS_FREQ_READ_INVALID_TIMER_NUMB;
  }

  if(returnData->channelNumber > 4 || returnData->channelNumber < 1)
  {
    syslog(LOG_ERR, "%s@%d-Channel must be 1 - 4, was:%lu\n",
      __FILE__, __LINE__, returnData->channelNumber);
    return MEADOW_MEAS_FREQ_READ_INVALID_CHANNEL_NUMB;
  }

  // Serialize the remainder of this function against concurrent
  // configure()/unconfigure() calls. See _measureFreqLock.
  ret = nxmutex_lock(&_measureFreqLock);
  if(ret < 0)
  {
    return ret;
  }

  mdwFreqTimerInfo_t *mdwFreqTimerInfo =
            meadow_measure_freq_chk_get_timer_info(returnData->timerNumber);
  if(mdwFreqTimerInfo == NULL)
  {
    syslog(LOG_ERR,
      "%s@%d-Error:Couldn't get TimerInfo from timer number:%lu\n",
      __FILE__, __LINE__, returnData->timerNumber);
    nxmutex_unlock(&_measureFreqLock);
    return MEADOW_MEAS_FREQ_READ_TIMER_ACCESS_NULL;
  }

#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT > 0)
  // syslog(LOG_MDIAG,
  //   "--->%s@%d-Data-for timer:%lu, chan:%lu, active chans:0x%02x\n",
  //   __FILE__, __LINE__,
  //   returnData->timerNumber, returnData->channelNumber,
  //   mdwFreqTimerInfo->chanActiveBits);
#endif

  // Is the channel configured?
  uint8_t channelBit = meadow_measure_freq_get_channel_bit(
          returnData->channelNumber);
  if((mdwFreqTimerInfo->chanActiveBits & channelBit) == 0)
  {
    syslog(LOG_ERR,
      "%s@%d-Error:Unconfigured channel accessed\n", __FILE__, __LINE__);
    nxmutex_unlock(&_measureFreqLock);
    return MEADOW_MEAS_FREQ_READ_CHAN_NOT_CONFIG;
  }

  mdwFreqChanData_t *mdwFreqChanData =
            mdwFreqTimerInfo->mdwFreqChanData[returnData->channelNumber - 1];
  if(mdwFreqChanData == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Error:Channel data NULL\n", __FILE__, __LINE__);
    nxmutex_unlock(&_measureFreqLock);
    return MEADOW_MEAS_FREQ_READ_CHANNEL_DATA_NULL;
  }

  // The fields below are written by the ISR at interrupt context, so they
  // must be snapshotted (and gpioCountForAvg reset) atomically with respect
  // to it. Otherwise a torn read could pair a stale 'bgn'/'mid' with a
  // fresh 'end', or lose a count increment that lands between the read and
  // the reset.
  bool useDutyCycle;
  uint32_t bgnResultCnt, bgnResultOvr, midResultCnt, midResultOvr;
  uint32_t endResultCnt, endResultOvr;
  uint64_t gpioCountForAvg;
  uint64_t startTimeForAvg;

  irqstate_t flags = enter_critical_section();

  useDutyCycle    = mdwFreqChanData->useDutyCycle;
  bgnResultCnt    = mdwFreqChanData->bgnResultCnt;
  bgnResultOvr    = mdwFreqChanData->bgnResultOvr;
  midResultCnt    = mdwFreqChanData->midResultCnt;
  midResultOvr    = mdwFreqChanData->midResultOvr;
  endResultCnt    = mdwFreqChanData->endResultCnt;
  endResultOvr    = mdwFreqChanData->endResultOvr;
  gpioCountForAvg = mdwFreqChanData->gpioCountForAvg;
  startTimeForAvg = mdwFreqChanData->startTimeForAvg;

  mdwFreqChanData->gpioCountForAvg = 0;

  leave_critical_section(flags);

  // Any activity since last time this code was last executed?
  if(gpioCountForAvg == 0)
  {
    returnData->dutyCycleX1000  = 0;
    returnData->frequencyX1000  = 0;
    returnData->avgFreqX1000    = 0;
    returnData->gpioCountForAvg = 0;

    // Reset capture time for next average
    mdwFreqChanData->startTimeForAvg = meadow_measure_freq_get_current_time();

    syslog(LOG_ERR, "%s@%d-Error:No freq input since last read\n",
              __FILE__, __LINE__);
    nxmutex_unlock(&_measureFreqLock);
    return MEADOW_MEAS_FREQ_READ_FREQ_INPUT_NOT_DETECTED;
  }

  //-------------------------------------------------------
  // We need the timer's width to do the overflow math
  if(mdwFreqTimerInfo->timerWidth)
    regOvrFloTimSize = MEADOW_FREQ_32_BIT_OVERFLOW_COUNT;
  else
    regOvrFloTimSize = MEADOW_FREQ_16_BIT_OVERFLOW_COUNT;

  // Add in the overflow counts, normalized relative to bgnResultOvr so the
  // multiply stays on small deltas instead of the free-running (and
  // ever-growing) absolute overflow count. bgnCapture needs no overflow
  // term of its own: relative to its own baseline, it's always zero.
  bgnCapture = bgnResultCnt;
  midCapture = midResultCnt + \
        ((midResultOvr - bgnResultOvr) * regOvrFloTimSize);
  endCapture = endResultCnt + \
        ((endResultOvr - bgnResultOvr) * regOvrFloTimSize);

  // Normalize from the beginning
  fullCycle = endCapture - bgnCapture;
  halfCycle = midCapture - bgnCapture;

  // A zero-length cycle is only possible if an edge landed exactly on
  // CNT==0 on the very first capture; guard against the divide-by-zero
  // below rather than propagating inf/NaN into the uint32_t return fields.
  if(fullCycle == 0)
  {
    syslog(LOG_ERR, "%s@%d-Error:Zero-length cycle, discarding sample\n",
              __FILE__, __LINE__);
    nxmutex_unlock(&_measureFreqLock);
    return MEADOW_MEAS_FREQ_READ_FREQ_INPUT_NOT_DETECTED;
  }

  // To preserve resolution, use floating point math.
  // Duty Cycle is the ratio of the full cycle count and the cycle count
  // before the trailing edge was detected.
  if(useDutyCycle)
    dblDutyCycle = (((double)halfCycle) * 100.0) / ((double)fullCycle);
  else
    dblDutyCycle = 0;

  // The dblFrequency is the timer's clock divided by the cycle count.
  dblFrequency = ((double)MEADOW_FREQ_CLOCK_FREQ) / ((double)fullCycle);

  // Average frequency since last calculated
  totalCaptureTime =
            meadow_measure_freq_get_current_time() - \
            startTimeForAvg;

  // Convert time in nanoseconds to fractional seconds
  double dblTotalCaptureTime =\
            ((double) totalCaptureTime) / (1000.0 * 1000.0 * 1000.0);
  dblTotalInputCount = (double)gpioCountForAvg;

  // Guard against a zero (or near-zero) elapsed time -- the RTC subsecond
  // counter has coarse (millisecond-scale) resolution, so two calls landing
  // within the same tick (plausible under a tight polling loop) would
  // otherwise divide by zero here, producing +Infinity and then
  // implementation-defined/undefined behavior when assigned into the
  // uint32_t avgFreqX1000 return field below. Skip the average-frequency
  // update for this call rather than propagate a bogus reading.
  if(dblTotalCaptureTime > 0.0)
    dblAvgFreq = ((double)dblTotalInputCount) / dblTotalCaptureTime;
  else
    dblAvgFreq = 0;

  if(useDutyCycle)
    returnData->dutyCycleX1000  = (dblDutyCycle * 1000.0);
  else
    returnData->dutyCycleX1000  = 0;

  returnData->frequencyX1000    = (dblFrequency * 1000.0);
  returnData->avgFreqX1000      = (dblAvgFreq   * 1000.0);
  returnData->gpioCountForAvg   = (uint32_t)dblTotalInputCount;

  // Reset Meadow time for next average. gpioCountForAvg was already reset
  // above, atomically with the snapshot.
  // Since reading the information is asynchronous with these calculation
  // the average frequence can vary a bit.
  mdwFreqChanData->startTimeForAvg = meadow_measure_freq_get_current_time();

  nxmutex_unlock(&_measureFreqLock);
  return MEADOW_MEAS_FREQ_READ_SUCCESSFUL;
}

#if MEADOW_MEASURE_FREQ_INCLUDE_REG_DUMP > 0
//=================================================================
// Dump all timer registers
void meadow_measure_freq_diag_dump_timer_regs(char *label,
          mdwFreqTimerInfo_t *mdwFreqTimerInfo)
{
  uint32_t timerBase = mdwFreqTimerInfo->timerBase;

  syslog(LOG_MDIAG,
    "\nTimer:%lu Register Dump-%s\n", mdwFreqTimerInfo->timerNumb, label);
  syslog(LOG_MDIAG,
    "\tCR1:\t0x%08x\tCR2:\t0x%08x\tSMCR:\t0x%08x\tDIER:\t0x%08x\n",
    getreg16(timerBase + STM32_GTIM_CR1_OFFSET),
    getreg16(timerBase + STM32_GTIM_CR2_OFFSET),
    getreg32(timerBase + STM32_GTIM_SMCR_OFFSET),
    getreg16(timerBase + STM32_GTIM_DIER_OFFSET));

  syslog(LOG_MDIAG,
    "\tSR:\t0x%08x\tEGR:\t0x%08x\tCCMR1:\t0x%08x\tCCMR2:\t0x%08x\n",
    getreg16(timerBase + STM32_GTIM_SR_OFFSET),
    getreg16(timerBase + STM32_GTIM_EGR_OFFSET),
    getreg32(timerBase + STM32_GTIM_CCMR1_OFFSET),
    getreg32(timerBase + STM32_GTIM_CCMR2_OFFSET));

  syslog(LOG_MDIAG,
    "\tCCER:\t0x%08x\tCNT:\t0x%08x\tPSC:\t0x%08x\tARR:\t0x%08x\n",
    getreg16(timerBase + STM32_GTIM_CCER_OFFSET),
    getreg32(timerBase + STM32_GTIM_CNT_OFFSET),
    getreg16(timerBase + STM32_GTIM_PSC_OFFSET),
    getreg32(timerBase + STM32_GTIM_ARR_OFFSET));

  syslog(LOG_MDIAG,
    "\tCCR1:\t0x%08x\tCCR2:\t0x%08x\tCCR3:\t0x%08x\tCCR4:\t0x%08x\n",
    getreg32(timerBase + STM32_GTIM_CCR1_OFFSET),
    getreg32(timerBase + STM32_GTIM_CCR2_OFFSET),
    getreg32(timerBase + STM32_GTIM_CCR3_OFFSET),
    getreg32(timerBase + STM32_GTIM_CCR4_OFFSET));

  syslog(LOG_MDIAG,
    "\tDCR:\t0x%08x\tDMAR:\t0x%08x\tOR:\t0x%08x\n",
    getreg16(timerBase + STM32_GTIM_DCR_OFFSET),
    getreg16(timerBase + STM32_GTIM_DMAR_OFFSET),
    getreg16(timerBase + STM32_GTIM_OR_OFFSET));
}
#endif
