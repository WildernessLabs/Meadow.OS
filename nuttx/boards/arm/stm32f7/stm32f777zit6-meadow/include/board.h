/****************************************************************************
 * boards/arm/stm32f7/stm32f777zit6-meadow/include/board.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __BOARDS_ARM_STM32F777ZIT6_MEADOW_INCLUDE_BOARD_H
#define __BOARDS_ARM_STM32F777ZIT6_MEADOW_INCLUDE_BOARD_H

/*
* It looks like this file was originally copied from
* nuttx/boards/arm/stm32f7/stm32f769i-disco/include/board.h or a one like it.
* However, boards like nuttx/boards/arm/stm32f7/nucleo-f767zi/include/board.h
* don't even have a CONFIG_STM32F7_OTGFS configuration option.
*
* This file has been modified to use the nucleo-f767zi scheme for Meadow Rebase.
*/

// #pragma message "(--) nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/include/board.h"

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#  include <stdint.h>
#endif

/* Do not include STM32F7 header files here */

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

// This is temporary while confirming 216 MHz works.
// Set to '0' to revert to 192 MHz.
#define MEADOW_TRY_216_MHz_CLOCK (1)

/* Clocking *****************************************************************/

/* The Meadow board provides the following clock sources:
 *
 *   X2:  25 MHz oscillator for STM32F777ZIT6 microcontroller
 *   and Ethernet PHY.
 *   X1:  No crystal for STM32F777ZIT6 embedded RTC
 *
 * So we have these clock source available within the STM32
 *
 *   HSI: 16 MHz RC factory-trimmed
 *   LSI: 32 KHz RC
 *   HSE: On-board crystal frequency is 25MHz
 *   LSE: None
 */

#define STM32_BOARD_XTAL        25000000ul

#define STM32_HSI_FREQUENCY     16000000ul
#define STM32_LSI_FREQUENCY     32000
#define STM32_HSE_FREQUENCY     STM32_BOARD_XTAL
#define STM32_LSE_FREQUENCY     32768


/* Main PLL Configuration.
 *
 * PLL source is HSE = 25,000,000
 *
 * PLL_VCO = (STM32_HSE_FREQUENCY / PLLM) * PLLN
 * Subject to:
 *
 *     2 <= PLLM <= 63
 *   192 <= PLLN <= 432
 *   192 MHz <= PLL_VCO <= 432MHz
 *
 * SYSCLK  = PLL_VCO / PLLP
 * Subject to
 *
 *   PLLP = {2, 4, 6, 8}
 *   SYSCLK <= 216 MHz
 *
 * USB OTG FS, SDMMC and RNG Clock = PLL_VCO / PLLQ
 * Subject to
 *   The USB OTG FS requires a 48 MHz clock to work correctly. The SDMMC
 *   and the random number generator need a frequency lower than or equal
 *   to 48 MHz to work correctly.
 *
 * 2 <= PLLQ <= 15
 */
 
#if (MEADOW_TRY_216_MHz_CLOCK == 1)
/* USB OTG FS clock (= SDMMCCLK = RNGCLK) must be 48 MHz
 *
 * PLL_VCO = (25,000,000 / 25) * 432 = 432 MHz
 * SYSCLK  = 432 MHz / 2 = 216 MHz
 * USB OTG FS, SDMMC and RNG Clock = 432 MHz / 9 = 48 MHz
 * DSI CLK = PLL_VCO / PLLR = 432 / 8 = 54 MHz
 */

#define STM32_PLLCFG_PLLM       RCC_PLLCFG_PLLM(25)
#define STM32_PLLCFG_PLLN       RCC_PLLCFG_PLLN(432)
#define STM32_PLLCFG_PLLP       RCC_PLLCFG_PLLP_2
#define STM32_PLLCFG_PLLQ       RCC_PLLCFG_PLLQ(9)

#define STM32_VCO_FREQUENCY     ((STM32_HSE_FREQUENCY / 25) * 432)
#define STM32_SYSCLK_FREQUENCY  (STM32_VCO_FREQUENCY / 2)   // 432/2=216 MHz
#define STM32_OTGFS_FREQUENCY   (STM32_VCO_FREQUENCY / 9)   // 432/9=48 MHz

#else   // #if (MEADOW_TRY_216_MHz_CLOCK == 1)

#if defined(CONFIG_STM32F7_OTGFS)
 *
 * PLL_VCO = (25,000,000 / 25) * 384 = 384 MHz
 * SYSCLK  = 384 MHz / 2 = 192 MHz
 * USB OTG FS, SDMMC and RNG Clock = 384 MHz / 8 = 48MHz
 * DSI CLK = PLL_VCO / PLLR = 384 / 7 = 54,86 MHz
 */

