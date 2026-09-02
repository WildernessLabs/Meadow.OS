/****************************************************************************
 * /Meadow.OS/misc/meadow_stm32f7_clockenable.c
 *
 *   Copyright (C) 2025 Wilderness Labs. All rights reserved.
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

// This file is a duplicate of the Nuttx 'void stm32_clockenable(void)'
// function and stm32_stdclockconfig function. This was needed because
// when the Meadow power management was created it needed the
// stm32_clockenable function. However, it is only built if the Nuttx power
// management code is built via the CONFIG_PM configuration option.
// Therefore, this function was duplicated here with the name changed to
// meadow_clockenable.
// The decision to do this was due to the side-effects and complexities of
// pulling in the entire Nuttx power managment.

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include "meadow_stm32f7_clockenable.h"
#include "../hcom_nx/hcom_nx_common.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "stm32_rcc.h"    // meadow_clockenable
#include "hardware/stm32f76xx77xx_pwr.h"
#include "hardware/stm32f76xx77xx_flash.h"
#include "hardware/stm32f76xx77xx_rcc.h"
#include "boards/arm/stm32f7/stm32f777zit6-meadow/include/board.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Allow up to 100 milliseconds for the high speed clock to become ready.
 * that is a very long delay, but if the clock does not become ready we are
 * hosed anyway.
 */

#define HSERDY_TIMEOUT (100 * CONFIG_BOARD_LOOPSPERMSEC)
#define HSIRDY_TIMEOUT HSERDY_TIMEOUT

/* Voltage output scale (default to Scale 1 mode) */

#ifndef STM32_PWR_VOS_SCALE
#  define STM32_PWR_VOS_SCALE PWR_CR1_VOS_SCALE_1
#endif

/* HSE divisor to yield ~1MHz RTC clock */

#define HSE_DIVISOR ((STM32_HSE_FREQUENCY + 500000) / 1000000)

/****************************************************************************
 * Private Function
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_stdclockconfig
 *
 * Description:
 *   Called to change to new clock based on settings in board.h
 *
 *   NOTE:  This logic would need to be extended if you need to select low-
 *   power clocking modes!
 ****************************************************************************/

