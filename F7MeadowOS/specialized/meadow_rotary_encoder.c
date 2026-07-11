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

// If code never wanted remove meadow_rotary_encoder from
// nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src/Make.defs
// Otherwise the meadow_rotary_encoder.c file must exist.
#if(MEADOW_ROTARY_ENCODER_BUILD > 0)

#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <semaphore.h>
#include <arch/board/board.h>
#include "stm32_gpio.h"
#include <nuttx/arch.h>

#if defined(CONFIG_ROTARY_ENCODER_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
#pragma message "(--) meadow_rotary_encoder.c"
#endif

// #define USE_MEADOW_DEBUG_HELPERS
#undef USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>

#pragma GCC optimize("O0")    // Prevent compiler from changing the code

//============================================================
// Defines the maximum number of encoders that can be monitored.
// Since the F7, under Nuttx, only has 16 "interrupt groups" there's no
// point in having more that 8 rotary encoders.
#define MEADOW_ROTARY_ENC_MAX_ENCODERS (8)

#define MEADOW_ROTARY_ENC_MAX_PORT (10)   // A-K
#define MEADOW_ROTARY_ENC_MAX_PIN (15)    // 0-15

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

// The offsets yield 16 values which determine the direction, and how it
// effects the count. Either add 1 or subtract 1 or do nothing

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

static int meadow_rotary_encoder_read_now(rotEncReadData_t *rotEncReadData);
static int rotenc_config_interrupt_remove(uint32_t encoderNumb);
static int meadow_rotenc_wqueue_set_delay(rotaryEncoderInfo_t *rotaryEncoderInfo);
static void meadow_rotenc_wqueue_cancel(rotaryEncoderInfo_t *rotaryEncoderInfo);
static void meadow_rotenc_takesem(sem_t *semaphore);

/****************************************************************************
 * Private Functions
 ****************************************************************************/
// This functions finds the direction (CW or CCW) and adds/subtracts the count.
// This counts rising and falling edges of interrupts, 'A' and 'B'.
static void rotenc_calc_dir_count(rotaryEncoderInfo_t *rotaryEncoderInfo,
          uint32_t newCondBits)
{
  irqstate_t flags = enter_critical_section();

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
      rotaryEncoderInfo->rotClockWise = 0;
      rotaryEncoderInfo->totalCount++;
      rotaryEncoderInfo->sinceRdCount++;
      break;

    case -1: // CCW
      rotaryEncoderInfo->rotClockWise = 1;
      rotaryEncoderInfo->totalCount--;
      rotaryEncoderInfo->sinceRdCount--;
      break;

    default:
      break;
  }
  leave_critical_section(flags);
}

//===========================================================================
// Rotary encoder input A ISR
// Note: may need to insure that both 'A' and 'B' are changing every other
// call, Possibly use int isrACount and isrBCount and if difference > than
// some small number, warn user (e.g. "Only GPIO is changing").
static int rotenc_gpio_rot_enc_isr_a(int irq, void *context, void *arg)
{
  uint8_t pinNumb;
  uint32_t idrRegister;
  rotaryEncoderInfo_t *rotaryEncoderInfo = (rotaryEncoderInfo_t *)arg;
   
  // Save previous B state
  uint32_t newCondBits = (rotaryEncoderInfo->prevCondBits) & 0x02;

  // Find the current GPIO state 
  idrRegister = *((uint32_t *)(rotaryEncoderInfo->idrAddressA));
  pinNumb = rotaryEncoderInfo->pinInfoA & 0x0f;
  bool gpioState = (idrRegister & (1 << pinNumb)) > 0 ? true : false;
  
  if(gpioState)
  {
    newCondBits |= 0x01;    // Set bit A if high
  }

  // Only saves 2 ls bits
  rotaryEncoderInfo->prevCondBits = newCondBits;

  rotenc_calc_dir_count(rotaryEncoderInfo, newCondBits);
  return OK;
}

// ===========================================================================
// Rotary encoder input B ISR
static int rotenc_gpio_rot_enc_isr_b(int irq, void *context, void *arg)
{
  uint8_t pinNumb;
  uint32_t idrRegister;
  
  rotaryEncoderInfo_t *rotaryEncoderInfo = (rotaryEncoderInfo_t *)arg;

  // Save previous A state
  uint32_t newCondBits = (rotaryEncoderInfo->prevCondBits) & 0x01;

  // Find the current GPIO state 
  idrRegister = *((uint32_t *)(rotaryEncoderInfo->idrAddressB));
  pinNumb = rotaryEncoderInfo->pinInfoB & 0x0f;
  bool gpioState =  (idrRegister & (1 << pinNumb)) > 0 ? true : false;
  if(gpioState)
  {
    newCondBits |= 0x02;    // Set bit B
  }

  // Only saves 2 ls bits
  rotaryEncoderInfo->prevCondBits = newCondBits;

  rotenc_calc_dir_count(rotaryEncoderInfo, newCondBits);
  return OK;
}

