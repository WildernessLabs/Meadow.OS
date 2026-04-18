/****************************************************************************
 * arch/arm/include/cxd32xx/chip.h
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

#ifndef __ARCH_ARM_INCLUDE_CXD32XX_CHIP_H
#define __ARCH_ARM_INCLUDE_CXD32XX_CHIP_H

/****************************************************************************
 * Pre-processor Prototypes
 ****************************************************************************/

#define CXD32M4_SYSH_PRIORITY_MIN     0xe0 /* All bits[7:5] set is minimum priority */
#define CXD32M4_SYSH_PRIORITY_DEFAULT 0x80 /* Midpoint is the default */
#define CXD32M4_SYSH_PRIORITY_MAX     0x00 /* Zero is maximum priority */
#define CXD32M4_SYSH_PRIORITY_STEP    0x20 /* Steps between priorities */

#define NVIC_SYSH_PRIORITY_MIN        CXD32M4_SYSH_PRIORITY_MIN
#define NVIC_SYSH_PRIORITY_DEFAULT    CXD32M4_SYSH_PRIORITY_DEFAULT
#define NVIC_SYSH_PRIORITY_MAX        CXD32M4_SYSH_PRIORITY_MAX
#define NVIC_SYSH_PRIORITY_STEP       CXD32M4_SYSH_PRIORITY_STEP

<<<<<<<< HEAD:nuttx/arch/arm/include/cxd32xx/chip.h
#endif /* __ARCH_ARM_INCLUDE_CXD32XX_CHIP_H */
========
#define arm_dsb()  __asm__ __volatile__ ("dsb " : : : "memory")
#define arm_isb()  __asm__ __volatile__ ("isb " : : : "memory")
#define arm_dmb()  __asm__ __volatile__ ("dmb " : : : "memory")

#define ARM_DSB()  arm_dsb()
#define ARM_ISB()  arm_isb()
#define ARM_DMB()  arm_dmb()

#endif /* __ARCH_ARM_SRC_ARMV6_M_BARRIERS_H */
>>>>>>>> 5d8cdeaea847bcaf0b9145dd922e0a83045067ca:nuttx/arch/arm/src/armv6-m/barriers.h