#ifndef CONFIG_STM32F7_CUSTOM_CLOCKCONFIG
static void meadow_stdclockconfig(void)
{
  uint32_t regval;
  volatile int32_t timeout;

#ifdef STM32_BOARD_USEHSI
  /* Enable Internal High-Speed Clock (HSI) */

  regval  = getreg32(STM32_RCC_CR);
  regval |= RCC_CR_HSION;           /* Enable HSI */
  putreg32(regval, STM32_RCC_CR);

  /* Wait until the HSI is ready (or until a timeout elapsed) */

  for (timeout = HSIRDY_TIMEOUT; timeout > 0; timeout--)
    {
      /* Check if the HSIRDY flag is the set in the CR */

      if ((getreg32(STM32_RCC_CR) & RCC_CR_HSIRDY) != 0)
        {
          /* If so, then break-out with timeout > 0 */

          break;
        }
    }

#else /* if STM32_BOARD_USEHSE */
  /* Enable External High-Speed Clock (HSE) */

  regval  = getreg32(STM32_RCC_CR);
#ifdef STM32_HSEBYP_ENABLE          /* May be defined in board.h header file */
  regval |= RCC_CR_HSEBYP;          /* Enable HSE clock bypass */
#else
  regval &= ~RCC_CR_HSEBYP;         /* Disable HSE clock bypass */
#endif
  regval |= RCC_CR_HSEON;           /* Enable HSE */
  putreg32(regval, STM32_RCC_CR);

  /* Wait until the HSE is ready (or until a timeout elapsed) */

  for (timeout = HSERDY_TIMEOUT; timeout > 0; timeout--)
    {
      /* Check if the HSERDY flag is the set in the CR */

      if ((getreg32(STM32_RCC_CR) & RCC_CR_HSERDY) != 0)
        {
          /* If so, then break-out with timeout > 0 */

          break;
        }
    }
#endif

  /* Check for a timeout.  If this timeout occurs, then we are hosed.  We
   * have no real back-up plan, although the following logic makes it look
   * as though we do.
   */

  if (timeout > 0)
    {
      /* Select regulator voltage output Scale 1 mode to support system
       * frequencies up to 216 MHz.
       */

      regval  = getreg32(STM32_RCC_APB1ENR);
      regval |= RCC_APB1ENR_PWREN;
      putreg32(regval, STM32_RCC_APB1ENR);

      regval  = getreg32(STM32_PWR_CR1);
      regval &= ~PWR_CR1_VOS_MASK;
      regval |= STM32_PWR_VOS_SCALE;
      putreg32(regval, STM32_PWR_CR1);

      /* Set the HCLK source/divider */

      regval = getreg32(STM32_RCC_CFGR);
      regval &= ~RCC_CFGR_HPRE_MASK;
      regval |= STM32_RCC_CFGR_HPRE;
      putreg32(regval, STM32_RCC_CFGR);

      /* Set the PCLK2 divider */

      regval = getreg32(STM32_RCC_CFGR);
      regval &= ~RCC_CFGR_PPRE2_MASK;
      regval |= STM32_RCC_CFGR_PPRE2;
      putreg32(regval, STM32_RCC_CFGR);

      /* Set the PCLK1 divider */

      regval = getreg32(STM32_RCC_CFGR);
      regval &= ~RCC_CFGR_PPRE1_MASK;
      regval |= STM32_RCC_CFGR_PPRE1;
      putreg32(regval, STM32_RCC_CFGR);

#ifdef CONFIG_STM32F7_RTC_HSECLOCK
      /* Set the RTC clock divisor */

      regval = getreg32(STM32_RCC_CFGR);
      regval &= ~RCC_CFGR_RTCPRE_MASK;
      regval |= RCC_CFGR_RTCPRE(HSE_DIVISOR);
      putreg32(regval, STM32_RCC_CFGR);
#endif

      /* Set the PLL dividers and multipliers to configure the main PLL */

#ifdef STM32_BOARD_USEHSI
      regval = (STM32_PLLCFG_PLLM | STM32_PLLCFG_PLLN | STM32_PLLCFG_PLLP |
                RCC_PLLCFG_PLLSRC_HSI | STM32_PLLCFG_PLLQ);
#else /* if STM32_BOARD_USEHSE */
      regval = (STM32_PLLCFG_PLLM | STM32_PLLCFG_PLLN | STM32_PLLCFG_PLLP |
                RCC_PLLCFG_PLLSRC_HSE | STM32_PLLCFG_PLLQ);
#endif
      putreg32(regval, STM32_RCC_PLLCFG);

      /* Enable the main PLL */

      regval = getreg32(STM32_RCC_CR);
      regval |= RCC_CR_PLLON;
      putreg32(regval, STM32_RCC_CR);

      /* Wait until the PLL is ready */

      while ((getreg32(STM32_RCC_CR) & RCC_CR_PLLRDY) == 0)
        {
        }

      /* Over-drive is needed if
       *  - Voltage output scale 1 mode is selected and SYSCLK frequency is
       *    over 180 MHz.
       *  - Voltage output scale 2 mode is selected and SYSCLK frequence is
       *    over 168 MHz.
       */

      if ((STM32_PWR_VOS_SCALE == PWR_CR1_VOS_SCALE_1 &&
           STM32_SYSCLK_FREQUENCY > 180000000) ||
          (STM32_PWR_VOS_SCALE == PWR_CR1_VOS_SCALE_2 &&
           STM32_SYSCLK_FREQUENCY > 168000000))
        {
          /* Enable the Over-drive to extend the clock frequency up to
           * 216 MHz.
           */

          regval  = getreg32(STM32_PWR_CR1);
          regval |= PWR_CR1_ODEN;
          putreg32(regval, STM32_PWR_CR1);
          while ((getreg32(STM32_PWR_CSR1) & PWR_CSR1_ODRDY) == 0)
            {
            }

          regval = getreg32(STM32_PWR_CR1);
          regval |= PWR_CR1_ODSWEN;
          putreg32(regval, STM32_PWR_CR1);
          while ((getreg32(STM32_PWR_CSR1) & PWR_CSR1_ODSWRDY) == 0)
            {
            }
        }

      /* Configure FLASH wait states */

      regval = FLASH_ACR_LATENCY(BOARD_FLASH_WAITSTATES);

#ifdef CONFIG_STM32F7_FLASH_ART_ACCELERATOR
      /* The Flash memory interface accelerates code execution with a system
       * of instruction prefetch and cache lines on ITCM interface (ART
       * Accelerator™).
       */

      regval |= FLASH_ACR_ARTEN;
      regval |= FLASH_ACR_PRFTEN;
#endif

      putreg32(regval, STM32_FLASH_ACR);

      /* Select the main PLL as system clock source */

      regval  = getreg32(STM32_RCC_CFGR);
      regval &= ~RCC_CFGR_SW_MASK;
      regval |= RCC_CFGR_SW_PLL;
      putreg32(regval, STM32_RCC_CFGR);

      /* Wait until the PLL source is used as the system clock source */

      while ((getreg32(STM32_RCC_CFGR) & RCC_CFGR_SWS_MASK) !=
             RCC_CFGR_SWS_PLL)
        {
        }

#if defined(CONFIG_STM32F7_LTDC) || defined(CONFIG_STM32F7_PLLSAI)

      /* Configure PLLSAI */

      regval = getreg32(STM32_RCC_PLLSAICFGR);
      regval &= ~(RCC_PLLSAICFGR_PLLSAIN_MASK
                  | RCC_PLLSAICFGR_PLLSAIP_MASK
                  | RCC_PLLSAICFGR_PLLSAIQ_MASK
                  | RCC_PLLSAICFGR_PLLSAIR_MASK);
      regval |= (STM32_RCC_PLLSAICFGR_PLLSAIN
                 | STM32_RCC_PLLSAICFGR_PLLSAIP
                 | STM32_RCC_PLLSAICFGR_PLLSAIQ
                 | STM32_RCC_PLLSAICFGR_PLLSAIR);
      putreg32(regval, STM32_RCC_PLLSAICFGR);

      regval  = getreg32(STM32_RCC_DCKCFGR1);
      regval &= ~(RCC_DCKCFGR1_PLLI2SDIVQ_MASK
                  | RCC_DCKCFGR1_PLLSAIDIVQ_MASK
                  | RCC_DCKCFGR1_PLLSAIDIVR_MASK
                  | RCC_DCKCFGR1_SAI1SEL_MASK
                  | RCC_DCKCFGR1_SAI2SEL_MASK
                  | RCC_DCKCFGR1_TIMPRESEL
                  | RCC_DCKCFGR1_DFSDM1SEL
                  | RCC_DCKCFGR1_ADFSDM1SEL);

      regval |= (STM32_RCC_DCKCFGR1_PLLI2SDIVQ
                 | STM32_RCC_DCKCFGR1_PLLSAIDIVQ
                 | STM32_RCC_DCKCFGR1_PLLSAIDIVR
                 | STM32_RCC_DCKCFGR1_SAI1SRC
                 | STM32_RCC_DCKCFGR1_SAI2SRC
                 | STM32_RCC_DCKCFGR1_TIMPRESRC
                 | STM32_RCC_DCKCFGR1_DFSDM1SRC
                 | STM32_RCC_DCKCFGR1_ADFSDM1SRC);

      putreg32(regval, STM32_RCC_DCKCFGR1);

      /* Enable PLLSAI */

      regval = getreg32(STM32_RCC_CR);
      regval |= RCC_CR_PLLSAION;
      putreg32(regval, STM32_RCC_CR);

      /* Wait until the PLLSAI is ready */

      while ((getreg32(STM32_RCC_CR) & RCC_CR_PLLSAIRDY) == 0)
        {
        }
#endif
#if defined(CONFIG_STM32F7_PLLI2S) || (STM32_RCC_DCKCFGR1_SAI1SRC == RCC_DCKCFGR1_SAI1SEL(1)) || (STM32_RCC_DCKCFGR1_SAI2SRC == RCC_DCKCFGR1_SAI2SEL(1))

      /* Configure PLLI2S */

      regval = getreg32(STM32_RCC_PLLI2SCFGR);
      regval &= ~(RCC_PLLI2SCFGR_PLLI2SN_MASK
                  | RCC_PLLI2SCFGR_PLLI2SP_MASK
                  | RCC_PLLI2SCFGR_PLLI2SQ_MASK
                  | RCC_PLLI2SCFGR_PLLI2SR_MASK);
      regval |= (STM32_RCC_PLLSAICFGR_PLLSAIN
                 | STM32_RCC_PLLSAICFGR_PLLSAIP
                 | STM32_RCC_PLLSAICFGR_PLLSAIQ
                 | STM32_RCC_PLLSAICFGR_PLLSAIR);
      putreg32(regval, STM32_RCC_PLLI2SCFGR);

      /* Enable PLLI2S */

      regval = getreg32(STM32_RCC_CR);
      regval |= RCC_CR_PLLI2SON;
      putreg32(regval, STM32_RCC_CR);

      /* Wait until the PLLI2S is ready */

      while ((getreg32(STM32_RCC_CR) & RCC_CR_PLLI2SRDY) == 0)
        {
        }
#endif

      regval  = getreg32(STM32_RCC_DCKCFGR2);
      regval &= ~(RCC_DCKCFGR2_USART1SEL_MASK
                  | RCC_DCKCFGR2_USART2SEL_MASK
                  | RCC_DCKCFGR2_UART4SEL_MASK
                  | RCC_DCKCFGR2_UART5SEL_MASK
                  | RCC_DCKCFGR2_USART6SEL_MASK
                  | RCC_DCKCFGR2_UART7SEL_MASK
                  | RCC_DCKCFGR2_UART8SEL_MASK
                  | RCC_DCKCFGR2_I2C1SEL_MASK
                  | RCC_DCKCFGR2_I2C2SEL_MASK
                  | RCC_DCKCFGR2_I2C3SEL_MASK
                  | RCC_DCKCFGR2_I2C4SEL_MASK
                  | RCC_DCKCFGR2_LPTIM1SEL_MASK
                  | RCC_DCKCFGR2_CECSEL_MASK
                  | RCC_DCKCFGR2_CK48MSEL_MASK
                  | RCC_DCKCFGR2_SDMMCSEL_MASK
                  | RCC_DCKCFGR2_SDMMC2SEL_MASK
                  | RCC_DCKCFGR2_DSISEL_MASK);

      regval |= (STM32_RCC_DCKCFGR2_USART1SRC
                 | STM32_RCC_DCKCFGR2_USART2SRC
                 | STM32_RCC_DCKCFGR2_UART4SRC
                 | STM32_RCC_DCKCFGR2_UART5SRC
                 | STM32_RCC_DCKCFGR2_USART6SRC
                 | STM32_RCC_DCKCFGR2_UART7SRC
                 | STM32_RCC_DCKCFGR2_UART8SRC
                 | STM32_RCC_DCKCFGR2_I2C1SRC
                 | STM32_RCC_DCKCFGR2_I2C2SRC
                 | STM32_RCC_DCKCFGR2_I2C3SRC
                 | STM32_RCC_DCKCFGR2_I2C4SRC
                 | STM32_RCC_DCKCFGR2_LPTIM1SRC
                 | STM32_RCC_DCKCFGR2_CECSRC
                 | STM32_RCC_DCKCFGR2_CK48MSRC
                 | STM32_RCC_DCKCFGR2_SDMMCSRC
                 | STM32_RCC_DCKCFGR2_SDMMC2SRC
                 | STM32_RCC_DCKCFGR2_DSISRC);

      putreg32(regval, STM32_RCC_DCKCFGR2);

#if defined(CONFIG_STM32F7_IWDG) || defined(CONFIG_STM32F7_RTC_LSICLOCK)
      /* Low speed internal clock source LSI */

      stm32_rcc_enablelsi();
#endif

#if defined(CONFIG_STM32F7_RTC_LSECLOCK)
      /* Low speed external clock source LSE
       *
       * TODO: There is another case where the LSE needs to
       * be enabled: if the MCO1 pin selects LSE as source.
       */

      stm32_rcc_enablelse();
#endif
    }
}
#endif