#define STM32_PLLCFG_PLLM       RCC_PLLCFG_PLLM(25)
#define STM32_PLLCFG_PLLN       RCC_PLLCFG_PLLN(384)
#define STM32_PLLCFG_PLLP       RCC_PLLCFG_PLLP_2
#define STM32_PLLCFG_PLLQ       RCC_PLLCFG_PLLQ(8)
#define STM32_PLLCFG_PLLR       RCC_PLLCFG_PLLR(7)

#define STM32_VCO_FREQUENCY     ((STM32_HSE_FREQUENCY / 25) * 384)
#define STM32_SYSCLK_FREQUENCY  (STM32_VCO_FREQUENCY / 2)
#define STM32_OTGFS_FREQUENCY   (STM32_VCO_FREQUENCY / 8)

#elif defined(CONFIG_STM32F7_SDMMC1) || defined(CONFIG_STM32F7_SDMMC2) || defined(CONFIG_STM32F7_RNG)
/* SDMMCCLK (= USB OTG FS clock = RNGCLK) should be <= 48MHz
 *
 * PLL_VCO = (25,000,000 / 25) * 432 = 432 MHz
 * SYSCLK  = 432 MHz / 2 = 216 MHz
 * USB OTG FS, SDMMC and RNG Clock = 432 MHz / 10 = 43.2 MHz
 * DSI CLK = PLL_VCO / PLLR = 432 / 8 = 54 MHz
 */

#define STM32_PLLCFG_PLLM       RCC_PLLCFG_PLLM(25)
#define STM32_PLLCFG_PLLN       RCC_PLLCFG_PLLN(432)
#define STM32_PLLCFG_PLLP       RCC_PLLCFG_PLLP_2
#define STM32_PLLCFG_PLLQ       RCC_PLLCFG_PLLQ(10)
#define STM32_PLLCFG_PLLR       RCC_PLLCFG_PLLR(8)

#define STM32_VCO_FREQUENCY     ((STM32_HSE_FREQUENCY / 25) * 432)
#define STM32_SYSCLK_FREQUENCY  (STM32_VCO_FREQUENCY / 2)
#define STM32_OTGFS_FREQUENCY   (STM32_VCO_FREQUENCY / 10)

#else
/*
 * Same as above. Works with OTGFS and allows STM32F7 to run at 216HHz instead of 192MHz
 *
 * PLL_VCO = (25,000,000 / 25) * 432 = 432 MHz
 * SYSCLK  = 432 MHz / 2 = 216 MHz
 * USB OTG FS, SDMMC and RNG Clock = 432 MHz / 10 = 43.2 MHz
 * DSI CLK = PLL_VCO / PLLR = 432 / 8 = 54 MHz
 */

#define STM32_PLLCFG_PLLM       RCC_PLLCFG_PLLM(25)
#define STM32_PLLCFG_PLLN       RCC_PLLCFG_PLLN(432)
#define STM32_PLLCFG_PLLP       RCC_PLLCFG_PLLP_2
#define STM32_PLLCFG_PLLQ       RCC_PLLCFG_PLLQ(10)
#define STM32_PLLCFG_PLLR       RCC_PLLCFG_PLLR(8)

#define STM32_VCO_FREQUENCY     ((STM32_HSE_FREQUENCY / 25) * 432)
#define STM32_SYSCLK_FREQUENCY  (STM32_VCO_FREQUENCY / 2)
#define STM32_OTGFS_FREQUENCY   (STM32_VCO_FREQUENCY / 10)
#endif

#endif  // #if (MEADOW_TRY_216_MHz_CLOCK == 1)


/* Configure factors for  PLLSAI clock */

#define CONFIG_STM32F7_PLLSAI 1
#define STM32_RCC_PLLSAICFGR_PLLSAIN    RCC_PLLSAICFGR_PLLSAIN(384)
#define STM32_RCC_PLLSAICFGR_PLLSAIP    RCC_PLLSAICFGR_PLLSAIP(8)
#define STM32_RCC_PLLSAICFGR_PLLSAIQ    RCC_PLLSAICFGR_PLLSAIQ(2)
#define STM32_RCC_PLLSAICFGR_PLLSAIR    RCC_PLLSAICFGR_PLLSAIR(2)

/* SAIx input frequency = 25 / M * N / Q / P
 *   25000000 / 25 * 384 / 2 / 8
 */

#define STM32F7_SAI1_FREQUENCY           (49142857)
#define STM32F7_SAI2_FREQUENCY           (49142857)

/* Configure Dedicated Clock Configuration Register */

