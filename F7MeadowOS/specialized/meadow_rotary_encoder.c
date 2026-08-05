/****************************************************************************
 * F7MeadowOS/specialized/meadow_rotary_encoder.c
 * 
 *   Copyright (C) 2024-2026 Wilderness Labs. All rights reserved.
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

// This module contains code to handle rotary encoders, at the native level.
// There's a #define MEADOW_ROTENC_DELAY_LOWER_THRESHOLD_MS which determines
// how the code behaves. If the configured delay is at or below this level the
// current rotary encoder values are returned immediately, even if there's
// been no change.
// If the configured delay is above this threshold, the values will be
// returned after the configured delay period, but only if there has been a
// change in the rotary encoder values. If no change, the caller could wait
// forever.

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <nuttx/config.h>
#include <meadow/meadow_rotary_encoder.h>

// If this code is never needed remove meadow_rotary_encoder from
// nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/Make.defs
// Otherwise the meadow_rotary_encoder.c file must exist.
#if(MEADOW_ROTARY_ENCODER_BUILD > 0)

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <arch/board/board.h>
#include "stm32_gpio.h"
#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>

#if defined(CONFIG_ROTARY_ENCODER_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
#pragma message "(--) meadow_rotary_encoder.c"
#endif

// #define USE_MEADOW_DEBUG_HELPERS
#undef USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>

// #pragma GCC optimize("O0")    // Prevent compiler from changing the code

/****************************************************************************
 * Private Data
 ****************************************************************************/
// A rotary encoder has 2 digital outputs, called A and B. Therefore, 2 GPIO
// input are needed. The way a rotary encoder is designed, when rotated it
// toggles output A and output B 90 degrees out of phase. If A toggles
// followed by B, it indicates clock-wise rotation, if B then A, counter
// clock-wise. This is how rotational direction is determined and whether
// the count is incremented or decremented.
// For each transition we must consider the previous and current state of
// A and B. This past and present state is represented by a 4-bit number.
// Bits 0 and 1 represent the current state of A and B and bits 2 and 3
// represent previous states of A and B.
// |old|new|
// |A|B|A|B|
//  3 2 1 0
//
// This 4-bit number creates 16 possible values (0-15). The following lookup
// table is used to determine if there is a change and if there is its
// direction.
// For example, the if bits 0-3 are all 0, this would mean that A
// is Low and was Low, and that B is Low and was Low (i.e. nothing changed.)

// The offsets yield 16 values which determine the direction and how it
// effects the count. Either add 1 or subtract 1 or no change

// This is not a new idea, its been around for years.
static int rotEncLookup[] =
{
  // Notice the values forward and backward match
  0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0
};

// All F7 possible input data register addresses, used for ISR access to read
// GPIO state value.
static uint32_t rotencInputDataReg[] = 
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
static rotaryEncoderInfo_t *_allRotaryEncodersList[MEADOW_ROTARY_ENC_MAX_ENCODERS]\
        = {NULL};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void meadow_rotary_encoder_read_now(rotaryEncoderInfo_t *rotaryEncoderInfo,
          rotEncReadData_t *rotEncReadData);
static int rotenc_config_interrupt_remove(uint32_t encoderNumb);
static rotaryEncoderInfo_t *rotenc_acquire(uint32_t encoderNumb);
static void rotenc_release(rotaryEncoderInfo_t *rotaryEncoderInfo);
static bool rotenc_read_gpio_bit(volatile uint32_t *idrAddress, uint8_t pinInfo);
static int rotenc_config_gpio_arm(uint32_t cfgset, xcpt_t isr,
          rotaryEncoderInfo_t *rotaryEncoderInfo, uint32_t encoderNumb,
          char pinLetter);

/****************************************************************************
 * Private Functions
 ****************************************************************************/
