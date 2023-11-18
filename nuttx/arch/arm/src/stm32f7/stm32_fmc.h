/****************************************************************************
 * arch/arm/src/stm32f7/stm32_fmc.h
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

#ifndef __ARCH_ARM_SRC_STM32F7_STM32_FMC_H
#define __ARCH_ARM_SRC_STM32F7_STM32_FMC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <sys/types.h>
#include <stdbool.h>

#include "hardware/stm32_fmc.h"

#ifndef __ASSEMBLY__

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

#  define FMC_SDRAM_CR_COLBITS_8                       0x00000000
#  define FMC_SDRAM_CR_COLBITS_9                       0x00000001
#  define FMC_SDRAM_CR_COLBITS_10                      0x00000002
#  define FMC_SDRAM_CR_COLBITS_11                      0x00000003

#  define FMC_SDRAM_CR_ROWBITS_11                      0x00000000
#  define FMC_SDRAM_CR_ROWBITS_12                      0x00000004
#  define FMC_SDRAM_CR_ROWBITS_13                      0x00000008

#  define FMC_SDRAM_CR_WIDTH_8                         0x00000000
#  define FMC_SDRAM_CR_WIDTH_16                        0x00000010
#  define FMC_SDRAM_CR_WIDTH_32                        0x00000020

#  define FMC_SDRAM_CR_BANKS_2                         0x00000000
#  define FMC_SDRAM_CR_BANKS_4                         0x00000040

#  define FMC_SDRAM_CR_CASLAT_1                        0x00000080
#  define FMC_SDRAM_CR_CASLAT_2                        0x00000100
#  define FMC_SDRAM_CR_CASLAT_3                        0x00000180

#  define FMC_SDRAM_CR_WRITE_PROT                      0x00000200

#  define FMC_SDRAM_CR_SDCLK_DISABLE                   0x00000000
#  define FMC_SDRAM_CR_SDCLK_2X                        0x00000800
#  define FMC_SDRAM_CR_SDCLK_3X                        0x00000C00

#  define FMC_SDRAM_CR_BURST_READ                      0x00001000

#  define FMC_SDRAM_CR_RPIPE_0                         0x00000000
#  define FMC_SDRAM_CR_RPIPE_1                         0x00002000
#  define FMC_SDRAM_CR_RPIPE_2                         0x00004000

#  define FMC_SDRAM_TR_TMRD_SHIFT                      0
#  define FMC_SDRAM_TR_TXSR_SHIFT                      4
#  define FMC_SDRAM_TR_TRAS_SHIFT                      8
#  define FMC_SDRAM_TR_TRC_SHIFT                       12
#  define FMC_SDRAM_TR_TWR_SHIFT                       16
#  define FMC_SDRAM_TR_TRP_SHIFT                       20
#  define FMC_SDRAM_TR_TRCD_SHIFT                      24

#  define FMC_SDRAM_MODE_CMD_NORMAL                    0
#  define FMC_SDRAM_MODE_CMD_CLK_ENABLE                1
#  define FMC_SDRAM_MODE_CMD_PALL                      2
#  define FMC_SDRAM_MODE_CMD_AUTO_REFRESH              3
#  define FMC_SDRAM_MODE_CMD_LOAD_MODE                 4
#  define FMC_SDRAM_MODE_CMD_SELF_REFRESH              5
#  define FMC_SDRAM_MODE_CMD_POWER_DOWN                6

#  define FMC_SDRAM_CMD_BANK_1                         0x00000010
#  define FMC_SDRAM_CMD_BANK_2                         0x00000008

#  define FMC_SDRAM_AUTO_REFRESH_SHIFT                 5
#  define FMC_SDRAM_MODEREG_SHIFT                      9

#  define FMC_SDRAM_MODEREG_BURST_LENGTH_1             (0x0000 << FMC_SDRAM_MODEREG_SHIFT)
#  define FMC_SDRAM_MODEREG_BURST_LENGTH_2             (0x0001 << FMC_SDRAM_MODEREG_SHIFT)
#  define FMC_SDRAM_MODEREG_BURST_LENGTH_4             (0x0002 << FMC_SDRAM_MODEREG_SHIFT)
#  define FMC_SDRAM_MODEREG_BURST_LENGTH_8             (0x0004 << FMC_SDRAM_MODEREG_SHIFT)
#  define FMC_SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      (0x0000 << FMC_SDRAM_MODEREG_SHIFT)
#  define FMC_SDRAM_MODEREG_BURST_TYPE_INTERLEAVED     (0x0008 << FMC_SDRAM_MODEREG_SHIFT)
#  define FMC_SDRAM_MODEREG_CAS_LATENCY_2              (0x0020 << FMC_SDRAM_MODEREG_SHIFT)
#  define FMC_SDRAM_MODEREG_CAS_LATENCY_3              (0x0030 << FMC_SDRAM_MODEREG_SHIFT)
#  define FMC_SDRAM_MODEREG_OPERATING_MODE_STANDARD    (0x0000 << FMC_SDRAM_MODEREG_SHIFT)
#  define FMC_SDRAM_MODEREG_WRITEBURST_MODE_PROGRAMMED (0x0000 << FMC_SDRAM_MODEREG_SHIFT)
#  define FMC_SDRAM_MODEREG_WRITEBURST_MODE_SINGLE     (0x0200 << FMC_SDRAM_MODEREG_SHIFT)

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: stm32_fmc_init
 *
 * Description:
 *   Initialize the FMC peripheral.
 *
 ****************************************************************************/

void stm32_fmc_init(void);

/****************************************************************************
 * Name: stm32_fmc_sdram_write_protect
 *
 * Description:
 *   Enable/Disable writes to an SDRAM.
 *
 ****************************************************************************/

void stm32_fmc_sdram_write_protect(int bank, bool state);

/****************************************************************************
 * Name: stm32_fmc_sdram_set_refresh_rate
 *
 * Description:
 *   Set the SDRAM refresh rate.
 *
 ****************************************************************************/

void stm32_fmc_sdram_set_refresh_rate(int count);

/****************************************************************************
 * Name: stm32_fmc_sdram_enable
 *
 * Description:
 *   Enable FMC SDRAM. Do this after issue refresh rate.
 *
 ****************************************************************************/

void stm32_fmc_sdram_enable(void);

/****************************************************************************
 * Name: stm32_fmc_sdram_set_timing
 *
 * Description:
 *   Set the SDRAM timing parameters.
 *
 ****************************************************************************/

void stm32_fmc_sdram_set_timing(int bank, uint32_t timing);

/****************************************************************************
 * Name: stm32_fmc_sdram_set_control
 *
 * Description:
 *   Set the SDRAM control parameters.
 *
 ****************************************************************************/

void stm32_fmc_sdram_set_control(int bank, uint32_t ctrl);

/****************************************************************************
 * Name: stm32_fmc_sdram_command
 *
 * Description:
 *   Send a command to the SDRAM.
 *
 ****************************************************************************/

void stm32_fmc_sdram_command(uint32_t cmd);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __ARCH_ARM_SRC_STM32F7_STM32_FMC_H */
