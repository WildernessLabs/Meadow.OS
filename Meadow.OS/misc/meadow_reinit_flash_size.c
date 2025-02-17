/****************************************************************************
 * /Meadow.OS/misc/meadow_reinit_flash_size.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../hcom_nx/hcom_nx_common.h"
#include "hardware/stm32_qspi.h"
#include <arm_internal.h>

/****************************************************************************
 * Public Function
 ****************************************************************************/
// The Meadow function that initialized qspi uses a fixed value defined
// in CONFIG_STM32F7_QSPI_FLASH_SIZE as the initial flash size. Since we've
// been called here the size must have been wrong. Therefore, we'll override
// the initial register values.
// In Legacy Meadow, this function (not file) was added to the Nuttx code base.
// It has been added here, in a Meadow file, to minimize the number of Nuttx
// code changes.
void meadow_qspi_hw_reinitialize(int flashSize)
{  
  uint32_t regval;

  // Disable the QSPI; abort anything happening, disable, wait for not busy
  regval = getreg32(STM32_QUADSPI_CR);
  regval |= QSPI_CR_ABORT;
  putreg32(regval, STM32_QUADSPI_CR);

  // Reconfigure QSPI Flash Size, must assume CS High Time and Clock Mode
  // are already set correctly
  regval = getreg32(STM32_QUADSPI_DCR);
  if (0 != flashSize)
    {
      unsigned int nsize = flashSize;
      int nlog2size = 31;

      while ((nsize & 0x80000000) == 0)
        {
          --nlog2size;
          nsize <<= 1;
        }

      regval |= ((nlog2size - 1) << QSPI_DCR_FSIZE_SHIFT);
    }

  putreg32(regval, STM32_QUADSPI_DCR);

  // Enable QSPI
  regval = getreg32(STM32_QUADSPI_CR);
  regval |= QSPI_CR_EN;
  putreg32(regval, STM32_QUADSPI_CR);

  // Wait till BUSY flag clear
  while (((regval = getreg32(STM32_QUADSPI_SR)) & QSPI_SR_BUSY));
}
