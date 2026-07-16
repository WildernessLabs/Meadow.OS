/****************************************************************************
 * F7MeadowOS\kerneltests\meadow_gpio_counter_tests.c
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

// In ./build.sh -u=gpioCount
// Then -p 1? -v <test number>
// e.g. meadow developer -p 1? -v 1 (config D05)

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <nuttx/config.h>
#include <meadow/meadow_gpio_counter.h>

// Only build CONFIG_GPIO_COUNTER_TESTS if configured
//#if defined(CONFIG_GPIO_COUNTER_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
#if(1)

#include <meadow/hcom_shared_common.h>
#include "../hcom_nx/hcom_nx_common.h"
#include "stm32_gpio.h"   // stm32_configgpio
#include <nuttx/kthread.h>

#pragma message "(--) meadow_gpio_counter_tests.c"

// Diagnostic always as this is test code
// #define USE_MEADOW_DEBUG_HELPERS
#undef USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/

// D05 (PB4) for Version 2 Feather or CCM V1. Interrupt group isolation means
// that no pins can share the same Pin number for a gpio counter
#define GPIO_CNT_PIN_CCM_D05_PB4_INPUT   (GPIO_INPUT | GPIO_PULLDOWN | GPIO_PORTB | GPIO_PIN4)
#define GPIO_CNT_PIN_CCM_D06_PB13_INPUT  (GPIO_INPUT | GPIO_PULLDOWN | GPIO_PORTB | GPIO_PIN13)
#define GPIO_CNT_PIN_CCM_D07_PB7_INPUT   (GPIO_INPUT | GPIO_PULLDOWN | GPIO_PORTB | GPIO_PIN7)
#define GPIO_CNT_PIN_CCM_D08_PB6_INPUT   (GPIO_INPUT | GPIO_PULLDOWN | GPIO_PORTB | GPIO_PIN6)
#define GPIO_CNT_PIN_CCM_D09_PI11_INPUT  (GPIO_INPUT | GPIO_PULLDOWN | GPIO_PORTI | GPIO_PIN11)
#define GPIO_CNT_PIN_CCM_D10_PD5_INPUT   (GPIO_INPUT | GPIO_PULLDOWN | GPIO_PORTD | GPIO_PIN5)

/************************************************************************************
 * Private Data
 ************************************************************************************/

/************************************************************************************
 * Public Data
 ************************************************************************************/

/************************************************************************************
 * Private Function Prototypes
 ************************************************************************************/
static void gpio_counter_test_add_config(uint32_t testNumb,
  uint32_t gpioCntrNumb, uint32_t gpioPin);
static void gpio_counter_test_remove_config(uint32_t userData);
static void gpio_counter_config_test_null_parms(uint32_t userData);
// static void *gpio_counter_test_blocking_read_kthread(int argc, char *argv[]);

/************************************************************************************
 * Public Functions
 ************************************************************************************/