//========================================================================
// Static - Return data now
int meadow_rotary_encoder_read_now(rotEncReadData_t *rotEncReadData)
{
  rotaryEncoderInfo_t *rotaryEncoderInfo;
  rotaryEncoderInfo = _allRotaryEncodersList[rotEncReadData->encoderNumb];

  // Find the encoders current data values, clear one and return
  irqstate_t flags = enter_critical_section();
  rotEncReadData->encoderCount = \
      rotaryEncoderInfo->totalCount / MEADOW_ROTENC_GPIO_EDGES_PER_COUNT;
  rotEncReadData->rotClockWise = \
      rotaryEncoderInfo->rotClockWise;
  rotEncReadData->encoderChange = \
      rotaryEncoderInfo->sinceRdCount / MEADOW_ROTENC_GPIO_EDGES_PER_COUNT;
  leave_critical_section(flags);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// To be called from .Net app to configure/delete rotary encoder
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

  // Get possible existing information
  rotaryEncoderInfo = _allRotaryEncodersList[cfg->encoderNumb];
  if(cfg->isAddEncoder)
  {
    // Add, but is slot empty?
    if(rotaryEncoderInfo != NULL)
    {
      syslog(LOG_ERR, "%s@%d-Encoder %ld is in use\n",
                __FILE__, __LINE__, cfg->encoderNumb);
      return -ENOTEMPTY;    // Not empty
    }
  }
  else
  {
    // Remove, but is there something there?
    if(rotaryEncoderInfo == NULL)
    {
      syslog(LOG_ERR, "%s@%d-Encoder %ld already removed\n",
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

  rotaryEncoderInfo = zalloc(sizeof(rotaryEncoderInfo_t));
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
  rotaryEncoderInfo->idrAddressA = rotencInputDataReg[cfg->portA];
  rotaryEncoderInfo->idrAddressB = rotencInputDataReg[cfg->portB];

  if(rotaryEncoderInfo->delayMs > MEADOW_ROTENC_DELAY_LOWER_THRESHOLD_MS)
  {
    // Need to be able to block callers thread if a delay is requested
    sem_init(&(rotaryEncoderInfo->delayChgSem), 0, 0);
    sem_setprotocol(&(rotaryEncoderInfo->delayChgSem), SEM_PRIO_NONE);

    // Signalled by the last active reader/worker when removal is pending
    sem_init(&(rotaryEncoderInfo->removeDoneSem), 0, 0);
    sem_setprotocol(&(rotaryEncoderInfo->removeDoneSem), SEM_PRIO_NONE);
  }

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
  }

  // Setup both rising and falling input points to trigger isr
  // This call also enables the interrupts
  ret = stm32_gpiosetevent(
    cfgsetA,                      // Nuttx cfgset
    1,                            // rising edge,
    1,                            // falling edge,
    0,                            // event
    rotenc_gpio_rot_enc_isr_a,    // ISR A
    rotaryEncoderInfo);           // information address
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error encoder:%ld Pin A interrupt, ret:%d\n",
              __FILE__, __LINE__, cfg->encoderNumb, ret);
  }

  ret = stm32_gpiosetevent(
    cfgsetB,                      // Nuttx cfgset
    1,                            // rising edge,
    1,                            // falling edge,
    0,                            // event
    rotenc_gpio_rot_enc_isr_b,    // ISR B
    rotaryEncoderInfo);           // information address
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error encoder:%ld Pin B interrupt, ret:%d\n",
              __FILE__, __LINE__, cfg->encoderNumb, ret);
  }

  return ret;
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
  rotaryEncoderInfo = _allRotaryEncodersList[encoderNumb];

  // Release any waiting thread
  if(rotaryEncoderInfo->delayMs > MEADOW_ROTENC_DELAY_LOWER_THRESHOLD_MS)
  {
    // Indicate removal in progress so a reader or the wqueue worker that's
    // already mid-flight will bail out instead of touching delayChgSem /
    // rotEncWQueue after we destroy/free them. Keep track of how many
    // are currently registered while we still hold the critical section.
    irqstate_t flags = enter_critical_section();
    // Note the isRemoving variable is set 'true' but never set to 'false'.
    // Why? Because the isRemoving flag is part of the rotaryEncoderInfo
    // struct which is freed a few lines down.
    rotaryEncoderInfo->isRemoving = true;
    int32_t usersLeft = rotaryEncoderInfo->activeUserCount;
    leave_critical_section(flags);

    // Wake anything already blocked in takesem() so it observes isRemoving
    sem_post(&(rotaryEncoderInfo->delayChgSem));

    // Best-effort: stops a not-yet-dispatched timeout callback. If the
    // worker is already executing, the isRemoving check below handles it.
    meadow_rotenc_wqueue_cancel(rotaryEncoderInfo);

    // Wait for any reader/worker that had already registered before we
    // set isRemoving to finish with delayChgSem/rotEncWQueue.
    if(usersLeft > 0)
    {
      meadow_rotenc_takesem(&(rotaryEncoderInfo->removeDoneSem));
    }

    sem_destroy(&(rotaryEncoderInfo->delayChgSem));
    sem_destroy(&(rotaryEncoderInfo->removeDoneSem));
  }

  // Get info from the configuration memory before freeing it
  pinInfoA = rotaryEncoderInfo->pinInfoA;
  pinInfoB = rotaryEncoderInfo->pinInfoB;

  // Tell Nuttx to forget about these GPIO interrupts
  // This call will also disables the interrupts
  ret = stm32_gpiosetevent(pinInfoA, 0, 0, 0, NULL, NULL);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error removing encoder:%ld Pin A interrupt, ret:%d\n",
              __FILE__, __LINE__, encoderNumb, ret);
  }

  ret = stm32_gpiosetevent(pinInfoB, 0, 0, 0, NULL, NULL);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error removing encoder:%ld Pin B interrupt, ret:%d\n",
              __FILE__, __LINE__, encoderNumb, ret);
  }

  free(rotaryEncoderInfo);
  _allRotaryEncodersList[encoderNumb] = NULL;

  return ret;
}

