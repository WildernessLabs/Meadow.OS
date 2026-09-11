/****************************************************************************
 * Meadow.OS/specialized/meadow_measure_freq_local.h
 * 
 *   Copyright (C) 2024 Wilderness Labs. All rights reserved.
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
#ifndef __CONFIGS_MEADOW_SPEC_MEADOW_FREQ_LOCAL__H
#define __CONFIGS_MEADOW_SPEC_MEADOW_FREQ_LOCAL__H

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <meadow/hcom_shared_common.h>

#include <nuttx/config.h>
#include <string.h>
#include <stdint.h>
#include "stm32_gpio.h"   // stm32_configgpio

// Since the interrupts are based on edges, the only reason for slowing the
// clock would be to slow down the number of overflows for a 16-bit timer
// (see comments in ISR).
// The clock speed can only be even multiples of the system clock.
// See board.h for details.
// For current application reduce clock speed by ((216/2)/100) = 1.08MHz
#define MEADOW_FREQ_CLOCK_FREQ ((STM32_SYSCLK_FREQUENCY/2)/100)

//=====================================================
#define MEADOW_FREQ_MAX_TIMER_CHANNELS     (4)
#define MEADOW_FREQ_TIMER_WIDTH_16         (0)
#define MEADOW_FREQ_TIMER_WIDTH_32         (1)
#define MEADOW_FREQ_16_BIT_OVERFLOW_COUNT  (65536)
#define MEADOW_FREQ_32_BIT_OVERFLOW_COUNT  (4294967296)

// To configure a GPIO as an input to a timer it, needs to contain how it's
// to be used (floating input), plus Pin and Port and the Timer defined
// alternate function value plus the Nuttx GPIO_ALT value.
#define MEADOW_TIMER_GPIO_CONST (GPIO_ALT | GPIO_INPUT | GPIO_FLOAT)

//=====================================================
// Preprocessor defines
#define MEADOW_MEASURE_FREQ_INCLUDE_REG_DUMP (0)

// Adds syslog diagnostic output
#define MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT (0)

// Diagnostic
#define MEADOW_MEASURE_FREQ_INCLUDE_DIAG_ISR_TIME (0)

#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_ISR_TIME > 0)
// Use pin for timing via scope
#define DEBUG_GPIO_V2_D06 (GPIO_OUTPUT | GPIO_FLOAT | GPIO_PUSHPULL | \
  GPIO_SPEED_100MHz | GPIO_PORTB | GPIO_PIN13)
#endif

#define MEADOW_MEAS_FREQ_NUMB_OF_F7_TIMERS (14)

// There are up to 10 entries for each timer
#define MEADOW_MEAS_FREQ_MAX_CCM_ENTRIES_PER_TIMER (10)

// Each Feather timer entry can have as many as 4 entries that takes 1 byte.
#define MEADOW_MEAS_FREQ_MAX_F7_BYTES_PER_TIMER (4)

#if(MEADOW_MEASURE_FREQ_INCLUDE_DIAG_OUTPUT > 0)
  // Need 2 bytes for each CCM entry because channel number is defined in this
  // as well as the port and pin pair
  #define MEADOW_MEAS_FREQ_MAX_CCM_BYTES_PER_TIMER \
    (MEADOW_MEAS_FREQ_MAX_CCM_ENTRIES_PER_TIMER * 2)
#endif

//--------------------------------------------------------------------------
// This structure contains runtime data
struct mdwFreqChanData_s
{
  // Needed for duty cycle
  uint32_t midCaptureCnt;
  uint32_t midCaptureOvr;

  // Used to calculate frequency etc.
  uint32_t bgnResultCnt;
  uint32_t bgnResultOvr;
  uint32_t midResultCnt;
  uint32_t midResultOvr;
  uint32_t endResultCnt;
  uint32_t endResultOvr;

  bool useDutyCycle;            // Does channel need duty cycle?
  uint32_t inputConfig;         // Nuttx GPIO config for unconfig
  uint64_t gpioCountForAvg;     // GPIO input count for average
  uint64_t startTimeForAvg;     // Meadow start time for average
};
typedef struct mdwFreqChanData_s mdwFreqChanData_t;

#define ACTIVE_CHAN_BITFIELD_1 (0b00000001)
#define ACTIVE_CHAN_BITFIELD_2 (0b00000010)
#define ACTIVE_CHAN_BITFIELD_3 (0b00000100)
#define ACTIVE_CHAN_BITFIELD_4 (0b00001000)

// Offsets for channels in the mdwFreqChanData field
#define FREQ_CHANNEL_NUMBER_CHAN_1 (1)
#define FREQ_CHANNEL_NUMBER_CHAN_2 (2)
#define FREQ_CHANNEL_NUMBER_CHAN_3 (3)
#define FREQ_CHANNEL_NUMBER_CHAN_4 (4)

// Offsets for channels in the mdwFreqChanData field
#define FREQ_CHAN_DATA_OFFSET_CHAN_1 (0)
#define FREQ_CHAN_DATA_OFFSET_CHAN_2 (1)
#define FREQ_CHAN_DATA_OFFSET_CHAN_3 (2)
#define FREQ_CHAN_DATA_OFFSET_CHAN_4 (3)

// The 'freqTimerInfo_s' contains information that defines the selected
// timer's F7's internal hardware capabilities. Except for the 'freqDcRtData'
// element, each field is pre-defined from the 'struct freqTimerInfo_s array'
struct mdwFreqTimerInfo_s
{
  uint8_t  timerNumb   : 4; // ( # ) 1 - 14 timer number
  uint8_t  timerWidth  : 1; // (32b) 16-bit or 32-bit timer? 0=16-bits, 1=32-bits
  uint8_t  timerMaxClk : 1; // (216) 0=STM32_APB1_TIM2_CLKIN, 1=STM32_APB2_TIM1_CLKIN
  uint8_t  timerAPBClk : 1; // (apb) 0=STM32_RCC_APB1ENR, 1=STM32_RCC_APB2ENR
  uint8_t  timerUsable : 1; // (use) Is Timer useable? 0=No, 1=Yes
  uint32_t timerBase;       // Unique base address for each timer
  uint32_t timerClkEn;      // Offset for timer enable bit for APB1 or APB2
  uint32_t timerIrqVec;     // Timer's Interrupt vector
  uint16_t timerAltFunc;    // Timer's GPIO alternate function
  uint64_t timerOverflow;   // Each CNT overflow, increment
  uint8_t  chanActiveBits;  // Channel active? (bit 0=chan1, bit 1=chan2....)
  mdwFreqChanData_t *mdwFreqChanData[4]; // One for each input channel
};
typedef struct mdwFreqTimerInfo_s mdwFreqTimerInfo_t;

struct mdwFreqChanPortPin_s
{
  uint8_t portPin;
  uint8_t chan;
};
typedef struct mdwFreqChanPortPin_s mdwFreqChanPortPin_t;

// This array contains timer information most of which is fixed by the
// STM32F7's hardware. It contains each F7 timer and a flag for useability
// (TIM1, TIM6, TIM7 and TIM8 are not usable) and some timer numbers don't
// exist in the STM32F777IIK6 we use.
// See 'struct mdwFreqTimerInfo_s' for exact field usage.
static mdwFreqTimerInfo_t mdwFreqTimerInfoArray[] =
{
            //   |--- bit-field---|---------------------- Fixed by hardware -----------------------|--------- Runtime Data ---------|
            //   #  32b 216 apb use    Base Addr       Clk Timer Enable      IRQ Vector*   Alt Func OvF Acv  Chan1 Chan2 Chan3 Chan4
  /* TIM1   */  {1 , 0,  1,  1, 0, STM32_TIM1_BASE,  RCC_APB2ENR_TIM1EN,  0,               GPIO_AF1, 0,  0, {NULL, NULL, NULL, NULL}},
  /* TIM2   */  {2 , 1,  0,  0, 1, STM32_TIM2_BASE,  RCC_APB1ENR_TIM2EN,  STM32_IRQ_TIM2,  GPIO_AF1, 0,  0, {NULL, NULL, NULL, NULL}},
  /* TIM3   */  {3 , 0,  0,  0, 1, STM32_TIM3_BASE,  RCC_APB1ENR_TIM3EN,  STM32_IRQ_TIM3,  GPIO_AF2, 0,  0, {NULL, NULL, NULL, NULL}},
  /* TIM4   */  {4 , 0,  0,  0, 1, STM32_TIM4_BASE,  RCC_APB1ENR_TIM4EN,  STM32_IRQ_TIM4,  GPIO_AF2, 0,  0, {NULL, NULL, NULL, NULL}},
  /* TIM5   */  {5 , 1,  0,  0, 1, STM32_TIM5_BASE,  RCC_APB1ENR_TIM5EN,  STM32_IRQ_TIM5,  GPIO_AF2, 0,  0, {NULL, NULL, NULL, NULL}},
  /* TIM6   */  {6 , 0,  0,  0, 0, STM32_TIM6_BASE,  RCC_APB1ENR_TIM6EN,  STM32_IRQ_TIM6,  0,        0,  0, {NULL, NULL, NULL, NULL}},
  /* TIM7   */  {7 , 0,  0,  0, 0, STM32_TIM7_BASE,  RCC_APB1ENR_TIM7EN,  STM32_IRQ_TIM7,  0,        0,  0, {NULL, NULL, NULL, NULL}},
  /* TIM8   */  {8 , 0,  1,  1, 0, STM32_TIM8_BASE,  RCC_APB2ENR_TIM8EN,  0,               GPIO_AF3, 0,  0, {NULL, NULL, NULL, NULL}},
  /* TIM9   */  {9 , 0,  1,  1, 1, STM32_TIM9_BASE,  RCC_APB2ENR_TIM9EN,  STM32_IRQ_TIM9,  GPIO_AF3, 0,  0, {NULL, NULL, NULL, NULL}},
  /* TIM10  */  {10, 0,  1,  1, 1, STM32_TIM10_BASE, RCC_APB2ENR_TIM10EN, STM32_IRQ_TIM10, GPIO_AF3, 0,  0, {NULL, NULL, NULL, NULL}},
  /* TIM11  */  {11, 0,  1,  1, 1, STM32_TIM11_BASE, RCC_APB2ENR_TIM11EN, STM32_IRQ_TIM11, GPIO_AF3, 0,  0, {NULL, NULL, NULL, NULL}},
  /* TIM12  */  {12, 0,  0,  0, 1, STM32_TIM12_BASE, RCC_APB1ENR_TIM12EN, STM32_IRQ_TIM12, GPIO_AF9, 0,  0, {NULL, NULL, NULL, NULL}},
  /* TIM13  */  {13, 0,  0,  0, 1, STM32_TIM13_BASE, RCC_APB1ENR_TIM13EN, STM32_IRQ_TIM13, GPIO_AF9, 0,  0, {NULL, NULL, NULL, NULL}},
  /* TIM14  */  {14, 0,  0,  0, 1, STM32_TIM14_BASE, RCC_APB1ENR_TIM14EN, STM32_IRQ_TIM14, GPIO_AF9, 0,  0, {NULL, NULL, NULL, NULL}}
};
#define MEADOW_FREQ_TOTAL_TIMERS_AVAILABLE (sizeof(mdwFreqTimerInfoArray)/sizeof(mdwFreqTimerInfo_t))