// set developer -p 18 come here
// Testing entry point
void meadow_kt_gpio_counter_tests(uint32_t userData)
{
  int ret;
  gpioCntReadData_t gpioCntReadData;

  syslog(LOG_MTEST,
    "GPIO Counter tests received 'set developer -p 1? -v %lu'\n",
    userData);

  switch(userData)
  {
    //-------------------------------------------------------------
    // Configuring the gpio counter with and without a delay
    // Note: The input pins defined are for F7FeatherV2 D05, D06, D07, D08.
    // This test code only defines the first 6 GPIOs, even though 8 GPIOs are
    // supported. And be careful to not use the pins used by syslog,
    // D12 and D13. And keep in mind GPIO groups.
    //
    case 1:   // GPIO Count 0 Delay Zero millisec, D05 & DO6
    gpio_counter_test_add_config(userData, 0,
        GPIO_CNT_PIN_CCM_D05_PB4_INPUT);
      break;
    case 2:   // GPIO Count 5 Delay 0 millisec, D07 & D08
      gpio_counter_test_add_config(userData, 5,
        GPIO_CNT_PIN_CCM_D07_PB7_INPUT);
      break;
    case 3:   // GPIO Count 7 Delay 0 millisec, D09 & D10
      gpio_counter_test_add_config(userData, 7,
        GPIO_CNT_PIN_CCM_D09_PI11_INPUT);
      break;
    case 4:   // GPIO Count 8 Delay 0 millisec, D05 & DO6
      // This configuration should fail, gpio counter out of range
      gpio_counter_test_add_config(userData, 8,
        GPIO_CNT_PIN_CCM_D05_PB4_INPUT);
      break;
    case 5:   // GPIO Count 0 Delay 1 millisec, D05 & DO6
      gpio_counter_test_add_config(userData, 0,
        GPIO_ADC1_IN13_0);
      break;
    case 6:   // GPIO Count 0 Delay 2 millisec, D05 & DO6
      gpio_counter_test_add_config(userData, 0,
        GPIO_CNT_PIN_CCM_D05_PB4_INPUT);
      break;
    case 7:   // GPIO Count 0 Delay 20 millisec, D05 & DO6
      gpio_counter_test_add_config(userData, 0,
        GPIO_CNT_PIN_CCM_D05_PB4_INPUT);
      break;
    case 8:   // GPIO Count 0 Delay 200 millisec, D05 & DO6
      gpio_counter_test_add_config(userData, 0,
        GPIO_CNT_PIN_CCM_D05_PB4_INPUT);
      break;
    case 9:   // GPIO Count 0 Delay 2000 millisec, D05 & DO6
      gpio_counter_test_add_config(userData, 0,
        GPIO_CNT_PIN_CCM_D05_PB4_INPUT);
      break;

    //-------------------------------------------------------------
    // Unconfigure
    case 11:  // GPIO Count 0
    case 12:  // GPIO Count 5
    case 13:  // GPIO Count 7
    case 14:  // GPIO Count 8 This should fail
      gpio_counter_test_remove_config(userData);
      break;

    //-------------------------------------------------------------
    // Test reading count
    case 21:    // GPIO Count 0
      gpioCntReadData.gpioCntrNumb = 0;
      ret = meadow_gpio_counter_read_data(&gpioCntReadData);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "GPIO Count 0 returned error:%d\n", ret);
        break;
      }

      syslog(LOG_MTEST,
        "GPIO Count 0 - Changed by %ld since read, total:%ld\n",
        gpioCntReadData.gpioCountChange,
        gpioCntReadData.gpioCount) ;
      break;

    case 22:    // GPIO Count 5
      gpioCntReadData.gpioCntrNumb = 5;
      ret = meadow_gpio_counter_read_data(&gpioCntReadData);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "GPIO Count 5 returned error:%d\n", ret);
        break;
      }

      syslog(LOG_MTEST,
        "GPIO Count 5 - Changed by %ld since read, total:%ld\n",
        gpioCntReadData.gpioCountChange,
        gpioCntReadData.gpioCount) ;
      break;

    case 23:    // GPIO Count 7
      gpioCntReadData.gpioCntrNumb = 7;
      ret = meadow_gpio_counter_read_data(&gpioCntReadData);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "GPIO Count 7 returned error:%d\n", ret);
        break;
      }

      syslog(LOG_MTEST,
        "GPIO Count 7 - Changed by %ld since read total:%ld\n",
        gpioCntReadData.gpioCountChange,
        gpioCntReadData.gpioCount) ;
      break;

    //-------------------------------------------------------------
    // Reset/change count, gpio counter 0
    case 31:
      syslog(LOG_MTEST,
        "GPIO Count test-setting gpio counter 0 to 0 (%lu)\n",
        userData);
      ret = meadow_gpio_counter_set_count(0, 0);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "GPIO Count 0 returned error:%d\n", ret);
        break;
      }
      syslog(LOG_MTEST, "GPIO Count 0 set to 0\n");
      break;

    // Test Resetting count, gpio counter 5
    case 32:
      syslog(LOG_MTEST, "GPIO Count test-setting gpio counter 5 to -999999 (%lu)\n",
        userData);
      ret = meadow_gpio_counter_set_count(5, -999999);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "GPIO Count 5 returned error:%d\n", ret);
        break;
      }
      syslog(LOG_MTEST, "GPIO Count 5 set to -999999\n");
      break;

    // Test Resetting count, gpio counter 7
    case 33:
      syslog(LOG_MTEST, "GPIO Count test-setting gpio counter 7 to 0 (%lu)\n",
        userData);
      ret = meadow_gpio_counter_set_count(7, 0);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "GPIO Count 7 returned error:%d\n", ret);
        break;
      }
      syslog(LOG_MTEST, "GPIO Count 7 set to 0\n");
      break;

    //-------------------------------------------------------------
    // Function parameter null test
    case 41:    // GPIO Count 0
      gpio_counter_config_test_null_parms(userData);
      break;

    // //-------------------------------------------------------------
    // // Concurrent remove-while-blocking-read race test (gpio counter 0). Starts
    // // a background thread blocked in meadow_gpio_counter_read_data(),
    // // then removes gpio counter 0 out from under it. Expect the background
    // // thread's syslog line to report a clean -ENODATA, not a hang or crash.
    // case 51:
    //   gpio_counter_test_exercise_test(userData);
    //   break;

    default:
      syslog(LOG_MTEST, "GPIO Count test-received unknown userData of %lu\n",
        userData);
      break;
  }
}

/************************************************************************************
 * Private Functions
 ************************************************************************************/