//========================================================================
// Public - Allow the user to set or clear the current rotary encoder count.
int meadow_rotary_encoder_set_count(uint32_t encoderNumb, int32_t newCount)
{
  rotaryEncoderInfo_t *rotaryEncoderInfo;
  rotaryEncoderInfo = _allRotaryEncodersList[encoderNumb];

  // Is requested slot valid?
  if(rotaryEncoderInfo == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Encoder %ld is not configured\n",
              __FILE__, __LINE__, encoderNumb);
    return -ENODATA;    // No Data
  }

  irqstate_t flags = enter_critical_section();
  rotaryEncoderInfo->totalCount = newCount;
  leave_critical_section(flags);

  return OK;
}

//========================================================================
// Public - Return the current rotary encoder information.
// 
// If the configured delay is > MEADOW_ROTENC_DELAY_LOWER_THRESHOLD_MS the
// calling thread is blocked for the specified delay period. At the end of
// the delay period, if the count has changed, the thread returns with
// encoder data. If the count has not changed the calling thread will
// continue to rechecked at the rate of the provided delay. Any delay
// less than MEADOW_ROTENC_DELAY_LOWER_THRESHOLD_MS will return immediately.
int meadow_rotary_encoder_read_data(rotEncReadData_t *rotEncReadData)
{
  int ret;
  int loopRet;  // (--) Needed?
  rotaryEncoderInfo_t *rotaryEncoderInfo;

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

  rotaryEncoderInfo = _allRotaryEncodersList[rotEncReadData->encoderNumb];

  // Is requested slot valid?
  if(rotaryEncoderInfo == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Encoder %ld is not configured\n",
              __FILE__, __LINE__, rotEncReadData->encoderNumb);
    return -ENODATA;    // No Data
  }

  if(rotaryEncoderInfo->delayMs <= MEADOW_ROTENC_DELAY_LOWER_THRESHOLD_MS)
  {
    // Get info and exit
    ret = meadow_rotary_encoder_read_now(rotEncReadData);
    rotaryEncoderInfo->sinceRdCount = 0;  // Clear count (--) here?
    return ret;
  }

  irqstate_t flags = enter_critical_section();
  // If in the process of being removed bailout
  if(rotaryEncoderInfo->isRemoving)
  {
    leave_critical_section(flags);
    return -ENODEV;
  }

  // Register as an active user of delayChgSem/rotEncWQueue before touching
  // either one, so a concurrent remove() knows to wait for us.
  rotaryEncoderInfo->activeUserCount++;
  leave_critical_section(flags);

  do
  {
    // Schedule a delay
    loopRet = meadow_rotenc_wqueue_set_delay(rotaryEncoderInfo);
    if(loopRet < 0)
    {
      syslog(LOG_ERR, "%s@%d-Error: encoder:%ld set delay() ret:%d, errno:%d\n",
                __FILE__, __LINE__, rotEncReadData->encoderNumb,
                loopRet, errno);
      break;
    }

    // Wait for periodic timeout 
    meadow_rotenc_takesem(&(rotaryEncoderInfo->delayChgSem));

  // Because we count each edge, if the sinceRdCount is < edges we'll leave
  // loop too early
  } while (rotaryEncoderInfo->sinceRdCount < MEADOW_ROTENC_GPIO_EDGES_PER_COUNT &&
    !rotaryEncoderInfo->isRemoving);

  rotaryEncoderInfo->sinceRdCount = 0;  // Clear count

  // // Has count changed? If not loop and try again.
  // while(rotaryEncoderInfo->sinceRdCount == 0 &&
  //   !rotaryEncoderInfo->isRemoving)
  // {
  //   // Reschedule a delay
  //   ret = meadow_rotenc_wqueue_set_delay(rotaryEncoderInfo);
  //   if(ret < 0)
  //   {
  //     syslog(LOG_ERR, "%s@%d-Error: encoder:%ld set delay() ret:%d, errno:%d\n",
  //               __FILE__, __LINE__, rotEncReadData->encoderNumb, ret, errno);
  //     isError = true;
  //     break;
  //   }

  //   // Keep rechecking at top of while Wait/rewait for semaphore to be signalled, waits till delay has passed
  //   // then returns and checks again. Will keep rewaiting until rotary encoder
  //   // value changes.
  //   // We wait here until LPWORK has timed out
  //   meadow_rotenc_takesem(&(rotaryEncoderInfo->delayChgSem));
  // }

  // Check if we left the loop due to an error
  if(loopRet == OK)
  {
    ret = meadow_rotary_encoder_read_now(rotEncReadData);
  }

  // Unregister. If removal is in progress and we're the last active user,
  // wake the remover so it's safe for it to destroy/free the encoder.
  flags = enter_critical_section();
  rotaryEncoderInfo->activeUserCount--;
  bool isLastUser = rotaryEncoderInfo->isRemoving &&
                     (rotaryEncoderInfo->activeUserCount == 0);
  leave_critical_section(flags);
  if(isLastUser)
  {
    sem_post(&(rotaryEncoderInfo->removeDoneSem));
  }

  return ret;
}