#define STM32_RCC_DCKCFGR1_PLLI2SDIVQ  RCC_DCKCFGR1_PLLI2SDIVQ(1)
#define STM32_RCC_DCKCFGR1_PLLSAIDIVQ  RCC_DCKCFGR1_PLLSAIDIVQ(1)
#define STM32_RCC_DCKCFGR1_PLLSAIDIVR  RCC_DCKCFGR1_PLLSAIDIVR(0)
#define STM32_RCC_DCKCFGR1_SAI1SRC     RCC_DCKCFGR1_SAI1SEL(0)
#define STM32_RCC_DCKCFGR1_SAI2SRC     RCC_DCKCFGR1_SAI2SEL(0)
#define STM32_RCC_DCKCFGR1_TIMPRESRC   0
#define STM32_RCC_DCKCFGR1_DFSDM1SRC   0
#define STM32_RCC_DCKCFGR1_ADFSDM1SRC  0

/* Configure factors for PLLI2S clock */

#define CONFIG_STM32F7_PLLI2S          1
#define STM32_RCC_PLLI2SCFGR_PLLI2SN   RCC_PLLI2SCFGR_PLLI2SN(192)
#define STM32_RCC_PLLI2SCFGR_PLLI2SP   RCC_PLLI2SCFGR_PLLI2SP(4)
#define STM32_RCC_PLLI2SCFGR_PLLI2SQ   RCC_PLLI2SCFGR_PLLI2SQ(4)
#define STM32_RCC_PLLI2SCFGR_PLLI2SR   RCC_PLLI2SCFGR_PLLI2SR(4)

/* Configure Dedicated Clock Configuration Register 2 */

#define STM32_RCC_DCKCFGR2_USART1SRC  RCC_DCKCFGR2_USART1SEL_APB
#define STM32_RCC_DCKCFGR2_USART2SRC  RCC_DCKCFGR2_USART2SEL_APB
#define STM32_RCC_DCKCFGR2_UART4SRC   RCC_DCKCFGR2_UART4SEL_APB
#define STM32_RCC_DCKCFGR2_UART5SRC   RCC_DCKCFGR2_UART5SEL_APB
#define STM32_RCC_DCKCFGR2_USART6SRC  RCC_DCKCFGR2_USART6SEL_APB
#define STM32_RCC_DCKCFGR2_UART7SRC   RCC_DCKCFGR2_UART7SEL_APB
#define STM32_RCC_DCKCFGR2_UART8SRC   RCC_DCKCFGR2_UART8SEL_APB
#define STM32_RCC_DCKCFGR2_I2C1SRC    RCC_DCKCFGR2_I2C1SEL_HSI
#define STM32_RCC_DCKCFGR2_I2C2SRC    RCC_DCKCFGR2_I2C2SEL_HSI
#define STM32_RCC_DCKCFGR2_I2C3SRC    RCC_DCKCFGR2_I2C3SEL_HSI
#define STM32_RCC_DCKCFGR2_I2C4SRC    RCC_DCKCFGR2_I2C4SEL_HSI
#define STM32_RCC_DCKCFGR2_LPTIM1SRC  RCC_DCKCFGR2_LPTIM1SEL_APB
#define STM32_RCC_DCKCFGR2_CECSRC     RCC_DCKCFGR2_CECSEL_HSI
#define STM32_RCC_DCKCFGR2_CK48MSRC   RCC_DCKCFGR2_CK48MSEL_PLLSAI
#define STM32_RCC_DCKCFGR2_SDMMCSRC   RCC_DCKCFGR2_SDMMCSEL_48MHZ
#define STM32_RCC_DCKCFGR2_SDMMC2SRC  RCC_DCKCFGR2_SDMMC2SEL_48MHZ
#define STM32_RCC_DCKCFGR2_DSISRC     RCC_DCKCFGR2_DSISEL_PHY

/* Several prescalers allow the configuration of the two AHB buses, the
 * high-speed APB (APB2) and the low-speed APB (APB1) domains. The maximum
 * frequency of the two AHB buses is 216 MHz while the maximum frequency of
 * the high-speed APB domains is 108 MHz. The maximum allowed frequency of
 * the low-speed APB domain is 54 MHz.
 */

/* AHB clock (HCLK) is SYSCLK (216 MHz) */

#define STM32_RCC_CFGR_HPRE     RCC_CFGR_HPRE_SYSCLK  /* HCLK  = SYSCLK / 1 */
#define STM32_HCLK_FREQUENCY    STM32_SYSCLK_FREQUENCY
#define STM32_BOARD_HCLK        STM32_HCLK_FREQUENCY  /* same as above, to satisfy compiler */

#define BOARD_AHB_FREQUENCY     STM32_HCLK_FREQUENCY

/* APB1 clock (PCLK1) is HCLK/4 (54 MHz) */

#define STM32_RCC_CFGR_PPRE1    RCC_CFGR_PPRE1_HCLKd4     /* PCLK1 = HCLK / 4 */
#define STM32_PCLK1_FREQUENCY   (STM32_HCLK_FREQUENCY/4)

/* Timers driven from APB1 will be twice PCLK1 */

