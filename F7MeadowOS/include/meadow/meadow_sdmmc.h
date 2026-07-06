/****************************************************************************************************
 * Meadow.OS/include/meadow/meadow_sdmmc.h
 *
 *   Copyright (C) 2015 Gregory Nutt. All rights reserved.
 *   Authors: Gregory Nutt <gnutt@nuttx.org>
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
 ****************************************************************************************************/

#ifndef __CONFIGS_MEADOW_SRC_MEADOW_SDMMC__H
#define __CONFIGS_MEADOW_SRC_MEADOW_SDMMC__H

/****************************************************************************************************
 * Included Files
 ****************************************************************************************************/

#include "../include/board.h"    // defines GPIO_SDIO_NC

/****************************************************************************************************
 * SDCard Support
 ****************************************************************************************************/
#if defined (CONFIG_STM32F7_SDMMC1)
#error "CONFIG_STM32F7_SDMMC1 is not supported on the Meadow"
#endif

#if defined (CONFIG_STM32F7_SDMMC2)
int stm32_sdio_initialize_meadow(void);

// NCD because C=Card, D=Detection and N=because the pin is pulled to ground
// (i.e. not) when the SD card is inserted. Better would be 'nCD'.

#define GPIO_MEADOW_SDIO_NCD  (GPIO_INPUT|GPIO_PULLUP|GPIO_EXTI|GPIO_PORTG|GPIO_PIN6)

#define HAVE_MEADOW_NCD         1
#define SDIO_SLOTNO             0       // With only one slot, it is must be 0
#define SDIO_MINOR              0
#define HAVE_SDIO               1

#endif      // #if defined (CONFIG_STM32F7_SDMMC2)

#endif // __CONFIGS_MEADOW_SRC_MEADOW_SDMMC__H