//=================================================================
// Static Callback via the LPWORK worker queue
static void meadow_rotenc_wqueue_callback(void *arg)
{
  if(arg == NULL)
    return;

  rotaryEncoderInfo_t *rotaryEncoderInfo = (rotaryEncoderInfo_t *) arg;

  syslog(LOG_MDIAG, "Entered wqueue worker, timeout for encoder:%u\n",
    rotaryEncoderInfo->encoderNumb);

  // Register before touching callerSem, same as a blocking reader does. If
  // remove() has already claimed the encoder, back off without touching it.
  irqstate_t flags = enter_critical_section();
  if(rotaryEncoderInfo->isRemoving)
  {
    leave_critical_section(flags);
    return;
  }
  rotaryEncoderInfo->activeUserCount++;
  leave_critical_section(flags);

  // Post the semaphore so the caller can return if value changed
  sem_post(&(rotaryEncoderInfo->delayChgSem));

  flags = enter_critical_section();
  rotaryEncoderInfo->activeUserCount--;
  bool isLastUser = rotaryEncoderInfo->isRemoving &&
                     (rotaryEncoderInfo->activeUserCount == 0);
  leave_critical_section(flags);

  if(isLastUser)
  {
    sem_post(&(rotaryEncoderInfo->removeDoneSem));
  }
}

//=================================================================
// Static - Start, restart, or stop the work queue timer
int meadow_rotenc_wqueue_set_delay(rotaryEncoderInfo_t *rotaryEncoderInfo)
{
  int ret;

  // syslog(LOG_MDIAG, "Enqueuing for encoder:%lu, for:%lu millisec (ticks:%ld)\n",
  //   encoderNumb, rotaryEncoderInfo->delayMs,
  //   MSEC2TICK(rotaryEncoderInfo->delayMs));
  // usleep(20 * 1000);

  ret = work_queue(LPWORK, &rotaryEncoderInfo->rotEncWQueue,
    meadow_rotenc_wqueue_callback,
    (void *)rotaryEncoderInfo,
    MSEC2TICK(rotaryEncoderInfo->delayMs));

  return ret;
}

//=================================================================
// Static - Delete the work queue timer
void meadow_rotenc_wqueue_cancel(rotaryEncoderInfo_t *rotaryEncoderInfo)
{
  // syslog(LOG_INFO, "%s@%d-Cancelling the delay timer, ptr:%p\n",
  //   __FILE__, __LINE__, _work_queue.worker);

  // If worker is not active return. Otherwise cancel the work
  if (work_available(&rotaryEncoderInfo->rotEncWQueue))
    return;

  // Only possible error is -ENOENT no such work queued, we can ignore
  work_cancel(LPWORK, &rotaryEncoderInfo->rotEncWQueue);
}

//=====================================================================
// Static - Wait for semaphore to be posted
void meadow_rotenc_takesem(sem_t *semaphore)
{
  int ret;

  do
  {
    /* Take the semaphore (perhaps waiting) */
    ret = sem_wait(semaphore);
  }
  while (ret == -EINTR);
}


#endif      // defined(MEADOW_ROTARY_ENCODER_BUILD)
