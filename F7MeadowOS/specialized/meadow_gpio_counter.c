/****************************************************************************
 * F7MeadowOS/specialized/meadow_gpio_counter.c
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

// This module contains code to handle gpio counters, at the native level.
// There's a #define MEADOW_GPIOCNT_DELAY_LOWER_THRESHOLD_MS which determines
// how the code behaves. If the configured delay is at or below this level the
// current gpio counter values are returned immediately, even if there's
// been no change.
// If the configured delay is above this threshold, the values will be
// returned after the configured delay period, but only if there has been a
// change in the gpio counter values. If no change, the caller could wait
// forever.

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <nuttx/config.h>
#include <meadow/meadow_gpio_counter.h>

// If this code is never needed remove meadow_gpio_counter from
// nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/Make.defs
// Otherwise the meadow_gpio_counter.c file must exist.
#if(MEADOW_GPIO_COUNTER_BUILD > 0)

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <arch/board/board.h>
#include "stm32_gpio.h"
#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>

#if defined(CONFIG_GPIO_COUNTER_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
#pragma message "(--) meadow_gpio_counter.c"
#endif

// #define USE_MEADOW_DEBUG_HELPERS
#undef USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>

#pragma GCC optimize("O0")    // Prevent compiler from changing the code

/****************************************************************************
 * Private Data
 ****************************************************************************/
// All F7 possible input data register addresses, used for ISR access to read
// GPIO state value.
static uint32_t gpioCntInputDataReg[] = 
{
  STM32_GPIOA_IDR,
  STM32_GPIOB_IDR,
  STM32_GPIOC_IDR,
  STM32_GPIOD_IDR,
  STM32_GPIOE_IDR,
  STM32_GPIOF_IDR,
  STM32_GPIOG_IDR,
  STM32_GPIOH_IDR,
  STM32_GPIOI_IDR,
  STM32_GPIOJ_IDR,
  STM32_GPIOK_IDR,
};

// This is the list of the GPIOs currently configured.
static gpioCounterInfo_t * _allGpioCountList[MEADOW_GPIO_COUNT_MAX_ENCODERS]\
        = {NULL};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void meadow_gpio_counter_read_now(gpioCounterInfo_t *gpioCounterInfo,
          gpioCntReadData_t *gpioCntReadData);
static int gpio_cnt_config_interrupt_remove(uint32_t gpioCntrNumb);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

//===========================================================================
// GPIO counter input ISR
// Note: may need to insure that both 'A' and 'B' are changing every other
// call, Possibly use int isrACount and isrBCount and if difference > than
// some small number, warn user (e.g. "Only GPIO is changing").
static int meadow_gpio_count_isr(int irq, void *context, void *arg)
{
  uint8_t pinNumb;
  uint32_t idrRegister;
  gpioCounterInfo_t *gpioCounterInfo = (gpioCounterInfo_t *)arg;
   
  // Save previous B state
  // uint32_t newCondBits = (gpioCounterInfo->prevCondBits) & 0x02;

  // Find the current GPIO state 
  idrRegister = *((uint32_t *)(gpioCounterInfo->idrAddress));
  pinNumb = gpioCounterInfo->gpioPinInfo & 0x0f;

  bool gpioState = (idrRegister & (1 << pinNumb)) > 0 ? true : false;
  syslog(LOG_MDIAG, "gpioState:%d\n", gpioState);

  gpioCounterInfo->totalCount++;
  return OK;
}

//========================================================================
// Static - Register the caller as an active reader of gpioCntrNumb's info,
// guarding against a concurrent gpio_cnt_config_interrupt_remove(). The
// lookup and the increment happen in the same critical section as
// gpio_cnt_config_interrupt_remove()'s un-publish step, so the two can never
// interleave: either this sees the still-valid pointer and remove() is then
// blocked from freeing it, or remove() already ran and this sees NULL.
// Returns NULL if the gpio counter is not configured; caller must not call
// gpio_cnt_release() in that case.
gpioCounterInfo_t *gpio_cnt_acquire(uint32_t gpioCntrNumb)
{
  // irqstate_t flags = enter_critical_section();
  gpioCounterInfo_t *gpioCounterInfo = _allGpioCountList[gpioCntrNumb];
  // if(gpioCounterInfo != NULL)
  // {
  //   gpioCounterInfo->activeReaders++;
  // }
  // leave_critical_section(flags);
  return gpioCounterInfo;
}

