/****************************************************************************
 * Meadow.OS/kerneltests/ana_to_dig_conv_tests.c
 * 
 *   Copyright (C) 2023, 2025 Wilderness Labs. All rights reserved.
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/arch.h>         // up_enable_irq
#include "arm_internal.h"       // getreg32 & putreg32
#include <arch/stm32f7/chip.h>
#include "stm32_gpio.h"
#include <nuttx/kthread.h>
#include "stm32_adc.h"
#include "stm32_rcc.h"
#include "stm32_dma.h"
#include "hardware/stm32f76xx77xx_memorymap.h"
#include <meadow/hcom_shared_common.h>
#include <meadow/meadow_syscall_support.h>
#include "hcom_nx/hcom_nx_common.h"

#if defined (CONFIG_ADC_TESTS) || defined (CONFIG_ALL_MEADOW_TESTS)

#pragma message "(--) ana_to_dig_conv_tests.c included"

#ifndef CONFIG_STM32F7_DMA2
#error "Meadow ADC with DMA requires CONFIG_STM32F7_DMA2"
#endif

//
//  Uncomment the line below to include the debug helper file.
//
// #define USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>

// #pragma GCC optimize "Og"

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/

// Of these PA4 and PA5 are the only ones that can be used for DAC
#define GPIO_V2_A00_IN4_PA4         (GPIO_ANALOG|GPIO_PORTA|GPIO_PIN4)
#define GPIO_V2_A01_IN5_PA5         (GPIO_ANALOG|GPIO_PORTA|GPIO_PIN5)
#define GPIO_V2_A02_IN3_PA3         (GPIO_ANALOG|GPIO_PORTA|GPIO_PIN3)
#define GPIO_V2_A03_IN8_PB0         (GPIO_ANALOG|GPIO_PORTB|GPIO_PIN0)
#define GPIO_V2_A04_IN9_PB1         (GPIO_ANALOG|GPIO_PORTB|GPIO_PIN1)
#define GPIO_V2_A05_IN10_PC0        (GPIO_ANALOG|GPIO_PORTC|GPIO_PIN0)

// This is NOT a valid analog input pin. It can be used for testing
#define GPIO_V2_A0x_INx_PA9         (GPIO_ANALOG|GPIO_PORTA|GPIO_PIN9)

// This is fixed for 2 bytes for the ADC results
#define ADC_TESTS_DMA_BYTES_PER_CONVERSION (2)

// Adjust for different tests. Values from 1 to 16 are valid
#define ADC_TESTS_MAX_GPIO_COUNT (16)

#define ADC_TESTS_RESULT_BUFFER_SIZE (ADC_TESTS_MAX_GPIO_COUNT * \
                        ADC_TESTS_DMA_BYTES_PER_CONVERSION)

/************************************************************************************
 * Private Data
 ************************************************************************************/

static double _voltageResultBuf[ADC_TESTS_RESULT_BUFFER_SIZE];
static uint32_t _userData;
static uint32_t _numbGpioActive;

enum
{
  unknown           = 0,
  configure0Gpio    = 1,    // Config 0 GPIOs
  configure1Gpio    = 2,    // Config 1 GPIO
  configure8Gpio    = 3,    // Config 8 GPIOs
  configure16Gpio   = 4,    // Config 16 GPIOs
  readGpioAnaOnce   = 5,
  readTempBatOnce   = 6,
  readGpioAnaOften  = 7,
  readTempBatOften  = 8,
  verifySeqRegs     = 9,    // Configure 16 GPIOs, verify SQR1/2/3 contents
};

/************************************************************************************
 * Private Function Prototypes
 ************************************************************************************/

static void adc_test_initialize(uint32_t numberGpio);
static void adc_test_verify_seq_regs(void);
static int adc_test_create_testing_thread(void);
static void *adc_test_kthread_func(int argc, char *argv[]);
static void show_all_data_in_buffer(char *headerText, double dataBuffer[],
              uint32_t dataBufElements);

/************************************************************************************
 * Private Functions
 ************************************************************************************/
