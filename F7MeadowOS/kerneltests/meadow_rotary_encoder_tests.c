/****************************************************************************
 * Meadow.OS/kerneltests/meadow_rotary_encoder_tests.c
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

// In ./build.sh -u=rotenc
// Then -p 18 -v <test number>
// e.g. meadow developer -p 18 -v 1 (config D05 & D06)

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <nuttx/config.h>
#include <meadow/meadow_rotary_encoder.h>

// Only build CONFIG_ROTARY_ENCODER_TESTS if configured
#if defined(CONFIG_ROTARY_ENCODER_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)

#include <meadow/hcom_shared_common.h>
#include "../hcom_nx/hcom_nx_common.h"
#include "stm32_gpio.h"   // stm32_configgpio
#include <nuttx/kthread.h>

#pragma message "(--) meadow_rotary_encoder_tests.c"

// Diagnostic always as this is test code
// #define USE_MEADOW_DEBUG_HELPERS
#undef USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/

// D05 (PB4) for Version 2 Feather or CCM V1. Interrupt group isolation means
// that no pins can share the same Pin number for a rotary encoder
#define ENCODER_PIN_CCM_D05_PB4_INPUT   (GPIO_INPUT | GPIO_PULLDOWN | GPIO_PORTB | GPIO_PIN4)
#define ENCODER_PIN_CCM_D06_PB13_INPUT  (GPIO_INPUT | GPIO_PULLDOWN | GPIO_PORTB | GPIO_PIN13)
#define ENCODER_PIN_CCM_D07_PB7_INPUT   (GPIO_INPUT | GPIO_PULLDOWN | GPIO_PORTB | GPIO_PIN7)
#define ENCODER_PIN_CCM_D08_PB6_INPUT   (GPIO_INPUT | GPIO_PULLDOWN | GPIO_PORTB | GPIO_PIN6)
#define ENCODER_PIN_CCM_D09_PI11_INPUT  (GPIO_INPUT | GPIO_PULLDOWN | GPIO_PORTI | GPIO_PIN11)
#define ENCODER_PIN_CCM_D10_PD5_INPUT   (GPIO_INPUT | GPIO_PULLDOWN | GPIO_PORTD | GPIO_PIN5)

/************************************************************************************
 * Private Data
 ************************************************************************************/

/************************************************************************************
 * Public Data
 ************************************************************************************/

/************************************************************************************
 * Private Function Prototypes
 ************************************************************************************/
static void rotary_encoder_test_add_config(uint32_t testNumb,
  uint32_t encoderNumb, uint32_t delayMs, uint32_t PinA, uint32_t PinB);
static void rotary_encoder_test_remove_config(uint32_t userData);
static void rotary_encoder_config_test_null_parms(uint32_t userData);
static void *rotary_encoder_test_blocking_read_kthread(int argc, char *argv[]);

/************************************************************************************
 * Public Functions
 ************************************************************************************/