#define STM32_APB1_TIM2_CLKIN   (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM3_CLKIN   (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM4_CLKIN   (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM5_CLKIN   (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM6_CLKIN   (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM7_CLKIN   (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM12_CLKIN  (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM13_CLKIN  (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM14_CLKIN  (2*STM32_PCLK1_FREQUENCY)

/* APB2 clock (PCLK2) is HCLK/2 (108MHz) */

#define STM32_RCC_CFGR_PPRE2    RCC_CFGR_PPRE2_HCLKd2     /* PCLK2 = HCLK / 2 */
#define STM32_PCLK2_FREQUENCY   (STM32_HCLK_FREQUENCY/2)

/* Timers driven from APB2 will be twice PCLK2 */

#define STM32_APB2_TIM1_CLKIN   (2*STM32_PCLK2_FREQUENCY)
#define STM32_APB2_TIM8_CLKIN   (2*STM32_PCLK2_FREQUENCY)
#define STM32_APB2_TIM9_CLKIN   (2*STM32_PCLK2_FREQUENCY)
#define STM32_APB2_TIM10_CLKIN  (2*STM32_PCLK2_FREQUENCY)
#define STM32_APB2_TIM11_CLKIN  (2*STM32_PCLK2_FREQUENCY)

/* FLASH wait states
 *
 *  --------- ---------- -----------
 *  VDD       MAX SYSCLK WAIT STATES
 *  --------- ---------- -----------
 *  1.7-2.1 V   180 MHz    8
 *  2.1-2.4 V   216 MHz    9
 *  2.4-2.7 V   216 MHz    8
 *  2.7-3.6 V   216 MHz    7
 *  --------- ---------- -----------
 */

#define BOARD_FLASH_WAITSTATES 7

/* SDIO dividers.  Note that slower clocking is required when DMA is disabled
 * in order to avoid RX overrun/TX underrun errors due to delayed responses
 * to service FIFOs in interrupt driven mode.  These values have not been
 * tuned!!!
 *
 * SDIOCLK=48MHz, SDIO_CK=SDIOCLK/(118+2)=400 KHz
 */

#define STM32_SDMMC_INIT_CLKDIV      (118 << STM32_SDMMC_CLKCR_CLKDIV_SHIFT)

/* DMA ON:  SDIOCLK=48MHz, SDIO_CK=SDIOCLK/(1+2)=16 MHz
 * DMA OFF: SDIOCLK=48MHz, SDIO_CK=SDIOCLK/(2+2)=12 MHz
 */

#ifdef CONFIG_SDIO_DMA
#  define STM32_SDMMC_MMCXFR_CLKDIV  (1 << STM32_SDMMC_CLKCR_CLKDIV_SHIFT)
#else
#  define STM32_SDMMC_MMCXFR_CLKDIV  (2 << STM32_SDMMC_CLKCR_CLKDIV_SHIFT)
#endif

/* DMA ON:  SDIOCLK=48MHz, SDIO_CK=SDIOCLK/(1+2)=16 MHz
 * DMA OFF: SDIOCLK=48MHz, SDIO_CK=SDIOCLK/(2+2)=12 MHz
 */

#ifdef CONFIG_SDIO_DMA
#  define STM32_SDMMC_SDXFR_CLKDIV   (1 << STM32_SDMMC_CLKCR_CLKDIV_SHIFT)
#else
#  define STM32_SDMMC_SDXFR_CLKDIV   (2 << STM32_SDMMC_CLKCR_CLKDIV_SHIFT)
#endif

/* LED definitions **********************************************************/

/* The STM32F777ZIT6-MEADOW board has numerous LEDs but only one, LD1
 * located near the reset button, that can be controlled by software
 * (LD2 is a power indicator, LD3-6
 * indicate USB status, LD7 is controlled by the ST-Link).
 *
 * LD1 is controlled by PI1 which is also the SPI2_SCK at the Arduino
 * interface.
 * One end of LD1 is grounded so a high output on PI1 will illuminate
 * the LED.
 *
 * If CONFIG_ARCH_LEDS is not defined, then the user can control the
 * LEDs in any way.
 * The following definitions are used to access individual LEDs.
 */

/* LED index values for use with board_userled() */

#define BOARD_LED1        0
#define BOARD_LED2        1
#define BOARD_LED3        2
#define BOARD_NLEDS       3

#define BOARD_LED_GREEN   BOARD_LED1
#define BOARD_LED_BLUE    BOARD_LED2
#define BOARD_LED_RED     BOARD_LED3

/* LED bits for use with board_userled_all() */

#define BOARD_LED1_BIT    (1 << BOARD_LED1)
#define BOARD_LED2_BIT    (1 << BOARD_LED2)
#define BOARD_LED3_BIT    (1 << BOARD_LED3)

