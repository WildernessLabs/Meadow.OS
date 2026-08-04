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

// In ./build.sh -u=gpiocount
// Then -p 23 -v <test number>
// e.g. meadow developer -p 23 -v 1 (config D05)

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <nuttx/config.h>
#include <meadow/meadow_gpio_counter.h>

// Only build CONFIG_GPIO_COUNTER_TESTS if configured
#if defined(CONFIG_GPIO_COUNTER_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)

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

/************************************************************************************
 * Public Functions
 ************************************************************************************/
// set developer -p 23 come here
// Testing entry point
void meadow_kt_gpio_counter_tests(uint32_t userData)
{
  int ret;
  gpioCntReadData_t gpioCntReadData;

  syslog(LOG_MTEST,
    "GPIO Counter tests received 'set developer -p 23 -v %lu'\n",
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
    case 1:   // GPIO Counter 0, D05
    gpio_counter_test_add_config(userData, 0,
        GPIO_CNT_PIN_CCM_D05_PB4_INPUT);
      break;
    case 2:   // GPIO Counter 5, D07
      gpio_counter_test_add_config(userData, 5,
        GPIO_CNT_PIN_CCM_D07_PB7_INPUT);
      break;
    case 3:   // GPIO Counter 7, D09
      gpio_counter_test_add_config(userData, 7,
        GPIO_CNT_PIN_CCM_D09_PI11_INPUT);
      break;
    case 4:   // GPIO Counter 8, D05
      // This configuration should fail, gpio counter out of range
      gpio_counter_test_add_config(userData, 8,
        GPIO_CNT_PIN_CCM_D05_PB4_INPUT);
      break;

    //-------------------------------------------------------------
    // Unconfigure / remove
    case 11:  // GPIO Counter 0
    case 12:  // GPIO Counter 5
    case 13:  // GPIO Counter 7
    case 14:  // GPIO Counter 8 This should fail
      gpio_counter_test_remove_config(userData);
      break;

    //-------------------------------------------------------------
    // Test reading count
    case 21:    // GPIO Counter 0
      gpioCntReadData.gpioCntrNumb = 0;
      ret = meadow_gpio_counter_read_data(&gpioCntReadData);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "GPIO Counter 0 returned error:%d\n", ret);
        break;
      }

      syslog(LOG_MTEST,
        "->GPIO Counter 0 - Changed by %ld since read, total:%ld\n",
        gpioCntReadData.gpioCountChange,
        gpioCntReadData.gpioCount);
      break;

    case 22:    // GPIO Counter 5
      gpioCntReadData.gpioCntrNumb = 5;
      ret = meadow_gpio_counter_read_data(&gpioCntReadData);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "GPIO Counter 5 returned error:%d\n", ret);
        break;
      }

      syslog(LOG_MTEST,
        "->GPIO Counter 5 - Changed by %ld since read, total:%ld\n",
        gpioCntReadData.gpioCountChange,
        gpioCntReadData.gpioCount);
      break;

    case 23:    // GPIO Counter 7
      gpioCntReadData.gpioCntrNumb = 7;
      ret = meadow_gpio_counter_read_data(&gpioCntReadData);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "GPIO Counter 7 returned error:%d\n", ret);
        break;
      }

      syslog(LOG_MTEST,
        "->GPIO Counter 7 - Changed by %ld since read total:%ld\n",
        gpioCntReadData.gpioCountChange,
        gpioCntReadData.gpioCount);
      break;

    //-------------------------------------------------------------
    // Reset/change count, gpio counter 0
    case 31:
      syslog(LOG_MTEST,
        "GPIO Counter test-setting gpio counter 0 to 0\n");
      ret = meadow_gpio_counter_set_count(0, 0);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "GPIO Counter 0 returned error:%d\n", ret);
        break;
      }
      syslog(LOG_MTEST, "->GPIO Counter 0 set to 0\n");
      break;

    // Test Resetting count, gpio counter 5
    case 32:
      syslog(LOG_MTEST, "GPIO Counter test-setting gpio counter 5 to 0\n");
      ret = meadow_gpio_counter_set_count(5, 0);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "GPIO Counter 5 returned error:%d\n", ret);
        break;
      }
      syslog(LOG_MTEST, "->GPIO Counter 5 set to 0\n");
      break;

    // Test Resetting count, gpio counter 7
    case 33:
      syslog(LOG_MTEST, "GPIO Counter test-setting gpio counter 7 to 0\n");
      ret = meadow_gpio_counter_set_count(7, 0);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "GPIO Counter 7 returned error:%d\n", ret);
        break;
      }
      syslog(LOG_MTEST, "->GPIO Counter 7 set to 0\n");
      break;

    //-------------------------------------------------------------
    // Function parameter null test
    case 41:    // GPIO Counter 0
      gpio_counter_config_test_null_parms(userData);
      break;

    default:
      syslog(LOG_MTEST, "GPIO Counter test-received unknown userData of %lu\n",
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

  syslog(LOG_MTEST, "GPIO Counter tests-configuring gpio counter %lu\n",
    gpioCntrNumb);

  // For testing need to initialize the 2 GPIOs as inputs
  stm32_configgpio(gpioPin);
  
  // Populate configuration struct
  cfg->gpioCntrNumb = gpioCntrNumb;   // GPIO Counter number 0 - 7
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

  syslog(LOG_MTEST, "GPIO Counter test-removing gpio counter %lu\n",
    gpioCntrNumb);

  // Need to unconfigure the 2 GPIOs used as inputs
  stm32_unconfiggpio(gpioPin);

  // Setup to unconfigure as .Net will do
  cfg->gpioCntrNumb = gpioCntrNumb;   // GPIO Counter number 0 - 7
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
    case 41:    // GPIO Counter 0
      // Call configuration function
      ret = meadow_gpio_counter_config(NULL);
      if(ret < 0)
      {
        syslog(LOG_MTEST, "Error:meadow_gpio_counter_config returned ret:%d\n",
          ret);
      }
      break;
  }

  syslog(LOG_MTEST, "GPIO Counter parameter test: gpio counter:%lu, ret:%d\n",
    gpioCntrNumb, ret);
}

#endif  // defined(CONFIG_GPIO_COUNTER_TESTS)