void gpio_counter_test_add_config(uint32_t testNumb,
  uint32_t gpioCntrNumb, uint32_t gpioPin)
{
  // Setup a gpio counter
  int ret;

  gpioCntConfigParms_t* cfg = malloc(sizeof(gpioCntConfigParms_t));
  if(cfg == NULL)
  {
    syslog(LOG_ERR, "Memory allocation failed\n");
    return;
  }

  syslog(LOG_MTEST, "GPIO Count tests-configuring gpio counter %lu\n",
    gpioCntrNumb);

  // For testing need to initialize the 2 GPIOs as inputs
  stm32_configgpio(gpioPin);
  
  // Populate configuration struct
  cfg->gpioCntrNumb = gpioCntrNumb;   // GPIO Count number 0 - 7
  cfg->isAddGpioCounter = true;         // New gpio counter
  cfg->gpioPortInfo = (gpioPin & 0xf0) >> 4;
  cfg->gpioPinInfo =  (gpioPin & 0x0f);
  cfg->resistorMode = 2;    // 2 = pull down

  // Call configuration function also used by managed code
  ret = meadow_gpio_counter_config(cfg);
  if(ret < 0)
  {
    syslog(LOG_MTEST, "Error:meadow_gpio_counter_config returned ret:%d\n",
      ret);
  }

  free (cfg);
}

//============================================================================
void gpio_counter_test_remove_config(uint32_t userData)
{
  int ret;
  uint32_t gpioCntrNumb = 9;  // Invalid
  uint32_t gpioPin = 0;

  gpioCntConfigParms_t* cfg = malloc(sizeof(gpioCntConfigParms_t));
  if(cfg == NULL)
  {
    syslog(LOG_ERR, "Memory allocation failed\n");
    return;
  }

  switch(userData)
  {
    case 11:
      gpioCntrNumb = 0;
      gpioPin = GPIO_CNT_PIN_CCM_D05_PB4_INPUT;
      break;
    case 12:
      gpioCntrNumb = 5;
      gpioPin = GPIO_CNT_PIN_CCM_D07_PB7_INPUT;
      break;
    case 13:
      gpioCntrNumb = 7;
      gpioPin = GPIO_CNT_PIN_CCM_D09_PI11_INPUT;
      break;
    case 14:
      gpioCntrNumb = 8;    // Invalid
      gpioPin = GPIO_CNT_PIN_CCM_D05_PB4_INPUT;
      break;
  }
  syslog(LOG_MTEST, "GPIO Count test-removing gpio counter %lu\n",
    gpioCntrNumb);

  // Need to unconfigure the 2 GPIOs used as inputs
  stm32_unconfiggpio(gpioPin);

  // Setup to unconfigure as .Net will do
  cfg->gpioCntrNumb = gpioCntrNumb;   // GPIO Count number 0 - 7
  cfg->isAddGpioCounter = false;        // Remove gpio counter

  // Call meadow_rotary_encoder.c's configuration function, used by
  // managed code
  ret = meadow_gpio_counter_config(cfg);
  if(ret < 0)
  {
    syslog(LOG_MTEST, "Error:meadow_gpio_counter_config returned ret:%d\n",
      ret);
  }

  free (cfg);
}

//==================================================================
// Pass in bad parameters
void gpio_counter_config_test_null_parms(uint32_t userData)
{
  // Setup a gpio counter
  int ret = 0;

  uint32_t gpioCntrNumb = 0;

  switch(userData)
  {
    // NULL config test, Should fail
    case 41:    // GPIO Count 0
      // Call configuration function
      ret = meadow_gpio_counter_config(NULL);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "Error:meadow_gpio_counter_config returned ret:%d\n",
          ret);
      }
      break;
  }

  syslog(LOG_MTEST, "GPIO Count parameter test: gpio counter:%lu, ret:%d\n",
    gpioCntrNumb, ret);
}

// //==================================================================
// // Background thread for the concurrent remove/read race test. Blocks in
// // meadow_gpio_counter_read_data() for gpio counter 0 and reports whether it
// // came back with a clean -ENODATA (gpio counter removed while waiting) instead
// // of hanging or crashing.
// void *gpio_counter_test_blocking_read_kthread(int argc, char *argv[])
// {
//   gpioCntReadData_t gpioCntReadData;
//   int ret;

//   gpioCntReadData.gpioCntrNumb = 0;

//   syslog(LOG_MTEST,
//     "GPIO Count race test-background thread starting blocking read on gpio counter 0\n");

//   ret = meadow_gpio_counter_read_data(&gpioCntReadData);

//   if(ret == -ENODATA)
//   {
//     syslog(LOG_MTEST,
//       "GPIO Count race test-PASS: background read returned -ENODATA (%d) "
//       "after concurrent remove, no hang/crash\n", ret);
//   }
//   else if(ret < 0)
//   {
//     syslog(LOG_MTEST,
//       "GPIO Count race test-background read returned unexpected error:%d\n",
//       ret);
//   }
//   else
//   {
//     syslog(LOG_MTEST,
//       "GPIO Count race test-background read returned data instead of "
//       "being removed (change:%ld, total:%ld) - gpio counter may have actually "
//       "rotated, re-run without touching it\n",
//       gpioCntReadData.gpioCountChange, gpioCntReadData.gpioCount);
//   }

//   return NULL;
// }

#endif  // defined(CONFIG_GPIO_COUNTER_TESTS)