/* If CONFIG_ARCH_LEDS is defined, the usage by the board port is defined in
 * include/board.h and src/stm32_leds.c.  The LEDs are used
 * to encode OS-related events as follows:
 *
 *   SYMBOL              Meaning                 LD1
 *   ------------------- ----------------------- ------
 *   LED_STARTED         NuttX has been started  OFF
 *   LED_HEAPALLOCATE    Heap has been allocated OFF
 *   LED_IRQSENABLED     Interrupts enabled      OFF
 *   LED_STACKCREATED    Idle stack created      ON
 *   LED_INIRQ           In an interrupt         N/C
 *   LED_SIGNAL          In a signal handler     N/C
 *   LED_ASSERTION       An assertion failed     N/C
 *   LED_PANIC           The system has crashed  FLASH
 *
 * Thus is LD1 is statically on, NuttX has successfully  booted and is,
 * apparently, running normally.  If LD1 is flashing at approximately
 * 2Hz, then a fatal error has been detected and the system has halted.
 */

#define LED_STARTED        0 /* NuttX has been started   OFF    OFF   OFF  */
#define LED_HEAPALLOCATE   1 /* Heap has been allocated  OFF    OFF   ON   */
#define LED_IRQSENABLED    2 /* Interrupts enabled       OFF    ON    OFF  */
#define LED_STACKCREATED   3 /* Idle stack created       OFF    ON    ON   */
#define LED_INIRQ          4 /* In an interrupt          N/C    N/C   GLOW */
#define LED_SIGNAL         5 /* In a signal handler      N/C    GLOW  N/C  */
#define LED_ASSERTION      6 /* An assertion failed      GLOW   N/C   GLOW */
#define LED_PANIC          7 /* The system has crashed   Blink  OFF   N/C  */
#define LED_IDLE           8 /* MCU is is sleep mode     ON     OFF   OFF  */

// #define BOARD_LTDC_WIDTH        800
// #define BOARD_LTDC_HEIGHT       472

/* Button definitions *******************************************************/

/* The STM32F7 Discovery supports one button:
 *  Pushbutton B1, labelled "User", is connected to GPIO PA0.
 *  A high value will be sensed when the button is depressed.
 */

#define BUTTON_USER        0
#define NUM_BUTTONS        1
#define BUTTON_USER_BIT    (1 << BUTTON_USER)

/* LED definitions **********************************************************/

#define MEADOW_ESP32_ONBOARD_RESET_PIN_INPUT  (GPIO_INPUT | GPIO_FLOAT | GPIO_SPEED_100MHz | GPIO_PORTF | GPIO_PIN7)
#define MEADOW_ESP32_ONBOARD_BOOT_PIN_INPUT   (GPIO_INPUT | GPIO_FLOAT | GPIO_SPEED_100MHz | GPIO_PORTI | GPIO_PIN10)

/* Alternate function pin selections ************************************************/

// Note: The source defns are from \arch\arm\src\stm32f7\chip\stm32f76xx77xx_pinmap.h

// USART1 (Meadow COM1) (optional syslog output)
#define GPIO_USART1_RX GPIO_USART1_RX_3 // PB15
#define GPIO_USART1_TX GPIO_USART1_TX_3 // PB14

// UART4 (Meadow COM4) (optional syslog output)
#define GPIO_UART4_RX GPIO_UART4_RX_6 // PI9
#define GPIO_UART4_TX GPIO_UART4_TX_5 // PH13

/* UART5 (Meadow STM32F7 to ESP32) */

#define GPIO_UART5_RX     (GPIO_UART5_RX_1|GPIO_SPEED_100MHz) /* PD2 */
#define GPIO_UART5_TX     (GPIO_UART5_TX_3|GPIO_SPEED_100MHz) /* PB13 - default */
#define GPIO_UART5_TX_V1  (GPIO_UART5_TX_3|GPIO_SPEED_100MHz) /* PB13 - F7v1 */
#define GPIO_UART5_TX_V2  (GPIO_UART5_TX_1|GPIO_SPEED_100MHz) /* PC12 - F7v2 & CCMv2 */

// UART6 (optional syslog output)
// F7v1 and F7v2 both exposed UART6 but on different pins
// F7v1 pins USART6_TX = D02 and USART6_RX = D05
// F7v2 pins USART6_TX = D09 and USART6_RX = D10
#define GPIO_USART6_RX GPIO_USART6_RX_1 // PC7
#define GPIO_USART6_TX GPIO_USART6_TX_1 // PC6

#define BOARD_LTDC_GCR_PCPOL    0
#define BOARD_LTDC_GCR_DEPOL    0
#define BOARD_LTDC_GCR_VSPOL    0
#define BOARD_LTDC_GCR_HSPOL    0

/* PWM for Meadow */