#ifdef CONFIG_ARMV7M_ITMSYSLOG
static inline void rcc_itm_syslog(void)
{
  /* Enable SWO output */

  modifyreg32(STM32_DBGMCU_CR, DBGMCU_CR_TRACEMODE_MASK,
              DBGMCU_CR_ASYNCH | DBGMCU_CR_TRACEIOEN);
}
#else
#  define rcc_itm_syslog()
#endif


/****************************************************************************
 * Public Function
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_clockenable
 *
 * Description:
 *   Re-enable the clock and restore the clock settings based on settings
 *   in board.h. This function is only available to support low-power
 *   modes of operation:  When re-awakening from deep-sleep modes, it is
 *   necessary to re-enable/re-start the PLL
 *
 *   This functional performs a subset of the operations performed by
 *   stm32_clockconfig():  It does not reset any devices, and it does not
 *   reset the currently enabled peripheral clocks.
 *
 *   If CONFIG_STM32F7_CUSTOM_CLOCKCONFIG is defined, then clocking
 *   will be enabled by an externally provided, board-specific function
 *   called stm32_board_clockconfig().
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
// Don't build this copy if the Nuttx version is built
#ifndef CONFIG_PM
void meadow_clockenable(void)
{
#if defined(CONFIG_STM32F7_CUSTOM_CLOCKCONFIG)

  /* Invoke Board Custom Clock Configuration */

  stm32_board_clockconfig();

#else

  /* Invoke standard, fixed clock configuration based on definitions
   * in board.h
   */

  meadow_stdclockconfig();

#endif
}

#endif