// This function, called by the GPIO ISRs, merges whichever pin just changed
// into prevCondBits and adds/subtracts the count. Both rising and falling
// edges are counted for interrupts, 'A' and 'B'. The read-modify-write of
// prevCondBits happens in the same critical section as the lookupOff/
// totalCount update (rather than in the calling ISR) so that isr_a and
// isr_b can never interleave their updates to this shared per-encoder state,
// regardless of their relative interrupt priorities.
static void rotenc_calc_dir_count(rotaryEncoderInfo_t *rotaryEncoderInfo,
          uint32_t changedPinBitMask, bool pinIsHigh)
{
  irqstate_t flags = enter_critical_section();

  // Merge the changed pin's new level with the other pin's last-known level
  uint32_t newCondBits = rotaryEncoderInfo->prevCondBits & ~changedPinBitMask;
  if(pinIsHigh)
  {
    newCondBits |= changedPinBitMask;
  }
  rotaryEncoderInfo->prevCondBits = newCondBits;

  // Calculate the new lookup table offset to determine what happened
  uint32_t lookupOff = rotaryEncoderInfo->lookupOff;
  lookupOff <<= 2;           // Move previous A & B to bits 2 & 3
  lookupOff |= newCondBits;  // Add the new A or B in bits 0 & 1
  lookupOff &= 0x0000000f;   // Save only lowest 4 bits
  rotaryEncoderInfo->lookupOff = lookupOff;

  // Use the new state bits to lookup direction (only 3 possible values)
  int newDir = rotEncLookup[lookupOff];

  switch (newDir)
  {
    case 0: // no change
      break;

    case 1: // CW
      rotaryEncoderInfo->totalCount++;
      break;

    case -1: // CCW
      rotaryEncoderInfo->totalCount--;
      break;

    default:
      break;
  }
  leave_critical_section(flags);
}

//===========================================================================
// Static - Read one GPIO pin's current level from its cached IDR pointer.
// Shared by both ISRs below.
static bool rotenc_read_gpio_bit(volatile uint32_t *idrAddress, uint8_t pinInfo)
{
  uint8_t pinNumb = pinInfo & 0x0f;
  return (*idrAddress & (1 << pinNumb)) != 0;
}

//===========================================================================
// Rotary encoder input A ISR
// Note: may need to insure that both 'A' and 'B' are changing every other
// call, Possibly use int isrACount and isrBCount and if difference > than
// some small number, warn user (e.g. "Only GPIO is changing").
static int rotenc_gpio_rot_enc_isr_a(int irq, void *context, void *arg)
{
  rotaryEncoderInfo_t *rotaryEncoderInfo = (rotaryEncoderInfo_t *)arg;

  bool pinIsHigh = rotenc_read_gpio_bit(rotaryEncoderInfo->idrAddressA,
            rotaryEncoderInfo->pinInfoA);

  // Bit 0x01 is pin A's bit within prevCondBits/newCondBits
  rotenc_calc_dir_count(rotaryEncoderInfo, 0x01, pinIsHigh);
  return OK;
}

// ===========================================================================
// Rotary encoder input B ISR
static int rotenc_gpio_rot_enc_isr_b(int irq, void *context, void *arg)
{
  rotaryEncoderInfo_t *rotaryEncoderInfo = (rotaryEncoderInfo_t *)arg;

  bool pinIsHigh = rotenc_read_gpio_bit(rotaryEncoderInfo->idrAddressB,
            rotaryEncoderInfo->pinInfoB);

  // Bit 0x02 is pin B's bit within prevCondBits/newCondBits
  rotenc_calc_dir_count(rotaryEncoderInfo, 0x02, pinIsHigh);
  return OK;
}

//========================================================================
// Static - Register the caller as an active reader of encoderNumb's info,
// guarding against a concurrent rotenc_config_interrupt_remove(). The
// lookup and the increment happen in the same critical section as
// rotenc_config_interrupt_remove()'s un-publish step, so the two can never
// interleave: either this sees the still-valid pointer and remove() is then
// blocked from freeing it, or remove() already ran and this sees NULL.
// Returns NULL if the encoder is not configured; caller must not call
// rotenc_release() in that case.
rotaryEncoderInfo_t *rotenc_acquire(uint32_t encoderNumb)
{
  irqstate_t flags = enter_critical_section();
  rotaryEncoderInfo_t *rotaryEncoderInfo = _allRotaryEncodersList[encoderNumb];
  if(rotaryEncoderInfo != NULL)
  {
    rotaryEncoderInfo->activeReaders++;
  }
  leave_critical_section(flags);
  return rotaryEncoderInfo;
}

//========================================================================
// Static - Unregister as an active reader. If rotenc_config_interrupt_remove()
// ran while we were active it deferred the free() to whichever caller turns
// out to be last out - do it here if that's us.
void rotenc_release(rotaryEncoderInfo_t *rotaryEncoderInfo)
{
  irqstate_t flags = enter_critical_section();
  rotaryEncoderInfo->activeReaders--;
  bool shouldFree = (rotaryEncoderInfo->activeReaders == 0) &&
                     rotaryEncoderInfo->pendingFree;
  leave_critical_section(flags);

  if(shouldFree)
  {
    kmm_free(rotaryEncoderInfo);
  }
}