#define GPIO_TIM2_CH1OUT  GPIO_TIM2_CH1OUT_1
#define GPIO_TIM2_CH2OUT  GPIO_TIM2_CH2OUT_1
#define GPIO_TIM2_CH3OUT  GPIO_TIM2_CH3OUT_1
#define GPIO_TIM3_CH2OUT  GPIO_TIM3_CH2OUT_3
#define GPIO_TIM3_CH3OUT  GPIO_TIM3_CH3OUT_1
#define GPIO_TIM3_CH4OUT  GPIO_TIM3_CH4OUT_1
#define GPIO_TIM4_CH1OUT  GPIO_TIM4_CH1OUT_1
#define GPIO_TIM4_CH2OUT  GPIO_TIM4_CH2OUT_1
#define GPIO_TIM4_CH3OUT  GPIO_TIM4_CH3OUT_1
#define GPIO_TIM4_CH4OUT  GPIO_TIM4_CH4OUT_1
#define GPIO_TIM5_CH1OUT  GPIO_TIM5_CH1OUT_2
#define GPIO_TIM8_CH1OUT  GPIO_TIM8_CH1OUT_1
#define GPIO_TIM8_CH4OUT  GPIO_TIM8_CH4OUT_1
#define GPIO_TIM9_CH2OUT  GPIO_TIM9_CH2OUT_1
#define GPIO_TIM12_CH1OUT  GPIO_TIM12_CH1OUT_1
#define GPIO_TIM12_CH2OUT  GPIO_TIM12_CH2OUT_1
#define GPIO_TIM14_CH1OUT  GPIO_TIM14_CH1OUT_1

/* DMA Channel/Stream Selections ********************************************/

#define DMACHAN_SAI2_A          DMAMAP_SAI2_A
#define DMACHAN_SAI2_B          DMAMAP_SAI2_B
#define DMACHAN_SAI1_B          DMAMAP_SAI1_B

/* SDMMC */

/* Stream selections are arbitrary for now but might become important in the
 * future if we set aside more DMA channels/streams.
 *
 * THE FOLLOWING ARE WRONG FOR F7. Issue written (PWM 5Feb23)
 * SDIO DMA
 *   DMAMAP_SDMMC1_1 = Channel 4, Stream 3
 *   DMAMAP_SDMMC1_2 = Channel 4, Stream 6
 *
 *   DMAMAP_SDMMC2_1 = Channel 11, Stream 0
 *   DMAMAP_SDMMC2_2 = Channel 11, Stream 5
 */

#define DMAMAP_SDMMC2  DMAMAP_SDMMC2_1

/* Alternate function pin selections ****************************************/

// ADC DMA mapping
#define ADC1_DMA_CHAN DMAMAP_ADC1_2     // DMA2, Channel 0, Stream 4

// If additional conflicts add the following to the function
// 'DMA_HANDLE stm32_dmachannel(unsigned int dmamap)'.
// syslog(2, "stm32_dmachannel(dmamap:0x%08x, DMA:%d, Chan:%d, Stream:%d)\n",
//  dmamap, ((dmamap >> 7) & 1) + 1, ((dmamap >> 4) & 7), (dmamap & 0xf));
// It won't show DMA initialized by Meadow.Core until it's initialized...
// Also for meadow_adc.c the stream number will need to be changed for a
// number of register uses.
// Note: All SPI DMA configuration is hardcoded in stm32_spi.c

/* SDMMC2 Pin mapping
 *
 *   -------- ---------------
 *               STM32F7
 *   SIGNAME  FUNCTION  GPIO
 *   -- ----- --------- -----
 *   VCP_RX   USART1_RX PA10
 *   VCP_TX   USART1_TX PA9
 *   -- ----- --------- -----
 */

/* Used to reconfigure F7's NJTRST pin as a non-debug GPIO at startup */

#define MEADOW_DEBUG_NJTRST_NOT_USED_GPIO (GPIO_INPUT | GPIO_FLOAT | GPIO_SPEED_100MHz | GPIO_PORTB | GPIO_PIN4)

/* The STM32 F7 connects to a SMSC LAN8742A PHY using these pins:
 *
 *   STM32 F7 BOARD        LAN8742A
 *   GPIO     SIGNAL       PIN NAME
 *   -------- ------------ -------------
 *   PG11     RMII_TX_EN   TXEN
 *   PG13     RMII_TXD0    TXD0
 *   PG14     RMII_TXD1    TXD1
 *   PC4      RMII_RXD0    RXD0/MODE0
 *   PC5      RMII_RXD1    RXD1/MODE1
 *   PD5      RMII_RXER    RXER/PHYAD0
 *   PA7      RMII_CRS_DV  CRS_DV/MODE2
 *   PC1      RMII_MDC     MDC
 *   PA2      RMII_MDIO    MDIO
 *   N/A      NRST         nRST
 *   PA1      RMII_REF_CLK nINT/REFCLK0
 *   N/A      OSC_25M      XTAL1/CLKIN
 *
 * The PHY address is 0, since RMII_RXER/PHYAD0 features a pull down.
 * After reset, RMII_RXER/PHYAD0 switches to the RXER function,
 * receive errors can be detected using GPIO pin PD5
 */

