/****************************************************************************
 * Meadow.OS/include/meadow/meadow_rotary_encoder.h
 * 
 *   Copyright (C) 2024-2025 Wilderness Labs. All rights reserved.
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
#ifndef __CONFIGS_MEADOW_SRC_MEADOW_ROTENC__H
#define __CONFIGS_MEADOW_SRC_MEADOW_ROTENC__H

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
// Should rotary encoder code be built? The feature is shipped (has a live
// dispatched kernel test); this flag exists only for potential future
// exclusion, not a pending decision.
#define MEADOW_ROTARY_ENCODER_BUILD     (1)

// Define the maximum number of encoders that can be monitored.
// Since the F7, under Nuttx, only has 16 "interrupt groups" there's no
// point in having more that 8 rotary encoders.
#define MEADOW_ROTARY_ENC_MAX_ENCODERS  (8)

#define MEADOW_ROTARY_ENC_MAX_PORT      (10)   // A-K
#define MEADOW_ROTARY_ENC_MAX_PIN       (15)    // 0-15

#define MEADOW_ROTARY_ENC_DIRECTION_CW  (0)
#define MEADOW_ROTARY_ENC_DIRECTION_CCW (1)

#if (MEADOW_ROTARY_ENCODER_BUILD > 0)

// The Rotary Encoder Count consists of 2 GPIOs (A and B) and both
// rising and falling edges of each pulse equals one encoder count.
#define MEADOW_ROTENC_GPIO_EDGES_PER_COUNT (4)

// Any delay below this value will be considered a delay of 0.
#define MEADOW_ROTENC_DELAY_LOWER_THRESHOLD_MS (0)

// Caller must populate this struct before sending it
struct rotEncConfigParms_s
{
  uint32_t encoderNumb;       // 0 - 7 to identify for collections
  bool isAddEncoder;          // true=add, false=remove
  uint32_t portA;             // 0 - 10 (A-K)
  uint32_t pinA;              // 0 - 15
  uint32_t portB;             // 0 - 10 (A-K)
  uint32_t pinB;              // 0 - 15
  uint32_t resistorMode;      // 0 = float, 1 = pull up, 2 = pull down
  uint32_t delayMs;           // Requested delay in milliseconds
};
typedef struct rotEncConfigParms_s rotEncConfigParms_t;

// This structure needs the encoderNumber set to 0-7 for the encoder.
// The meadow_rotary_encoder_read_data() function fills the other fields.
struct rotEncReadData_s
{
  uint32_t encoderNumb;     // Supplied, 0 - 7 encoder config number
  int32_t encoderCount;     // Returned, Current total count (+/-)
  int32_t encoderChange;    // Returned, Counts since last read (+/-)
  uint32_t rotClockWise;    // Returned, Current A/B relation, 0 = CW
};
typedef struct rotEncReadData_s rotEncReadData_t;

// Internal structure to manage rotary encoders
struct rotaryEncoderInfo_s
{
  uint8_t encoderNumb;        // Encoder number

  // Represents the CPU PinPort identifier (e.g. PD9, D=3 so 39)
  uint8_t pinInfoA;           // Supplied by configuration
  uint8_t pinInfoB;           // Supplied by configuration

  // Address of correct "Input Data Register" which holds GPIO's hardware
  // port state bits
  volatile uint32_t *idrAddressA;    // Calculated during configuration
  volatile uint32_t *idrAddressB;    // Calculated during configuration

  uint32_t prevCondBits;      // Current state information
  uint32_t lookupOff;         // Current lookup table offset
  // Using int32_t for count as this will roll-over in 24,855 days at 1 count
  // per second. or 24.855 days at 1000 counts per second. Could be int64_t.
  int32_t totalCount;         // Total of A and B, rising and/or falling edges
  int32_t prevCount;          // Count at last read, used to calc cnt change
  uint32_t rotClockWise;      // Current direction 0 = Clockwise
  uint32_t delayMs;           // Delay in milliseconds

  // Guards against rotenc_config_interrupt_remove() freeing this struct
  // while a read (or set_count) call is still using it. A caller registers
  // by incrementing activeReaders before use and decrementing after; remove()
  // sets pendingFree and only frees immediately if no reader is registered,
  // otherwise the last reader to unregister does the free. See
  // rotenc_acquire()/rotenc_release() in meadow_rotary_encoder.c.
  uint16_t activeReaders;     // Count of in-flight callers referencing this struct
  bool pendingFree;           // remove() requested while a reader was active
};
typedef struct rotaryEncoderInfo_s rotaryEncoderInfo_t;

// Public functions with syscalls
int meadow_rotary_encoder_config(rotEncConfigParms_t *cfg);
int meadow_rotary_encoder_set_count(uint32_t encoderNumb, int32_t newCount);
int meadow_rotary_encoder_read_data(rotEncReadData_t *rotEncReadData);

// Test function follow
void rotary_encoder_test_exercise_test(uint32_t userData);

#endif      // #if (MEADOW_ROTARY_ENCODER_BUILD > 0)

#endif      // __CONFIGS_MEADOW_SRC_MEADOW_ROTENC__H