// ADC tests enter here
void meadow_kt_adc_tests(uint32_t userData)
{
  int ret;
  double batteryVoltage;
  double temperatureValue;

  _userData = userData;

  syslog(LOG_MTEST, "%s@%d-Entered meadow_kt_adc_tests, userData:%lu\n",
    __FILE__, __LINE__, userData);
  usleep(20 * 1000);
  
  switch(userData)
  {
    case configure0Gpio: // (1)
      // Initialize test code
      syslog(LOG_MTEST, "--------------- configure-0-Gpio ---------------\n");
      usleep(20 * 1000);
      adc_test_initialize(0);
      _numbGpioActive = 0;
      break;

    case configure1Gpio: // (2)
      // Initialize test code
      syslog(LOG_MTEST, "--------------- configure-1-Gpio ---------------\n");
      usleep(20 * 1000);
      adc_test_initialize(1);
      _numbGpioActive = 1;
      break;
    case configure8Gpio: // (3)
      // Initialize test code
      syslog(LOG_MTEST, "--------------- configure-8-Gpio ---------------\n");
      usleep(20 * 1000);
      adc_test_initialize(8);
      _numbGpioActive = 8;
      break;

    case configure16Gpio: // (4)
      // Initialize test code
      syslog(LOG_MTEST, "--------------- configure-16-Gpio ---------------\n");
      usleep(20 * 1000);
      adc_test_initialize(ADC_TESTS_MAX_GPIO_COUNT);
      _numbGpioActive = ADC_TESTS_MAX_GPIO_COUNT;
      break;

      // Read the data here after configuring
    case readGpioAnaOnce: // (5)
      syslog(LOG_MTEST, "--------------- readGpioAnaOnce ---------------\n");
      usleep(20 * 1000);
      ret = meadow_adc_read_values();
      if(ret < 0)
      {
        syslog(LOG_ERR, "Error:GPIO conversion, ret:%d\n", ret);
      }
      // Show information in the buffer
      _numbGpioActive = 1;
      show_all_data_in_buffer("TestApp", _voltageResultBuf, _numbGpioActive);
      break;

    case readTempBatOnce: // (6)
      syslog(LOG_MTEST, "--------------- readTempBatOnce ---------------\n");
      // Read the internal values of battery and temperature once
      ret = meadow_adc_read_temp_vbat(&batteryVoltage, &temperatureValue);
      if(ret < 0)
      {
        syslog(LOG_ERR, "Error:Internal vbat and temp conversion, ret:%d\n", ret);
      }
      syslog(LOG_MTEST, "TestApp:Vbat:%.3f, Temp:%.3f\n",
        batteryVoltage, temperatureValue);
      break;
    
    case readGpioAnaOften: // (7)
      syslog(LOG_MTEST, "--------------- readGpioAnaOften ---------------\n");
      usleep(20 * 1000);
      // Create a thread to test standard GPIO ADC operation often
      adc_test_create_testing_thread();
      break;

    case readTempBatOften: // (8)
      syslog(LOG_MTEST, "--------------- readTempBatOften ---------------\n");
      usleep(20 * 1000);
      // Create a thread to test getting temperature & Vbat often
     adc_test_create_testing_thread();
      break;

    case verifySeqRegs: // (9)
      syslog(LOG_MTEST, "--------------- verifySeqRegs ---------------\n");
      usleep(20 * 1000);
      adc_test_verify_seq_regs();
      break;

    default:
      syslog(LOG_MTEST, "Undefined test for meadow_kt_adc_tests, userData:%lu\n",
        userData);
      break;
  }
}