#define GPIO_ETH_MDC          (GPIO_ETH_MDC_0|GPIO_SPEED_100MHz)
#define GPIO_ETH_MDIO         (GPIO_ETH_MDIO_0|GPIO_SPEED_100MHz)
#define GPIO_ETH_RMII_CRS_DV  (GPIO_ETH_RMII_CRS_DV_0|GPIO_SPEED_100MHz)
#define GPIO_ETH_RMII_REF_CLK (GPIO_ETH_RMII_REF_CLK_0|GPIO_SPEED_100MHz)
#define GPIO_ETH_RMII_RXD0    (GPIO_ETH_RMII_RXD0_0|GPIO_SPEED_100MHz)
#define GPIO_ETH_RMII_RXD1    (GPIO_ETH_RMII_RXD1_0|GPIO_SPEED_100MHz)
#define GPIO_ETH_RMII_TX_EN   (GPIO_ETH_RMII_TX_EN_1|GPIO_SPEED_100MHz)
#define GPIO_ETH_RMII_TXD0    (GPIO_ETH_RMII_TXD0_2|GPIO_SPEED_100MHz)
#define GPIO_ETH_RMII_TXD1    (GPIO_ETH_RMII_TXD1_2|GPIO_SPEED_100MHz)

/* I2C Mapping
 *
 * I2C1_SCL - PB6
 * I2C1_SDA - PB7
 */

#define GPIO_I2C1_SCL        (GPIO_I2C1_SCL_1|GPIO_SPEED_50MHz)
#define GPIO_I2C1_SDA        (GPIO_I2C1_SDA_1|GPIO_SPEED_50MHz)

#define GPIO_SPI2_CS          (GPIO_SPI_CS | GPIO_PORTI | GPIO_PIN0)
#define GPIO_SPI2_SCK         GPIO_SPI2_SCK_4 
#define GPIO_SPI2_MISO        GPIO_SPI2_MISO_3
#define GPIO_SPI2_MOSI        GPIO_SPI2_MOSI_3

// Meadow uses 3 SPI devices SPI2, SPI3 and SPI5
// SPI2 only has one option for RX and TX this is baked into stm32_spi.c
// #define DMAMAP_SPI2_RX             STM32_DMA_MAP(DMA1,DMA_STREAM3,DMA_CHAN0)
// #define DMAMAP_SPI2_TX             STM32_DMA_MAP(DMA1,DMA_STREAM4,DMA_CHAN0)

// Mapping for SPI3.
// #define DMAMAP_SPI3_RX_1  STM32_DMA_MAP(DMA1,DMA_STREAM0,DMA_CHAN0)
// #define DMAMAP_SPI3_TX_1  STM32_DMA_MAP(DMA1,DMA_STREAM5,DMA_CHAN0)
// #define DMAMAP_SPI3_RX_2  STM32_DMA_MAP(DMA1,DMA_STREAM2,DMA_CHAN0)
// #define DMAMAP_SPI3_TX_2  STM32_DMA_MAP(DMA1,DMA_STREAM7,DMA_CHAN0)
#define DMAMAP_SPI3_RX  DMAMAP_SPI3_RX_2
#define DMAMAP_SPI3_TX  DMAMAP_SPI3_TX_2

// Mapping for SPI5, Note uses DMA2 mapping
// #define DMAMAP_SPI5_RX_1  STM32_DMA_MAP(DMA2,DMA_STREAM3,DMA_CHAN2)
// #define DMAMAP_SPI5_TX_1  STM32_DMA_MAP(DMA2,DMA_STREAM4,DMA_CHAN2)
// #define DMAMAP_SPI5_RX_2  STM32_DMA_MAP(DMA2,DMA_STREAM5,DMA_CHAN7)
// #define DMAMAP_SPI5_TX_2  STM32_DMA_MAP(DMA2,DMA_STREAM6,DMA_CHAN7)
// #define DMAMAP_SPI5_RX_3  STM32_DMA_MAP(DMA2,DMA_STREAM5,DMA_CHAN9)
#define DMAMAP_SPI5_RX  DMAMAP_SPI5_RX_1
#define DMAMAP_SPI5_TX  DMAMAP_SPI5_TX_1

/************************************************************************************
 * Public Data
 ************************************************************************************/
#ifndef __ASSEMBLY__

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif
/* QSPI Mapping  */