//========================================================================
// Static - Return data now. Caller must hold a reference via rotenc_acquire().
void meadow_rotary_encoder_read_now(rotaryEncoderInfo_t *rotaryEncoderInfo,
          rotEncReadData_t *rotEncReadData)
{
  // Get the current data values
  irqstate_t flags = enter_critical_section();
  rotEncReadData->encoderCount = \
      rotaryEncoderInfo->totalCount / MEADOW_ROTENC_GPIO_EDGES_PER_COUNT;

  // What was the overall direction CW or CCW?
  int countDiff = rotaryEncoderInfo->totalCount - rotaryEncoderInfo->prevCount;
  rotEncReadData->rotClockWise = countDiff >= 0 ? \
    MEADOW_ROTARY_ENC_DIRECTION_CW : MEADOW_ROTARY_ENC_DIRECTION_CCW;
 
  // How much did the count change?
  rotEncReadData->encoderChange = \
    countDiff / MEADOW_ROTENC_GPIO_EDGES_PER_COUNT;

  // Prep for next read
  rotaryEncoderInfo->prevCount = rotaryEncoderInfo->totalCount;
  leave_critical_section(flags);
  return;
}

//========================================================================
// Static - Remove an existing rotary encoder entry
// Caller will need to handle GPIO configuration as needed.
int rotenc_config_interrupt_remove(uint32_t encoderNumb)
{
  int ret;
  uint8_t pinInfoA;
  uint8_t pinInfoB;
  rotaryEncoderInfo_t *rotaryEncoderInfo;
  bool shouldFreeNow = false;

  // Un-publish the slot and snapshot whether a reader is currently active,
  // atomically with rotenc_acquire()'s lookup+increment (see there). This
  // also guards against two concurrent removes of the same encoder both
  // acting on it - the second one to run this critical section sees NULL.
  irqstate_t flags = enter_critical_section();
  rotaryEncoderInfo = _allRotaryEncodersList[encoderNumb];
  if(rotaryEncoderInfo != NULL)
  {
    _allRotaryEncodersList[encoderNumb] = NULL;
    rotaryEncoderInfo->pendingFree = true;
    shouldFreeNow = (rotaryEncoderInfo->activeReaders == 0);
  }
  leave_critical_section(flags);

  if(rotaryEncoderInfo == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Encoder %lu already removed\n",
              __FILE__, __LINE__, encoderNumb);
    return -ENOTEMPTY;
  }

  // Get info from the configuration memory before possibly freeing it
  pinInfoA = rotaryEncoderInfo->pinInfoA;
  pinInfoB = rotaryEncoderInfo->pinInfoB;

  // Tell Nuttx to forget about these GPIO interrupts
  // This call will also disables the interrupts
  ret = stm32_gpiosetevent(pinInfoA, 0, 0, 0, NULL, NULL);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error removing encoder:%lu Pin A interrupt, ret:%d\n",
              __FILE__, __LINE__, encoderNumb, ret);
  }

  ret = stm32_gpiosetevent(pinInfoB, 0, 0, 0, NULL, NULL);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error removing encoder:%lu Pin B interrupt, ret:%d\n",
              __FILE__, __LINE__, encoderNumb, ret);
  }

  if(shouldFreeNow)
  {
    kmm_free(rotaryEncoderInfo);
  }

  return ret;
}