//================================================================
// This part of the code could be called > 1 time when it supports more
// than ADC1 for debugging. However the ADC reset done via RCC will only
// need to be done once.
// nuttx/arch/arm/src/stm32f7/chip/stm32f74xx77xx_adc.h
void adc_test_initialize(uint32_t numberGpio)
{
  int ret;
  uint8_t gpioList[ADC_TESTS_MAX_GPIO_COUNT];   // Might as well prepare for max

  syslog(LOG_MTEST, "--> Entered adc_test_initialize()\n");
  usleep(20 * 1000);

  // To test need to prepare a few things
  // A list of input points. Note points can be used more than once

  // These need to be configured here for testing, but only for testing.
  // For normal operation, they'll be configured by Meadow.Core
  stm32_configgpio(GPIO_V2_A00_IN4_PA4);
  stm32_configgpio(GPIO_V2_A01_IN5_PA5);
  stm32_configgpio(GPIO_V2_A02_IN3_PA3);
  stm32_configgpio(GPIO_V2_A03_IN8_PB0);
  stm32_configgpio(GPIO_V2_A04_IN9_PB1);
  stm32_configgpio(GPIO_V2_A05_IN10_PC0);

  // We'll use the first of these based on the ADC_TESTS_MAX_GPIO_COUNT
  // value. If it is 1 then only used the first entry. If it is 6, we'll use
  // the first 6 entries. For more than 6 we must reuse the previous analog
  // GPIOs, all the way to 16 entries.
  //
  // The Nuttx GPIO Config holds the Port in bits 7:4 and the Pin in bits 3:0
  // this is all we need to setup the ADC
  gpioList[0]  = GPIO_V2_A00_IN4_PA4  & 0x000000ff;
  gpioList[1]  = GPIO_V2_A01_IN5_PA5  & 0x000000ff;
  gpioList[2]  = GPIO_V2_A02_IN3_PA3  & 0x000000ff;
  gpioList[3]  = GPIO_V2_A03_IN8_PB0  & 0x000000ff;
  gpioList[4]  = GPIO_V2_A04_IN9_PB1  & 0x000000ff;
  gpioList[5]  = GPIO_V2_A05_IN10_PC0 & 0x000000ff;
  // Entries 6-15 used to repeat entries 0-5/0-3 above, on the assumption
  // that only 6 GPIOs are physically wired for ADC on this board. But that
  // made the sequence registers 2 and 1 always get filled with the same
  // values as register 3, which masked Issue #403's finding that the calls
  // filling SQR2/SQR1 never advanced past the caller's first 6 list entries
  // -- the bug and the (buggy) test produced identical output. These are
  // not physically wired to anything meaningful, but every value here is a
  // genuinely distinct, valid ADC channel (see _gpioAdcChanMap in
  // meadow_adc.c), so SQR2/SQR1 each get content that could only be correct
  // if the pointer into userGpioList was actually advanced.
  gpioList[6]  = 0x00;  // PA0  -> ADC channel 0
  gpioList[7]  = 0x01;  // PA1  -> ADC channel 1
  gpioList[8]  = 0x02;  // PA2  -> ADC channel 2
  gpioList[9]  = 0x06;  // PA6  -> ADC channel 6
  gpioList[10] = 0x07;  // PA7  -> ADC channel 7
  gpioList[11] = 0x21;  // PC1  -> ADC channel 11

  gpioList[12] = 0x22;  // PC2  -> ADC channel 12
  gpioList[13] = 0x23;  // PC3  -> ADC channel 13
  gpioList[14] = 0x24;  // PC4  -> ADC channel 14
  gpioList[15] = 0x25;  // PC5  -> ADC channel 15

  // syslog(LOG_MTEST, "----- gpioList contains -----\n");
  // hcom_nx_diag_print_buffer(gpioList, 16, 1);
  
  // Calling configuration API to set things up
  ret = meadow_adc_configure(gpioList, numberGpio, _voltageResultBuf);
  if(ret < 0)
  {
    syslog(LOG_MTEST, "%s@%d-Error:meadow_adc_configure() returned:%d, errno:%d\n",
      __FILE__, __LINE__, ret, errno);
    usleep(20 * 1000);
  }
}