// Unused Timers TIM1, TIM6, TIM7 and TIM8
// TIM1 and TIM8 are "Advanced-control timers" For example, instead of 1 IRQ
//  for the timer, these have 4 IRQs timer. This alone makes them incompatible
//  with this implementation.
// TIM6 and TIM7 are called Basic Timers and have no ability to connect to a
//  GPIO.

//----------------------------------------------------------------------------
// This table contains all of the STM32F7's timers and their valid GPIOs with
// channel. It is used for F7v1 and F7v2 and CCM. It should be able to verify
// any timer, GPIO combination.
static mdwFreqChanPortPin_t validStm32F7GpioArray[][MEADOW_MEAS_FREQ_MAX_CCM_ENTRIES_PER_TIMER] =
{
  /* Tim 1 */
  {
    {GPIO_PORTA | GPIO_PIN8,  1},   // PA8
    {GPIO_PORTE | GPIO_PIN9,  1},   // PE9
    {GPIO_PORTA | GPIO_PIN9,  2},   // PA9
    {GPIO_PORTE | GPIO_PIN11, 2},   // PE11
    {GPIO_PORTA | GPIO_PIN10, 3},   // PA10
    {GPIO_PORTE | GPIO_PIN13, 3},   // PE13
    {GPIO_PORTA | GPIO_PIN11, 4},   // PA11
    {GPIO_PORTE | GPIO_PIN14, 4},   // PE14
  },

  /* Tim 2 */
  {
    {GPIO_PORTA | GPIO_PIN0,  1},   // PA0
    {GPIO_PORTA | GPIO_PIN15, 1},   // PA15
    {GPIO_PORTA | GPIO_PIN1,  2},   // PA1
    {GPIO_PORTB | GPIO_PIN3,  2},   // PB3
    {GPIO_PORTA | GPIO_PIN2,  3},   // PA2
    {GPIO_PORTB | GPIO_PIN10, 3},   // PB10
    {GPIO_PORTA | GPIO_PIN3,  4},   // PA3
    {GPIO_PORTB | GPIO_PIN11, 4},   // PB11
  },

  /* Tim 3 */
  {
    {GPIO_PORTA | GPIO_PIN6, 1},    // PA6
    {GPIO_PORTC | GPIO_PIN6, 1},    // PC6
    {GPIO_PORTB | GPIO_PIN4, 1},    // PB4
    {GPIO_PORTA | GPIO_PIN7, 2},    // PA7
    {GPIO_PORTC | GPIO_PIN7, 2},    // PC7
    {GPIO_PORTB | GPIO_PIN5, 2},    // PB5
    {GPIO_PORTB | GPIO_PIN0, 3},    // PB0
    {GPIO_PORTC | GPIO_PIN8, 3},    // PC8
    {GPIO_PORTB | GPIO_PIN1, 4},    // PB1
    {GPIO_PORTC | GPIO_PIN9, 4},    // PC9
  },
  
  /* Tim 4 */
  {
    {GPIO_PORTD | GPIO_PIN12, 1},    // PD12
    {GPIO_PORTB | GPIO_PIN6,  1},    // PB6
    {GPIO_PORTD | GPIO_PIN13, 2},    // PD13
    {GPIO_PORTB | GPIO_PIN7,  2},    // PB7
    {GPIO_PORTD | GPIO_PIN14, 3},    // PD14
    {GPIO_PORTB | GPIO_PIN8,  3},    // PB8
    {GPIO_PORTD | GPIO_PIN15, 4},    // PD15
    {GPIO_PORTB | GPIO_PIN9,  4},    // PB9
  },

  /* Tim 5 */
  {
    {GPIO_PORTA | GPIO_PIN0,  1},    // PA0
    {GPIO_PORTH | GPIO_PIN10, 1},    // PH10
    {GPIO_PORTA | GPIO_PIN1,  2},    // PA1
    {GPIO_PORTH | GPIO_PIN11, 2},    // PH11
    {GPIO_PORTA | GPIO_PIN2,  3},    // PA2
    {GPIO_PORTH | GPIO_PIN12, 3},    // PH12
    {GPIO_PORTA | GPIO_PIN3,  4},    // PA3
    {GPIO_PORTI | GPIO_PIN0,  4},    // PI0
  },

  /* Tim 6 No GPIOs */
    {},

  /* Tim 7 No GPIOs */
    {},

  /* Tim 8 */
  {
    {GPIO_PORTC | GPIO_PIN6, 1},    // PC6
    {GPIO_PORTI | GPIO_PIN5, 1},    // PI5
    {GPIO_PORTC | GPIO_PIN7, 2},    // PC7
    {GPIO_PORTI | GPIO_PIN6, 2},    // PI6
    {GPIO_PORTC | GPIO_PIN8, 3},    // PC8
    {GPIO_PORTI | GPIO_PIN7, 3},    // PI7
    {GPIO_PORTC | GPIO_PIN9, 4},    // PC9
    {GPIO_PORTI | GPIO_PIN2, 4},    // PI2
  },

  /* Tim 9 */
  {
    {GPIO_PORTE | GPIO_PIN5, 1},    // PE5
    {GPIO_PORTA | GPIO_PIN2, 1},    // PA2
    {GPIO_PORTE | GPIO_PIN6, 2},    // PE6
    {GPIO_PORTA | GPIO_PIN3, 2},    // PA3
  },

  /* Tim 10 */
  {
    {GPIO_PORTF | GPIO_PIN6, 1},    // PF6
    {GPIO_PORTB | GPIO_PIN8, 1},    // PB8
  },

  /* Tim 11 */
  {
    {GPIO_PORTF | GPIO_PIN7, 1},    // PF7
    {GPIO_PORTB | GPIO_PIN9, 1},    // PB9
  },

  /* Tim 12 */
  {
    {GPIO_PORTH | GPIO_PIN6,  1},    // PH6
    {GPIO_PORTB | GPIO_PIN14, 1},    // PB14
    {GPIO_PORTH | GPIO_PIN9,  2},    // PH9
    {GPIO_PORTB | GPIO_PIN15, 2},    // PB15
  },

  /* Tim 13 */
  {
    {GPIO_PORTF | GPIO_PIN8, 1},    // PF8
    {GPIO_PORTA | GPIO_PIN6, 1},    // PA6
  },

  /* Tim 14 */
  {
    {GPIO_PORTF | GPIO_PIN9, 1},    // PF9
    {GPIO_PORTA | GPIO_PIN7, 1},    // PA7
  },
};