//========================================================================
// Static - Unregister as an active reader. If gpio_cnt_config_interrupt_remove()
// ran while we were active it deferred the free() to whichever caller turns
// out to be last out - do it here if that's us.
void gpio_cnt_release(gpioCounterInfo_t *gpioCounterInfo)
{
  // irqstate_t flags = enter_critical_section();
  // gpioCounterInfo->activeReaders--;
  // bool shouldFree = (gpioCounterInfo->activeReaders == 0) &&
  //                    gpioCounterInfo->pendingFree;
  // leave_critical_section(flags);

  // if(shouldFree)
  // {
    kmm_free(gpioCounterInfo);
  // }
}

//========================================================================
// Static - Return data now. Caller must hold a reference via gpio_cnt_acquire().
void meadow_gpio_counter_read_now(gpioCounterInfo_t *gpioCounterInfo,
          gpioCntReadData_t *gpioCntReadData)
{
  // Get the current data values
  irqstate_t flags = enter_critical_section();
  gpioCntReadData->gpioCount = \
      gpioCounterInfo->totalCount / MEADOW_GPIO_COUNT_EDGES_PER_COUNT;

  // What was the overall direction CW or CCW?
  int countDiff = gpioCounterInfo->totalCount - gpioCounterInfo->prevCount;
 
  // How much did the count change?
  gpioCntReadData->gpioCountChange = \
    countDiff / MEADOW_GPIO_COUNT_EDGES_PER_COUNT;

  // Prep for next read
  gpioCounterInfo->prevCount = gpioCounterInfo->totalCount;
  leave_critical_section(flags);
  return;
}

//========================================================================
// Static - Remove an existing gpio counter entry
// Caller will need to handle GPIO configuration as needed.
int gpio_cnt_config_interrupt_remove(uint32_t gpioCntrNumb)
{
  int ret;
  uint8_t gpioPinInfo;
  gpioCounterInfo_t *gpioCounterInfo;
  bool shouldFreeNow = false;

  // Un-publish the slot and snapshot whether a reader is currently active,
  // atomically with gpio_cnt_acquire()'s lookup+increment (see there). This
  // also guards against two concurrent removes of the same gpio counter both
  // acting on it - the second one to run this critical section sees NULL.
  irqstate_t flags = enter_critical_section();
  gpioCounterInfo = _allGpioCountList[gpioCntrNumb];
  if(gpioCounterInfo != NULL)
  {
    _allGpioCountList[gpioCntrNumb] = NULL;
    // gpioCounterInfo->pendingFree = true;
    // shouldFreeNow = (gpioCounterInfo->activeReaders == 0);
  }
  leave_critical_section(flags);

  if(gpioCounterInfo == NULL)
  {
    syslog(LOG_ERR, "%s@%d-GPIOCnt %ld already removed\n",
              __FILE__, __LINE__, gpioCntrNumb);
    return -ENOTEMPTY;
  }

  // Get info from the configuration memory before possibly freeing it
  gpioPinInfo = gpioCounterInfo->gpioPinInfo;

  // Tell Nuttx to forget about these GPIO interrupts
  // This call will also disables the interrupts
  ret = stm32_gpiosetevent(gpioPinInfo, 0, 0, 0, NULL, NULL);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error removing gpio counter:%ld Pin A interrupt, ret:%d\n",
              __FILE__, __LINE__, gpioCntrNumb, ret);
  }

  if(shouldFreeNow)
  {
    kmm_free(gpioCounterInfo);
  }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// Called to configure/delete gpio counter