//===================================================================
// Configures 16 GPIOs using channels 0-15 in ascending order, then reads
// back the ADC_SQR1/2/3 registers and confirms each sequence slot holds
// the expected channel number. Unlike adc_test_initialize()'s list (which
// prioritizes exercising the board's physically-wired analog pins),
// ascending channel order here makes the expected register contents known
// ahead of time -- SQR3 should hold channels 0-5, SQR2 channels 6-11, and
// SQR1 channels 12-15, each in slot order -- so this directly catches a
// regression of Issue #403's finding (the SQR2/SQR1 fill calls not
// advancing past the first 6 entries of the caller's GPIO list) without
// needing a human to read a register dump and do the bit math by hand.
static void adc_test_verify_seq_regs(void)
{
  uint8_t seqTestGpioList[ADC_TESTS_MAX_GPIO_COUNT] =
  {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05,   // PA0-PA5        -> channels 0-5
    0x06, 0x07, 0x10, 0x11, 0x20, 0x21,   // PA6,PA7,PB0,PB1,PC0,PC1 -> 6-11
    0x22, 0x23, 0x24, 0x25                // PC2-PC5        -> channels 12-15
  };
  double resultBuf[ADC_TESTS_MAX_GPIO_COUNT];
  int ret;
  uint32_t regval;
  uint32_t chan;
  bool pass = true;

  ret = meadow_adc_configure(seqTestGpioList, ADC_TESTS_MAX_GPIO_COUNT, resultBuf);
  if(ret < 0)
  {
    syslog(LOG_MTEST, "FAIL:verifySeqRegs - meadow_adc_configure() ret:%d\n", ret);
    return;
  }

  // SQR3 (indices 0-5) should hold channels 0-5 in slots 1-6.
  regval = getreg32(STM32_ADC1_BASE + STM32_ADC_SQR3_OFFSET);
  for(int slot = 0; slot < 6; slot++)
  {
    chan = (regval >> (slot * 5)) & 0x1f;
    if(chan != (uint32_t)slot)
    {
      syslog(LOG_MTEST, "FAIL:verifySeqRegs - SQR3 slot %d expected chan %d, got %" PRIu32 "\n",
                slot, slot, chan);
      pass = false;
    }
  }

  // SQR2 (indices 6-11) should hold channels 6-11 in slots 1-6.
  regval = getreg32(STM32_ADC1_BASE + STM32_ADC_SQR2_OFFSET);
  for(int slot = 0; slot < 6; slot++)
  {
    chan = (regval >> (slot * 5)) & 0x1f;
    if(chan != (uint32_t)(6 + slot))
    {
      syslog(LOG_MTEST, "FAIL:verifySeqRegs - SQR2 slot %d expected chan %d, got %" PRIu32 "\n",
                slot, 6 + slot, chan);
      pass = false;
    }
  }

  // SQR1 (indices 12-15) should hold channels 12-15 in slots 1-4.
  regval = getreg32(STM32_ADC1_BASE + STM32_ADC_SQR1_OFFSET);
  for(int slot = 0; slot < 4; slot++)
  {
    chan = (regval >> (slot * 5)) & 0x1f;
    if(chan != (uint32_t)(12 + slot))
    {
      syslog(LOG_MTEST, "FAIL:verifySeqRegs - SQR1 slot %d expected chan %d, got %" PRIu32 "\n",
                slot, 12 + slot, chan);
      pass = false;
    }
  }

  syslog(LOG_MTEST, "%s:verifySeqRegs - sequence register contents %s expected\n",
        pass ? "PASS" : "FAIL", pass ? "match" : "do not match");
}

//===================================================================
// Create a thread for testing
static int adc_test_create_testing_thread(void)
{
  int thread_id = kthread_create("ADC Test",
                                100,      // Priority
                                4096,     // Stack
                                (main_t) adc_test_kthread_func,
                                (char *const *) NULL);
  if (thread_id <= 0)
  {
    syslog(LOG_ERR, "%s@%d-Creation of %s kthread FAILED\n",
              __FILE__, __LINE__, "ADC Test Thread");
    return -ENOEXEC;
  }
  return OK;
}