//-------------------------------------------------
// Used to verify port and pin availability on F7FeatherV1
static uint8_t validF7v1GpioArray[][MEADOW_MEAS_FREQ_MAX_F7_BYTES_PER_TIMER] =
{
  /* TIM1 No GPIO exposed */
  {},

  /* TIM2  No GPIO exposed */
  {},
  
  /* TIM3  */
  {GPIO_PORTC | GPIO_PIN6,  // PC6, D02
   GPIO_PORTC | GPIO_PIN7,  // PC7, D05
   GPIO_PORTB | GPIO_PIN0,  // PB0, D06
   GPIO_PORTB | GPIO_PIN1,  // PB1, D09 (or PC9, D11)
  },

  /* TIM4 */
  {GPIO_PORTB | GPIO_PIN6,  // PB6, D08
   GPIO_PORTB | GPIO_PIN7,  // PB7, D07
   GPIO_PORTB | GPIO_PIN8,  // PB8, D03
   GPIO_PORTB | GPIO_PIN9,  // PB9, D04
  },

  // /* TIM5 */
  {GPIO_PORTH | GPIO_PIN10,  // PH10, D10
  },

  /* TIM6  No GPIO exposed */
  {},
  /* TIM7  No GPIO exposed */
  {},
  /* TIM8  No GPIO exposed */
  {},

  /* TIM9  */
  {GPIO_PORTA | GPIO_PIN3,  // PA3, A02
  },

  /* TIM10 */
  {GPIO_PORTB | GPIO_PIN8,  // PB8, D03
  },

  /* TIM11 */
  {GPIO_PORTB | GPIO_PIN9,  // PB9, D04
  },

  /* TIM12 */
  {GPIO_PORTB | GPIO_PIN14,  // PB14, D12
   GPIO_PORTB | GPIO_PIN15,  // PB15, D13
  },

  /* TIM13  No GPIO exposed */
  {},

  /* TIM14 */
  {GPIO_PORTA | GPIO_PIN7,  // PA7, A03
  },
};

