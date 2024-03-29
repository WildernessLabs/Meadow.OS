/****************************************************************************
 * Meadow.OS/Meadow.OS/meadow_temp_rebase.c
 *   Copyright (C) 2024 Wilderness Labs. All rights reserved.
 *   Author:  Wilderness Labs
*/
// This file is temporary while bringing up HCOM on Nuttx v12. It contains
// things required to get HCOM working. At this time we are attempting to
// not add modify Nuttx itself. Other Meadow features maybe included
// here as stubs, proxies or actual code, whatever is needed to get HCOM
// running.
//
// Please add comments to all additions, so they can be understood at a
// future time
//

#include <nuttx/config.h>

#include <debug.h>
#include <assert.h>

#include <meadow/meadow_temp_rebase.h>
#include <arm_internal.h>
#include "hardware/stm32_qspi.h"

//==========================================================================
// In Legacy Meadow the stm32f7_qspi_hw_reinitialize() function was added to
// the Nuttx stm32_qspi.c function. It has been removed to reduce the friction
// of updating Nuttx in the future.
// Why is this needed? because Meadow must run on all Meadow versions and
// not all versions use the same size flash. This is why this function became
// necessary. On startup Meadow has no idea what hardware it is running on,
// so it assumes it is the hardware with the smallest external flash memory.
// Once Meadow boots it can read the flash chip's internal information and find
// what the actual size of the flash is. This function then allows the STM32F7's
// internal registers to be corrected if the originally assumed flash size was
// wrong.
/****************************************************************************
 * Name: stm32f7_qspi_hw_reinitialize
 *
 * Description:
 *   Reinitializes the flash size.
 *
 *   Note: This function assumes that stm32f7_qspi_initialize() has already been
 *   successfully called.
 *
 * Input Parameters:
 *   flashSize - The actual size of the flash chip's memory in bytes
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
// The Meadow function that initialized qspi used a fixed value defined
// in CONFIG_STM32F7_QSPI_FLASH_SIZE as the flash size. Since we've
// been called here the size must be wrong. Therefore, we must override the
// initial Nuttx code configured register values.
// Note: If the Nuttx implementaion in stm32_qspi.c changes this function may
// also need change.
void stm32f7_qspi_hw_reinitialize(int flashSize)
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