//===================================================================
void *adc_test_kthread_func(int argc, char *argv[])
{
  int ret;
  double batteryVoltage;
  double temperatureValue;

  // Run the test. This is a soak test intended to run indefinitely (until
  // the device is reset), so the loop is deliberately unbounded.
  for(int chkCnt = 0; ; chkCnt++)
  {
    if(_userData == readGpioAnaOften)
    {
      // Calling meadow_adc.c API to indicate conversion needed
      // This thread will wait until buffer is full
      ret = meadow_adc_read_values();
      if(ret < 0)
      {
        syslog(LOG_ERR, "%s@%d-Call to meadow_adc_read_values() failed\n",
                  __FILE__, __LINE__);
      }

      // Show information in the buffer
      char textBuf[64];
      snprintf_chk(textBuf, sizeof(textBuf), "%04d-%s", chkCnt + 1, "Test App");

      _numbGpioActive = 1;
      show_all_data_in_buffer(textBuf, _voltageResultBuf, _numbGpioActive);
    }
    else if(_userData == readTempBatOften)
    {
      // Read the internal values of battery and temperature
      ret =  meadow_adc_read_temp_vbat(&batteryVoltage, &temperatureValue);
      if(ret < 0)
      {
        syslog(LOG_ERR, "Error:Internal vbat and temp conversion, ret:%d\n",
          ret);
      }
      
      syslog(LOG_MTEST, "TestApp: Vbat:%f, Temp:%f\n",
        batteryVoltage, temperatureValue);
    }
    else
    {
      syslog(LOG_MTEST, "ADC Test Thread should not be running\n");
    }

    // Just keep looping
    usleep(3000 * 1000);
  }
  return NULL;
}

//==========================================================================
// Output the information from the data buffer
void show_all_data_in_buffer(char *headerText, double dataBuffer[],
                            uint32_t dataBufElements)
{
#define DMA_ISR_DISP_MAX_PER_ROW (4)    // elements / row
#define DMA_ISR_DISP_VALUE_LEN (10)     // Doubles values take 5-9 char
#define DMA_ISR_DISP_LEADER_LEN (9)     // Addr takes 9 chars

  uint32_t dmaBuffOff = 0;
  int remainingElements = dataBufElements;
  int columnCnt;
  int lineBuffOff;
  int disp_max_per_row = DMA_ISR_DISP_MAX_PER_ROW;

  if(disp_max_per_row > dataBufElements)
    disp_max_per_row = dataBufElements;

  int disp_char_per_row = (disp_max_per_row * DMA_ISR_DISP_VALUE_LEN);
  int disp_total_line_len = disp_char_per_row + DMA_ISR_DISP_LEADER_LEN;

  char *lineBuff = malloc(disp_total_line_len);    // Room for NULL
  if(lineBuff == NULL)
  {
    syslog(LOG_ERR, "%s@%d-malloc failed\n", __FILE__, __LINE__);
    return;
  }

  // Fill unused spaces with space
  memset(lineBuff, 0x20, disp_total_line_len);

  do
  {
    // Add address of offset to beginning of line
    lineBuffOff = snprintf(lineBuff, disp_total_line_len, "%08" PRIx32 " ", dmaBuffOff);

    // Clamp this row's element count to what's actually left, so a
    // non-multiple-of-row-width element count doesn't read past the end
    // of dataBuffer on the final (partial) row.
    int row_elements = disp_max_per_row;
    if(row_elements > remainingElements)
      row_elements = remainingElements;

    // Build a full row of data then print it
    for(columnCnt = 0; columnCnt < row_elements; columnCnt++)
    {
      // Buffer size available for this value
      int bufSizeForCall = disp_char_per_row - (columnCnt * DMA_ISR_DISP_VALUE_LEN);

      // This will add a terminating NULL in the buffer
      int lineLen = snprintf(&lineBuff[lineBuffOff],
                bufSizeForCall,   // Buffer size
                "%.3f ", dataBuffer[dmaBuffOff++]);

      // snprintf() returns the length that would have been written had the
      // buffer been big enough, not the actual (possibly truncated) written
      // length. Clamp to what was actually written so the overwrite below
      // can't land past lineBuff's allocation.
      if(lineLen >= bufSizeForCall)
        lineLen = bufSizeForCall - 1;

      // On the last entry don't override the NULL, it's terminating the line
      if((columnCnt+ 1) != row_elements)
        lineBuff[lineBuffOff + lineLen] = 0x20;    // Overwrite unwanted NULL

      lineBuffOff += DMA_ISR_DISP_VALUE_LEN;    // Fixed spacing
      remainingElements--;
    }

    // Display this line of text
    syslog(LOG_MTEST, "%s:%s\n", headerText, lineBuff);

    // Line by line show entire buffer
  } while (remainingElements > 0);
  
  free(lineBuff);
}
#endif    // #if defined (CONFIG_ADC_TESTS)