int meadow_gpio_counter_config(gpioCntConfigParms_t* cfg)
{
  int ret = OK;
  gpioCounterInfo_t *gpioCounterInfo;

  if(cfg == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Parameter cfg cannot be NULL\n",
              __FILE__, __LINE__);
    return -EINVAL;
  }

  // Is gpio counter number out of range?
  if(cfg->gpioCntrNumb > (MEADOW_GPIO_COUNT_MAX_ENCODERS - 1))
  {
    syslog(LOG_ERR, "%s@%d-GPIOCnt %lu not available. GPIOCnt range is 0 - %u.\n",
              __FILE__, __LINE__, cfg->gpioCntrNumb,
              (MEADOW_GPIO_COUNT_MAX_ENCODERS - 1));
    return -ERANGE;    // Out of Range
  }

  // Check if slot is in use
  gpioCounterInfo = _allGpioCountList[cfg->gpioCntrNumb];
  if(cfg->isAddGpioCounter)
  {
    // Add, but is slot empty?
    if(gpioCounterInfo != NULL)
    {
      syslog(LOG_ERR, "%s@%d-GPIOCnt %ld is in use\n",
                __FILE__, __LINE__, cfg->gpioCntrNumb);
      return -ENOTEMPTY;    // Not empty
    }
  }
  else
  {
    // Remove, but is there something to remove?
    if(gpioCounterInfo == NULL)
    {
      syslog(LOG_ERR, "%s@%d-GPIOCnt %ld already removed\n",
                __FILE__, __LINE__, cfg->gpioCntrNumb);
      return -ENOTEMPTY;    // Not empty
    }

    // Remove
    ret = gpio_cnt_config_interrupt_remove(cfg->gpioCntrNumb);
    return ret;
  }

  // Adding new Rotary GPIOCnt

  // Verify GPIO parameters are in range
  if(cfg->gpioPortInfo > MEADOW_GPIO_COUNTER_MAX_PORT ||
     cfg->gpioPinInfo > MEADOW_GPIO_COUNTER_MAX_PIN)
  {
    syslog(LOG_ERR, "%s@%d-GPIOCnt %lu, GPIO Port or PIN is out of range\n",
              __FILE__, __LINE__, cfg->gpioCntrNumb);
    return -ERANGE;    // Out of Range
  }

  gpioCounterInfo = kmm_zalloc(sizeof(gpioCounterInfo_t));
  if(gpioCounterInfo == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Memory allocation failed\n", __FILE__, __LINE__);
    return -ENOMEM;
  }

  // Add allocated memory to correct free array slot
  _allGpioCountList[cfg->gpioCntrNumb] = gpioCounterInfo;

  // Add configuration information to gpio counter information
  gpioCounterInfo->gpioCntrNumb = cfg->gpioCntrNumb;
  gpioCounterInfo->delayMs = cfg->delayMs;

  // The GPIO input is used to configure and remove interrupt handling
  uint8_t pinDesignationA = cfg->gpioPortInfo << 4 | cfg->gpioPinInfo;
  gpioCounterInfo->gpioPinInfo = pinDesignationA;

  // Find the correct static offset for both GPIOs
  gpioCounterInfo->idrAddress = gpioCntInputDataReg[cfg->gpioPortInfo];

  // cfgset contains 20-bits of data. It is required by the Nuttx stm32_gpiosetevent
  // function. If the 20 bits of data are not correct, this Nuttx function will
  // reconfigure the GPIO based on whatever the data is in cfgset.
  // See stm32_gpio.h for more information.
  // Inputs: MMUU .... ...X PPPP BBBB
  // MM = Mode for input (this is 00)
  // UU = pull up, pull down or float
  // X  = configure as EXTI interrupt. Event or ISR set by stm32_gpiosetevent
  // Setup the Nuttx cfgset for this point to be configured by Nuttx
  uint32_t cfgsetA = (pinDesignationA & 0x000000ff);   // Set Port and Pin and clear MM

  switch(cfg->resistorMode)
  {
    case 0: // Float
      cfgsetA |= GPIO_FLOAT;
      break;
    case 1: // Pull up
      cfgsetA |= GPIO_PULLUP;
      break;
    case 2: // Pull down
      cfgsetA |= GPIO_PULLDOWN;
      break;
  }

  // Setup both rising and falling input points to trigger isr
  // This call also enables the interrupts
  ret = stm32_gpiosetevent(
    cfgsetA,                      // Nuttx cfgset
    1,                            // rising edge,
    1,                            // falling edge,
    0,                            // event
    meadow_gpio_count_isr,        // ISR
    gpioCounterInfo);           // information address
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error gpio counter:%ld Pin A interrupt, ret:%d\n",
              __FILE__, __LINE__, cfg->gpioCntrNumb, ret);
  }

  return ret;
}

