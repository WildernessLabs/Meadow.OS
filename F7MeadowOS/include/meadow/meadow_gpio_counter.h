/****************************************************************************
 * F7MeadowOS/include/meadow/meadow_gpio_counter.h
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
#ifndef __CONFIGS_MEADOW_SRC_GPIO_COUNTER__H
#define __CONFIGS_MEADOW_SRC_GPIO_COUNTER__H

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <nuttx/config.h>

#include <string.h>
#include <stdint.h>
#include "hcom_shared_common.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/
// Should gpio counter code be built? This is a temporary #define.
// Remove #define when final decision made
#define MEADOW_GPIO_COUNTER_BUILD         (1)

// Define the maximum number of GPIOs that can be monitored.
// Since the F7, under Nuttx, only has 16 "interrupt groups" there's no
// point in having more that 8 gpio counters.
#define MEADOW_GPIO_COUNT_MAX_MONITORED   (8)

#define MEADOW_GPIO_COUNTER_MAX_PORT      (10)   // A-K
#define MEADOW_GPIO_COUNTER_MAX_PIN       (15)   // 0-15

#if (MEADOW_GPIO_COUNTER_BUILD > 0)

// Caller must populate this struct before configuring a GPIO
struct gpioCntConfigParms_s
{
  uint32_t gpioCntrNumb;      // 0 - 7 to identify for collections
  bool isAddGpioCounter;      // true=add, false=remove
  uint32_t gpioPortInfo;      // 0 - 10 (A-K)
  uint32_t gpioPinInfo;       // 0 - 15
  uint32_t resistorMode;      // 0 = float, 1 = pull up, 2 = pull down
};
typedef struct gpioCntConfigParms_s gpioCntConfigParms_t;

// This structure needs the counter number set to 0-7.
// The meadow_gpio_counter_read_data() function fills the other fields.
struct gpioCntReadData_s
{
  uint32_t gpioCntrNumb;    // Supplied, 0 - 7 gpio counter config number
  uint64_t gpioCount;       // Returned, Current total count
  uint64_t gpioCountChange; // Returned, Counts since last read
};
typedef struct gpioCntReadData_s gpioCntReadData_t;

// Internal structure to manage gpio counters
struct gpioCounterInfo_s
{
  uint8_t gpioCntrNumb;       // GPIOCntr number

  // Represents the CPU PinPort identifier (e.g. PD9, D=3 so 39)
  uint8_t gpioPinInfo;        // Supplied by configuration

  // Using uint64_t for count since this only counts up (rising edges); at
  // 1000 counts per second this will not roll over for well over a million
  // years, so no reasonable application will ever see it wrap.
  uint64_t totalCount;        // Total of rising edges
  uint64_t prevCount;         // Count at last read
};
typedef struct gpioCounterInfo_s gpioCounterInfo_t;

// Public functions with syscalls (yet to be added)
int meadow_gpio_counter_config(gpioCntConfigParms_t *cfg);
int meadow_gpio_counter_set_count(uint32_t gpioCntrNumb, uint64_t newCount);
int meadow_gpio_counter_read_data(gpioCntReadData_t *gpioCntReadData);

// Test function follow
void gpio_counter_test_exercise_test(uint32_t userData);

#endif      // #if (MEADOW_GPIO_COUNTER_BUILD > 0)

#endif      // __CONFIGS_MEADOW_SRC_GPIO_COUNTER__H