//-------------------------------------------------
// Used to verify port and pin availability on F7FeatherV2
static uint8_t validF7v2GpioArray[][MEADOW_MEAS_FREQ_MAX_F7_BYTES_PER_TIMER]=
{
  /* TIM1 No GPIO exposed */
  {},

  /* TIM2 No GPIO exposed */
  {},

  /* TIM3 */
  {GPIO_PORTB | GPIO_PIN4,  // PB4, D05
   GPIO_PORTC | GPIO_PIN7,  // PC7, D10
   GPIO_PORTB | GPIO_PIN0,  // PB0, A03
   GPIO_PORTB | GPIO_PIN1,  // PB1, A04
  },

  /* TIM4 D08, D07, D03*, D04* */
  /* TIM4 */
  {GPIO_PORTB | GPIO_PIN6,  // PB6, D08
   GPIO_PORTB | GPIO_PIN7,  // PB7, D07
   GPIO_PORTB | GPIO_PIN8,  // PB8, D03
   GPIO_PORTB | GPIO_PIN9,  // PB9, D04
  },

  /* TIM5 */
  {GPIO_PORTH | GPIO_PIN10,  // PH10, D02
   GPIO_PORTA | GPIO_PIN3,   // PA3, A02
  },

  /* TIM6 No GPIO exposed */
  {},
  /* TIM7 No GPIO exposed */
  {},
  /* TIM8 No GPIO exposed */
  {},
  /* TIM9 No GPIO exposed */
  {},

  /* TIM10 */
  {GPIO_PORTB | GPIO_PIN8,  // PB8, D03
  },

  /* TIM11 */
  {GPIO_PORTB | GPIO_PIN9,  // PB9, D04
  },

  /* TIM12 */
  {GPIO_PORTB | GPIO_PIN14,  // PB14, D12
   GPIO_PORTB | GPIO_PIN15,  // PB15, D13
  },

  /* TIM13 No GPIO exposed */
  {},

  /* TIM14 No GPIO exposed */
  {},
};

#endif      // __CONFIGS_MEADOW_SPEC_MEADOW_FREQ_LOCAL__H