#define GPIO_QSPI_CS  (GPIO_QUADSPI_BK1_NCS_2 | GPIO_FLOAT | GPIO_PUSHPULL | GPIO_SPEED_100MHz)
#define GPIO_QSPI_IO0 (GPIO_QUADSPI_BK1_IO0_3 | GPIO_FLOAT | GPIO_PUSHPULL | GPIO_SPEED_100MHz)
#define GPIO_QSPI_IO1 (GPIO_QUADSPI_BK1_IO1_3 | GPIO_FLOAT | GPIO_PUSHPULL | GPIO_SPEED_100MHz)
#define GPIO_QSPI_IO2 (GPIO_QUADSPI_BK1_IO2_1 | GPIO_FLOAT | GPIO_PUSHPULL | GPIO_SPEED_100MHz)
#define GPIO_QSPI_IO3 (GPIO_QUADSPI_BK1_IO3_2 | GPIO_FLOAT | GPIO_PUSHPULL | GPIO_SPEED_100MHz)
#define GPIO_QSPI_SCK (GPIO_QUADSPI_CLK_1     | GPIO_FLOAT | GPIO_PUSHPULL | GPIO_SPEED_100MHz)

/* SDMMC2 Pin mapping
 *
 * D0 - PG9
 * D1 - PG10
 * D2 - PG11
 * D3 - PG12
 */

#define GPIO_SDMMC2_CK  (GPIO_SDMMC2_CK_0|GPIO_SPEED_50MHz)
#define GPIO_SDMMC2_CMD (GPIO_SDMMC2_CMD_0|GPIO_SPEED_50MHz)
#define GPIO_SDMMC2_D0  (GPIO_SDMMC2_D0_2|GPIO_SPEED_50MHz)
#define GPIO_SDMMC2_D1  (GPIO_SDMMC2_D1_2|GPIO_SPEED_50MHz)
#define GPIO_SDMMC2_D2  (GPIO_SDMMC2_D2_2|GPIO_SPEED_50MHz)
#define GPIO_SDMMC2_D3  (GPIO_SDMMC2_D3_2|GPIO_SPEED_50MHz)

/* OTGFS */

#define GPIO_OTGFS_DM  (GPIO_OTGFS_DM_0|GPIO_SPEED_100MHz)
#define GPIO_OTGFS_DP  (GPIO_OTGFS_DP_0|GPIO_SPEED_100MHz)
#define GPIO_OTGFS_ID  (GPIO_OTGFS_ID_0|GPIO_SPEED_100MHz)

/* FMC - SDRAM */

#define GPIO_FMC_A0     (GPIO_FMC_A0_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_A1     (GPIO_FMC_A1_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_A2     (GPIO_FMC_A2_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_A3     (GPIO_FMC_A3_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_A4     (GPIO_FMC_A4_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_A5     (GPIO_FMC_A5_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_A6     (GPIO_FMC_A6_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_A7     (GPIO_FMC_A7_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_A8     (GPIO_FMC_A8_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_A9     (GPIO_FMC_A9_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_A10    (GPIO_FMC_A10_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_A11    (GPIO_FMC_A11_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_A12    (GPIO_FMC_A12_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_BA0    (GPIO_FMC_BA0_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_BA1    (GPIO_FMC_BA1_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D0     (GPIO_FMC_D0_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D1     (GPIO_FMC_D1_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D2     (GPIO_FMC_D2_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D3     (GPIO_FMC_D3_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D4     (GPIO_FMC_D4_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D5     (GPIO_FMC_D5_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D6     (GPIO_FMC_D6_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D7     (GPIO_FMC_D7_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D8     (GPIO_FMC_D8_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D9     (GPIO_FMC_D9_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D10    (GPIO_FMC_D10_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D11    (GPIO_FMC_D11_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D12    (GPIO_FMC_D12_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D13    (GPIO_FMC_D13_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D14    (GPIO_FMC_D14_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_D15    (GPIO_FMC_D15_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_NBL0   (GPIO_FMC_NBL0_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_NBL1   (GPIO_FMC_NBL1_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_SDCLK  (GPIO_FMC_SDCLK_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_SDCKE0 (GPIO_FMC_SDCKE0_3|GPIO_SPEED_100MHz)
#define GPIO_FMC_SDCKE1 (GPIO_FMC_SDCKE1_1|GPIO_SPEED_100MHz)
#define GPIO_FMC_SDNE0  (GPIO_FMC_SDNE0_3|GPIO_SPEED_100MHz)
#define GPIO_FMC_SDNE1  (GPIO_FMC_SDNE1_1|GPIO_SPEED_100MHz)
#define GPIO_FMC_SDNCAS (GPIO_FMC_SDNCAS_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_SDNRAS (GPIO_FMC_SDNRAS_0|GPIO_SPEED_100MHz)
#define GPIO_FMC_SDNWE  (GPIO_FMC_SDNWE_3|GPIO_SPEED_100MHz)

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif  /* __BOARDS_ARM_STM32F777ZIT6_MEADOW_INCLUDE_BOARD_H */