// set developer -p 18 come here
// Testing entry point
void meadow_kt_rotary_encoder_tests(uint32_t userData)
{
  int ret;
  rotEncReadData_t rotEncReadData;

  syslog(LOG_MTEST,
    "Rotary Encoder tests received 'set developer -p 18 -v %lu'\n",
    userData);

  switch(userData)
  {
    //-------------------------------------------------------------
    // Configuring the encoder with and without a delay
    // Note: The input pins defined are for F7FeatherV2 D05, D06, D07, D08.
    // This test code only defines the first 6 GPIOs, even though 8 GPIOs are
    // supported. And be careful to not use the pins used by syslog,
    // D12 and D13. And keep in mind GPIO groups.
    //
    case 1:   // Encoder 0 Delay Zero millisec, D05 & DO6
    //                                       # Dly
    rotary_encoder_test_add_config(userData, 0, 0,
      ENCODER_PIN_CCM_D05_PB4_INPUT,    // GPIO A
      ENCODER_PIN_CCM_D06_PB13_INPUT);  // GPIO B
      break;
    case 2:   // Encoder 5 Delay 0 millisec, D07 & D08
      rotary_encoder_test_add_config(userData, 5, 0,
        ENCODER_PIN_CCM_D07_PB7_INPUT,
        ENCODER_PIN_CCM_D08_PB6_INPUT);
      break;
    case 3:   // Encoder 7 Delay 0 millisec, D09 & D10
      rotary_encoder_test_add_config(userData, 7, 0,
        ENCODER_PIN_CCM_D09_PI11_INPUT,
        ENCODER_PIN_CCM_D10_PD5_INPUT);
      break;
    case 4:   // Encoder 8 Delay 0 millisec, D05 & DO6
      // This configuration should fail, encoder out of range
      rotary_encoder_test_add_config(userData, 8, 0,
      ENCODER_PIN_CCM_D05_PB4_INPUT,
      ENCODER_PIN_CCM_D06_PB13_INPUT);
      break;
    case 5:   // Encoder 0 Delay 1 millisec, D05 & DO6
      rotary_encoder_test_add_config(userData, 0, 1,
      ENCODER_PIN_CCM_D05_PB4_INPUT,
      ENCODER_PIN_CCM_D06_PB13_INPUT);
      break;
    case 6:   // Encoder 0 Delay 2 millisec, D05 & DO6
      rotary_encoder_test_add_config(userData, 0, 2,
      ENCODER_PIN_CCM_D05_PB4_INPUT,
      ENCODER_PIN_CCM_D06_PB13_INPUT);
      break;
    case 7:   // Encoder 0 Delay 20 millisec, D05 & DO6
      rotary_encoder_test_add_config(userData, 0, 20,
      ENCODER_PIN_CCM_D05_PB4_INPUT,
      ENCODER_PIN_CCM_D06_PB13_INPUT);
      break;
    case 8:   // Encoder 0 Delay 200 millisec, D05 & DO6
      rotary_encoder_test_add_config(userData, 0, 200,
      ENCODER_PIN_CCM_D05_PB4_INPUT,
      ENCODER_PIN_CCM_D06_PB13_INPUT);
      break;
    case 9:   // Encoder 0 Delay 2000 millisec, D05 & DO6
      rotary_encoder_test_add_config(userData, 0, 2000,
      ENCODER_PIN_CCM_D05_PB4_INPUT,
      ENCODER_PIN_CCM_D06_PB13_INPUT);
      break;

    //-------------------------------------------------------------
    // Unconfigure
    case 11:  // Encoder 0
    case 12:  // Encoder 5
    case 13:  // Encoder 7
    case 14:  // Encoder 8 This should fail
      rotary_encoder_test_remove_config(userData);
      break;

    //-------------------------------------------------------------
    // Test reading count
    case 21:    // Encoder 0
      rotEncReadData.encoderNumb = 0;
      ret = meadow_rotary_encoder_read_data(&rotEncReadData);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "Encoder 0 returned error:%d\n", ret);
        break;
      }

      syslog(LOG_MTEST,
        "Encoder 0 - Changed by %ld since read, total:%ld, direction:%s\n",
        rotEncReadData.encoderChange,
        rotEncReadData.encoderCount,
        rotEncReadData.rotClockWise == 0 ? "CW" : "CCW") ;
      break;

    case 22:    // Encoder 5
      rotEncReadData.encoderNumb = 5;
      ret = meadow_rotary_encoder_read_data(&rotEncReadData);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "Encoder 5 returned error:%d\n", ret);
        break;
      }

      syslog(LOG_MTEST,
        "Encoder 5 - Changed by %ld since read, total:%ld, direction:%s\n",
        rotEncReadData.encoderChange,
        rotEncReadData.encoderCount,
        rotEncReadData.rotClockWise == 0 ? "CW" : "CCW") ;
      break;

    case 23:    // Encoder 7
      rotEncReadData.encoderNumb = 7;
      ret = meadow_rotary_encoder_read_data(&rotEncReadData);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "Encoder 7 returned error:%d\n", ret);
        break;
      }

      syslog(LOG_MTEST,
        "Encoder 7 - Changed by %ld since read total:%ld, direction:%s\n",
        rotEncReadData.encoderChange,
        rotEncReadData.encoderCount,
        rotEncReadData.rotClockWise == 0 ? "CW" : "CCW") ;
      break;

    //-------------------------------------------------------------
    // Reset/change count, encoder 0
    case 31:
      syslog(LOG_MTEST,
        "Rotary Encoder test-setting count encoder 0 to 0 (%lu)\n",
        userData);
      ret = meadow_rotary_encoder_set_count(0, 0);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "Encoder 0 returned error:%d\n", ret);
        break;
      }
      syslog(LOG_MTEST, "Encoder 0 set to 0\n");
      break;

    // Test Resetting count, encoder 5
    case 32:
      syslog(LOG_MTEST, "Rotary Encoder test-setting count encoder 5 to -999999 (%lu)\n",
        userData);
      ret = meadow_rotary_encoder_set_count(5, -999999);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "Encoder 5 returned error:%d\n", ret);
        break;
      }
      syslog(LOG_MTEST, "Encoder 5 set to -999999\n");
      break;

    // Test Resetting count, encoder 7
    case 33:
      syslog(LOG_MTEST, "Rotary Encoder test-setting count encoder 7 to 0 (%lu)\n",
        userData);
      ret = meadow_rotary_encoder_set_count(7, 0);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "Encoder 7 returned error:%d\n", ret);
        break;
      }
      syslog(LOG_MTEST, "Encoder 7 set to 0\n");
      break;

    //-------------------------------------------------------------
    // Function parameter null test
    case 41:    // Encoder 0
      rotary_encoder_config_test_null_parms(userData);
      break;

    //-------------------------------------------------------------
    // Concurrent remove-while-blocking-read race test (encoder 0). Starts
    // a background thread blocked in meadow_rotary_encoder_read_data(),
    // then removes encoder 0 out from under it. Expect the background
    // thread's syslog line to report a clean -ENODATA, not a hang or crash.
    case 51:
      rotary_encoder_test_exercise_test(userData);
      break;

    default:
      syslog(LOG_MTEST, "Rotary Encoder test-received unknown userData of %lu\n",
        userData);
      break;
  }
}

