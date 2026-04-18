/****************************************************************************
 * arch/arm/include/mps/chip.h
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

#ifndef __ARCH_ARM_INCLUDE_MPS_CHIP_H
#define __ARCH_ARM_INCLUDE_MPS_CHIP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Prototypes
 ****************************************************************************/

<<<<<<<< HEAD:nuttx/arch/arm/include/mps/chip.h
#define NVIC_SYSH_PRIORITY_MIN     0xe0 /* Bits [7:5] set in minimum priority */
========
#define PINMUX_USART1_RXD_0        (GPIO_PERIPH | GPIO_FUNCA | GPIO_PORTA | 5)
#define PINMUX_USART1_TXD_0        (GPIO_PERIPH | GPIO_FUNCA | GPIO_PORTA | 6)
>>>>>>>> 5d8cdeaea847bcaf0b9145dd922e0a83045067ca:nuttx/arch/avr/src/at32uc3/at32uc3a_pinmux.h

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

<<<<<<<< HEAD:nuttx/arch/arm/include/mps/chip.h
#endif /* __ARCH_ARM_INCLUDE_MPS_CHIP_H */
========
#endif /* __ARCH_AVR_SRC_AT32UC3_AT32UC3A_PINMUX_H */
>>>>>>>> 5d8cdeaea847bcaf0b9145dd922e0a83045067ca:nuttx/arch/avr/src/at32uc3/at32uc3a_pinmux.h