//========================================================================
// Static - Arm one pin's rising/falling-edge interrupt for a rotary
// encoder GPIO, logging on failure. Returns the stm32_gpiosetevent()
// result (>= 0 on success).
int rotenc_config_gpio_arm(uint32_t cfgset, xcpt_t isr,
          rotaryEncoderInfo_t *rotaryEncoderInfo, uint32_t encoderNumb,
          char pinLetter)
{
  int ret = stm32_gpiosetevent(cfgset, 1, 1, 0, isr, rotaryEncoderInfo);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error encoder:%lu Pin %c interrupt, ret:%d\n",
              __FILE__, __LINE__, encoderNumb, pinLetter, ret);
  }
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// Called to configure/delete rotary encoder
int meadow_rotary_encoder_config(rotEncConfigParms_t* cfg)
{
  int ret = OK;
  rotaryEncoderInfo_t *rotaryEncoderInfo;

  if(cfg == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Parameter cfg cannot be NULL\n",
              __FILE__, __LINE__);
    return -EINVAL;
  }

  // Is encoder number out of range?
  if(cfg->encoderNumb > (MEADOW_ROTARY_ENC_MAX_ENCODERS - 1))
  {
    syslog(LOG_ERR, "%s@%d-Encoder %lu not available. Encoder range is 0 - %u.\n",
              __FILE__, __LINE__, cfg->encoderNumb,
              (MEADOW_ROTARY_ENC_MAX_ENCODERS - 1));
    return -ERANGE;    // Out of Range
  }

  // Check if slot is in use
  rotaryEncoderInfo = _allRotaryEncodersList[cfg->encoderNumb];
  if(cfg->isAddEncoder)
  {
    // Add, but is slot empty?
    if(rotaryEncoderInfo != NULL)
    {
      syslog(LOG_ERR, "%s@%d-Encoder %lu is in use\n",
                __FILE__, __LINE__, cfg->encoderNumb);
      return -ENOTEMPTY;    // Not empty
    }
  }
  else
  {
    // Remove, but is there something to remove?
    if(rotaryEncoderInfo == NULL)
    {
      syslog(LOG_ERR, "%s@%d-Encoder %lu already removed\n",
                __FILE__, __LINE__, cfg->encoderNumb);
      return -ENOTEMPTY;    // Not empty
    }

    // Remove
    ret = rotenc_config_interrupt_remove(cfg->encoderNumb);
    return ret;
  }

  // Adding new Rotary Encoder

  // Verify GPIO parameters are in range
  if(cfg->portA > MEADOW_ROTARY_ENC_MAX_PORT || cfg->portB > MEADOW_ROTARY_ENC_MAX_PORT ||
    cfg->pinA > MEADOW_ROTARY_ENC_MAX_PIN || cfg->pinB > MEADOW_ROTARY_ENC_MAX_PIN)
  {
    syslog(LOG_ERR, "%s@%d-Encoder %lu, GPIO Port or PIN is out of range\n",
              __FILE__, __LINE__, cfg->encoderNumb);
    return -ERANGE;    // Out of Range
  }

  rotaryEncoderInfo = kmm_zalloc(sizeof(rotaryEncoderInfo_t));
  if(rotaryEncoderInfo == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Memory allocation failed\n", __FILE__, __LINE__);
    return -ENOMEM;
  }

  // Add allocated memory to correct free array slot
  _allRotaryEncodersList[cfg->encoderNumb] = rotaryEncoderInfo;

  // Add configuration information to encoder information
  rotaryEncoderInfo->encoderNumb = cfg->encoderNumb;
  rotaryEncoderInfo->delayMs = cfg->delayMs;

  // The 2 GPIO inputs are used to configure and remove interrupt handling
  uint8_t pinDesignationA = cfg->portA << 4 | cfg->pinA;
  uint8_t pinDesignationB = cfg->portB << 4 | cfg->pinB;
  rotaryEncoderInfo->pinInfoA = pinDesignationA;
  rotaryEncoderInfo->pinInfoB = pinDesignationB;

  // Find the correct static offset for both GPIOs
  rotaryEncoderInfo->idrAddressA =
    (volatile uint32_t *)rotencInputDataReg[cfg->portA];
  rotaryEncoderInfo->idrAddressB =
    (volatile uint32_t *)rotencInputDataReg[cfg->portB];

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
  uint32_t cfgsetB = (pinDesignationB & 0x000000ff);   // Set Port and Pin and clear MM

  switch(cfg->resistorMode)
  {
    case 0: // Float
      cfgsetA |= GPIO_FLOAT;
      cfgsetB |= GPIO_FLOAT;
      break;
    case 1: // Pull up
      cfgsetA |= GPIO_PULLUP;
      cfgsetB |= GPIO_PULLUP;
      break;
    case 2: // Pull down
      cfgsetA |= GPIO_PULLDOWN;
      cfgsetB |= GPIO_PULLDOWN;
      break;
    default:
      syslog(LOG_ERR, "%s@%d-Encoder %lu, resistorMode %lu is out of range\n",
                __FILE__, __LINE__, cfg->encoderNumb, cfg->resistorMode);
      rotenc_config_interrupt_remove(cfg->encoderNumb);
      return -EINVAL;
  }

  // Setup both rising and falling input points to trigger isr. This also
  // enables the interrupts. If either pin fails to arm, undo everything via
  // rotenc_config_interrupt_remove() rather than leaving a half-wired
  // encoder permanently stuck in the "in use" slot.
  ret = rotenc_config_gpio_arm(cfgsetA, rotenc_gpio_rot_enc_isr_a, rotaryEncoderInfo,
            cfg->encoderNumb, 'A');
  if(ret < 0)
  {
    rotenc_config_interrupt_remove(cfg->encoderNumb);
    return ret;
  }

  ret = rotenc_config_gpio_arm(cfgsetB, rotenc_gpio_rot_enc_isr_b, rotaryEncoderInfo,
            cfg->encoderNumb, 'B');
  if(ret < 0)
  {
    rotenc_config_interrupt_remove(cfg->encoderNumb);
    return ret;
  }

  return OK;
}