/************************************************************************************
 * Private Functions
 ************************************************************************************/
void rotary_encoder_test_add_config(uint32_t testNumb,
  uint32_t encoderNumb, uint32_t delayMs, uint32_t PinA, uint32_t PinB)
{
  // Setup a rotary encoder
  int ret;

  rotEncConfigParms_t* cfg = malloc(sizeof(rotEncConfigParms_t));
  if(cfg == NULL)
  {
    syslog(LOG_ERR, "Memory allocation failed\n");
    return;
  }

  syslog(LOG_MTEST, "Rotary Encoder tests-configuring encoder %lu with %lu ms delay\n",
    encoderNumb, delayMs);

  // For testing need to initialize the 2 GPIOs as inputs
  stm32_configgpio(PinA);
  stm32_configgpio(PinB);
  
  // Populate configuration struct
  cfg->encoderNumb = encoderNumb;   // Encoder number 0 - 7
  cfg->isAddEncoder = true;         // New rotary encoder
  cfg->portA = (PinA & 0xf0) >> 4;
  cfg->pinA =  (PinA & 0x0f);
  cfg->portB = (PinB & 0xf0) >> 4;
  cfg->pinB =  (PinB & 0x0f);
  cfg->resistorMode = 2;    // 2 = pull down
  cfg->delayMs = delayMs;

  // Call configuration function also used by managed code
  ret = meadow_rotary_encoder_config(cfg);
  if(ret < 0)
  {
    syslog(LOG_MTEST, "Error:meadow_rotary_encoder_config returned ret:%d\n",
      ret);
  }

  free (cfg);
}