//========================================================================
// Public - Allow the user to set or clear the current gpio counter count.
int meadow_gpio_counter_set_count(uint32_t gpioCntrNumb, int32_t newCount)
{
  // Check for valid gpio counter number (0-7)
  if(gpioCntrNumb > (MEADOW_GPIO_COUNT_MAX_ENCODERS - 1))
  {
    syslog(LOG_ERR, "%s@%d-GPIOCnt %lu not available. GPIOCnt range is 0 - %u.\n",
              __FILE__, __LINE__, gpioCntrNumb,
              (MEADOW_GPIO_COUNT_MAX_ENCODERS - 1));
    return -ERANGE;    // Out of Range
  }

  gpioCounterInfo_t *gpioCounterInfo = gpio_cnt_acquire(gpioCntrNumb);

  // Is requested slot valid?
  if(gpioCounterInfo == NULL)
  {
    syslog(LOG_ERR, "%s@%d-GPIOCnt %ld is not configured\n",
              __FILE__, __LINE__, gpioCntrNumb);
    return -ENODATA;    // No Data
  }

  irqstate_t flags = enter_critical_section();
  gpioCounterInfo->totalCount = newCount;
  leave_critical_section(flags);

  gpio_cnt_release(gpioCounterInfo);
  return OK;
}

//========================================================================
// Public - Read gpio counter information.
// 
// A configured delay less or equal to MEADOW_GPIOCNT_DELAY_LOWER_THRESHOLD_MS
// gpio counter data is return immediately.
// If the configured delay is greater than
// MEADOW_GPIOCNT_DELAY_LOWER_THRESHOLD_MS the calling thread waits for the
// specified delay period. After that period, if the count has changed, the
// calling thread returns with gpio counter data. If the count has not changed the
// calling thread will continue to wait, only returning when there is a
// change.
int meadow_gpio_counter_read_data(gpioCntReadData_t *gpioCntReadData)
{
  gpioCounterInfo_t *gpioCounterInfo;
  int ret = OK;

  // Check for NULL
  if(gpioCntReadData == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Input parameter cannot be NULL\n",
              __FILE__, __LINE__);
    return -EINVAL;
  }

  // Check for valid gpio counter number (0-7)
  if(gpioCntReadData->gpioCntrNumb > (MEADOW_GPIO_COUNT_MAX_ENCODERS - 1))
  {
    syslog(LOG_ERR, "%s@%d-GPIOCnt %lu not available. GPIOCnt range is 0 - %u.\n",
              __FILE__, __LINE__, gpioCntReadData->gpioCntrNumb,
              (MEADOW_GPIO_COUNT_MAX_ENCODERS - 1));
    return -ERANGE;    // Out of Range
  }

  gpioCounterInfo = gpio_cnt_acquire(gpioCntReadData->gpioCntrNumb);

  // Is requested slot configured?
  if(gpioCounterInfo == NULL)
  {
    syslog(LOG_ERR, "%s@%d-GPIOCnt %ld is not configured\n",
              __FILE__, __LINE__, gpioCntReadData->gpioCntrNumb);
    return -ENODATA;    // No Data
  }

  // Check if we should wait or immediately read the gpio counter's values
  if(gpioCounterInfo->delayMs <= MEADOW_GPIOCNT_DELAY_LOWER_THRESHOLD_MS)
  {
    // Get info and return since there's no delay requested.
    meadow_gpio_counter_read_now(gpioCounterInfo, gpioCntReadData);
  }
  else
  {
    // This simple polling loop does the waiting. abs() is used so a change
    // in either direction (CW or CCW) satisfies the wait.
    while(abs(gpioCounterInfo->totalCount - gpioCounterInfo->prevCount) < \
      MEADOW_GPIO_COUNT_EDGES_PER_COUNT)
    {
      usleep(gpioCounterInfo->delayMs * 1000);
    }

    if(ret == OK)
    {
      meadow_gpio_counter_read_now(gpioCounterInfo, gpioCntReadData);
    }
  }

  gpio_cnt_release(gpioCounterInfo);
  return ret;
}

#endif      // defined(MEADOW_GPIO_COUNTER_BUILD)