//========================================================================
// Public - Allow the user to set or clear the current rotary encoder count.
int meadow_rotary_encoder_set_count(uint32_t encoderNumb, int32_t newCount)
{
  // Check for valid encoder number (0-7)
  if(encoderNumb > (MEADOW_ROTARY_ENC_MAX_ENCODERS - 1))
  {
    syslog(LOG_ERR, "%s@%d-Encoder %lu not available. Encoder range is 0 - %u.\n",
              __FILE__, __LINE__, encoderNumb,
              (MEADOW_ROTARY_ENC_MAX_ENCODERS - 1));
    return -ERANGE;    // Out of Range
  }

  rotaryEncoderInfo_t *rotaryEncoderInfo = rotenc_acquire(encoderNumb);

  // Is requested slot valid?
  if(rotaryEncoderInfo == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Encoder %lu is not configured\n",
              __FILE__, __LINE__, encoderNumb);
    return -ENODATA;    // No Data
  }

  irqstate_t flags = enter_critical_section();
  rotaryEncoderInfo->totalCount = newCount;
  rotaryEncoderInfo->prevCount = newCount;
  leave_critical_section(flags);

  rotenc_release(rotaryEncoderInfo);
  return OK;
}

//========================================================================
// Public - Read rotary encoder information.
// 
// A configured delay less or equal to MEADOW_ROTENC_DELAY_LOWER_THRESHOLD_MS
// encoder data is return immediately.
// If the configured delay is greater than
// MEADOW_ROTENC_DELAY_LOWER_THRESHOLD_MS the calling thread waits for the
// specified delay period. After that period, if the count has changed, the
// calling thread returns with encoder data. If the count has not changed the
// calling thread will continue to wait, only returning when there is a
// change.
int meadow_rotary_encoder_read_data(rotEncReadData_t *rotEncReadData)
{
  rotaryEncoderInfo_t *rotaryEncoderInfo;
  int ret = OK;

  // Check for NULL
  if(rotEncReadData == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Input parameter cannot be NULL\n",
              __FILE__, __LINE__);
    return -EINVAL;
  }

  // Check for valid encoder number (0-7)
  if(rotEncReadData->encoderNumb > (MEADOW_ROTARY_ENC_MAX_ENCODERS - 1))
  {
    syslog(LOG_ERR, "%s@%d-Encoder %lu not available. Encoder range is 0 - %u.\n",
              __FILE__, __LINE__, rotEncReadData->encoderNumb,
              (MEADOW_ROTARY_ENC_MAX_ENCODERS - 1));
    return -ERANGE;    // Out of Range
  }

  rotaryEncoderInfo = rotenc_acquire(rotEncReadData->encoderNumb);

  // Is requested slot configured?
  if(rotaryEncoderInfo == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Encoder %lu is not configured\n",
              __FILE__, __LINE__, rotEncReadData->encoderNumb);
    return -ENODATA;    // No Data
  }

  // Check if we should wait or immediately read the encoder's values
  if(rotaryEncoderInfo->delayMs <= MEADOW_ROTENC_DELAY_LOWER_THRESHOLD_MS)
  {
    // Get info and return since there's no delay requested.
    meadow_rotary_encoder_read_now(rotaryEncoderInfo, rotEncReadData);
  }
  else
  {
    // This simple polling loop does the waiting. abs() is used so a change
    // in either direction (CW or CCW) satisfies the wait.
    while(true)
    {
      // totalCount/prevCount are also written from ISR context (and reset
      // together by set_count()), so read them under a critical section
      // rather than racing the ISR/writer for an unprotected read.
      irqstate_t condFlags = enter_critical_section();
      int countDiff = rotaryEncoderInfo->totalCount - rotaryEncoderInfo->prevCount;
      leave_critical_section(condFlags);
      if(abs(countDiff) >= MEADOW_ROTENC_GPIO_EDGES_PER_COUNT)
      {
        break;
      }

      usleep(rotaryEncoderInfo->delayMs * 1000);

      // Bail out if the encoder was removed while we were waiting - the
      // hardware is already torn down at that point, so a count change is
      // never coming.
      irqstate_t pollFlags = enter_critical_section();
      bool wasRemoved = rotaryEncoderInfo->pendingFree;
      leave_critical_section(pollFlags);
      if(wasRemoved)
      {
        ret = -ENODATA;
        break;
      }
    }

    if(ret == OK)
    {
      meadow_rotary_encoder_read_now(rotaryEncoderInfo, rotEncReadData);
    }
  }

  rotenc_release(rotaryEncoderInfo);
  return ret;
}

#endif      // defined(MEADOW_ROTARY_ENCODER_BUILD)