//============================================================================
void rotary_encoder_test_remove_config(uint32_t userData)
{
  int ret;
  uint32_t encoderNumb = 9;  // Invalid
  uint32_t PinA = 0;
  uint32_t PinB = 0;

  rotEncConfigParms_t* cfg = malloc(sizeof(rotEncConfigParms_t));
  if(cfg == NULL)
  {
    syslog(LOG_ERR, "Memory allocation failed\n");
    return;
  }

  switch(userData)
  {
    case 11:
      encoderNumb = 0;
      PinA = ENCODER_PIN_CCM_D05_PB4_INPUT;
      PinB = ENCODER_PIN_CCM_D06_PB13_INPUT;
      break;
    case 12:
      encoderNumb = 5;
      PinA = ENCODER_PIN_CCM_D07_PB7_INPUT;
      PinB = ENCODER_PIN_CCM_D08_PB6_INPUT;
      break;
    case 13:
      encoderNumb = 7;
      PinA = ENCODER_PIN_CCM_D09_PI11_INPUT;
      PinB = ENCODER_PIN_CCM_D10_PD5_INPUT;
      break;
    case 14:
      encoderNumb = 8;    // Invalid
      PinA = ENCODER_PIN_CCM_D05_PB4_INPUT;
      PinB = ENCODER_PIN_CCM_D06_PB13_INPUT;
      break;
  }
  syslog(LOG_MTEST, "Rotary Encoder test-removing encoder %lu\n",
    encoderNumb);

  // Need to unconfigure the 2 GPIOs used as inputs
  stm32_unconfiggpio(PinA);
  stm32_unconfiggpio(PinB);

  // Setup to unconfigure as .Net will do
  cfg->encoderNumb = encoderNumb;   // Encoder number 0 - 7
  cfg->isAddEncoder = false;        // Remove rotary encoder

  // Call meadow_rotary_encoder.c's configuration function, used by
  // managed code
  ret = meadow_rotary_encoder_config(cfg);
  if(ret < 0)
  {
    syslog(LOG_MTEST, "Error:meadow_rotary_encoder_config returned ret:%d\n",
      ret);
  }

  free (cfg);
}

//==================================================================
// Pass in bad parameters
void rotary_encoder_config_test_null_parms(uint32_t userData)
{
  // Setup a rotary encoder
  int ret = 0;

  uint32_t encoderNumb = 0;

  switch(userData)
  {
    // NULL config test, Should fail
    case 41:    // Encoder 0
      // Call configuration function
      ret = meadow_rotary_encoder_config(NULL);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "Error:meadow_rotary_encoder_config returned ret:%d\n",
          ret);
      }
      break;
  }

  syslog(LOG_MTEST, "Rotary Encoder parameter test: encoder:%lu, ret:%d\n",
    encoderNumb, ret);
}

//==================================================================
// Background thread for the concurrent remove/read race test. Blocks in
// meadow_rotary_encoder_read_data() for encoder 0 and reports whether it
// came back with a clean -ENODATA (encoder removed while waiting) instead
// of hanging or crashing.
void *rotary_encoder_test_blocking_read_kthread(int argc, char *argv[])
{
  rotEncReadData_t rotEncReadData;
  int ret;

  rotEncReadData.encoderNumb = 0;

  syslog(LOG_MTEST,
    "Rotary Encoder race test-background thread starting blocking read on encoder 0\n");

  ret = meadow_rotary_encoder_read_data(&rotEncReadData);

  if(ret == -ENODATA)
  {
    syslog(LOG_MTEST,
      "Rotary Encoder race test-PASS: background read returned -ENODATA (%d) "
      "after concurrent remove, no hang/crash\n", ret);
  }
  else if(ret < 0)
  {
    syslog(LOG_MTEST,
      "Rotary Encoder race test-background read returned unexpected error:%d\n",
      ret);
  }
  else
  {
    syslog(LOG_MTEST,
      "Rotary Encoder race test-background read returned data instead of "
      "being removed (change:%ld, total:%ld) - encoder may have actually "
      "rotated, re-run without touching it\n",
      rotEncReadData.encoderChange, rotEncReadData.encoderCount);
  }

  return NULL;
}

//==================================================================
// Stress-tests the activeReaders/pendingFree guard in
// rotenc_config_interrupt_remove(): starts a background thread blocked in
// meadow_rotary_encoder_read_data() for encoder 0, then removes encoder 0
// out from under it while it's still waiting. Watch syslog for the
// background thread's PASS/FAIL line.
void rotary_encoder_test_exercise_test(uint32_t userData)
{
  int ret;
  int pid;
  rotEncConfigParms_t *cfg = malloc(sizeof(rotEncConfigParms_t));
  if(cfg == NULL)
  {
    syslog(LOG_ERR, "Memory allocation failed\n");
    return;
  }

  syslog(LOG_MTEST,
    "Rotary Encoder race test-starting (userData:%lu)\n", userData);

  // For testing need to initialize the 2 GPIOs as inputs
  stm32_configgpio(ENCODER_PIN_CCM_D05_PB4_INPUT);
  stm32_configgpio(ENCODER_PIN_CCM_D06_PB13_INPUT);

  // Configure encoder 0 with a 500ms blocking delay
  cfg->encoderNumb = 0;
  cfg->isAddEncoder = true;
  cfg->portA = (ENCODER_PIN_CCM_D05_PB4_INPUT & 0xf0) >> 4;
  cfg->pinA =  (ENCODER_PIN_CCM_D05_PB4_INPUT & 0x0f);
  cfg->portB = (ENCODER_PIN_CCM_D06_PB13_INPUT & 0xf0) >> 4;
  cfg->pinB =  (ENCODER_PIN_CCM_D06_PB13_INPUT & 0x0f);
  cfg->resistorMode = 2;    // 2 = pull down
  cfg->delayMs = 500;

  ret = meadow_rotary_encoder_config(cfg);
  free(cfg);
  if(ret < 0)
  {
    syslog(LOG_MTEST,
      "Rotary Encoder race test-setup failed, ret:%d (encoder 0 may already "
      "be configured - remove it first with -v 11)\n", ret);
    return;
  }

  // Start the background thread that will block reading encoder 0
  pid = kthread_create("RotEncRaceTest", 100, 4096,
    (main_t) rotary_encoder_test_blocking_read_kthread, (char * const *)NULL);
  if(pid < 0)
  {
    syslog(LOG_MTEST,
      "Rotary Encoder race test-failed to create test thread, ret:%d\n", pid);
    return;
  }

  // Give the background thread time to enter its blocking read/poll loop
  // before removing the encoder out from under it. This is shorter than
  // the encoder's configured delayMs so the remove lands mid-poll, while
  // the background thread is asleep inside its wait.
  usleep(50 * 1000);

  syslog(LOG_MTEST,
    "Rotary Encoder race test-removing encoder 0 while background read is outstanding\n");

  stm32_unconfiggpio(ENCODER_PIN_CCM_D05_PB4_INPUT);
  stm32_unconfiggpio(ENCODER_PIN_CCM_D06_PB13_INPUT);

  cfg = malloc(sizeof(rotEncConfigParms_t));
  if(cfg == NULL)
  {
    syslog(LOG_ERR, "Memory allocation failed\n");
    return;
  }
  cfg->encoderNumb = 0;
  cfg->isAddEncoder = false;

  ret = meadow_rotary_encoder_config(cfg);
  free(cfg);
  if(ret < 0)
  {
    syslog(LOG_MTEST, "Rotary Encoder race test-remove returned ret:%d\n", ret);
  }

  syslog(LOG_MTEST,
    "Rotary Encoder race test-remove issued, watch for the background "
    "thread's PASS/FAIL log line\n");
}

#endif  // defined(CONFIG_ROTARY_ENCODER_TESTS)
