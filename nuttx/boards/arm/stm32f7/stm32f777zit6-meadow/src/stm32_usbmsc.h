/****************************************************************************
 * boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32_usbmsc.h
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

#ifndef __BOARDS_ARM_STM32F7_STM32F777ZIT6_MEADOW_SRC_STM32_USBMSC_H
#define __BOARDS_ARM_STM32F7_STM32F777ZIT6_MEADOW_SRC_STM32_USBMSC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>

#ifdef CONFIG_USBMSC

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The block device bound to USB mass storage LUN 0.
 *
 * CONFIG_MEADOW_USBMSC_RAMDISK selects the Stage 1 bring-up vehicle: a small
 * FAT12 RAM disk, which exists only to validate the composite CDC/ACM plus
 * mass storage descriptors against a host that can already read the medium.
 * Without it the LUN falls back to the SD card, which is already vfat.
 *
 * Neither of these is the eventual target.  That is a synthetic FAT view of
 * the LittleFS volume on /meadow0, which will replace the read and write
 * bodies of the block driver behind /dev/meadowmsc0.
 */

#ifdef CONFIG_MEADOW_USBMSC_RAMDISK
#  define MEADOW_USBMSC_LUN0_DEVPATH "/dev/meadowmsc0"
#else
#  define MEADOW_USBMSC_LUN0_DEVPATH "/dev/mmcsd0"
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_usbmsc_lun_available
 *
 * Description:
 *   Report whether the block device backing mass storage LUN 0 came up.
 *
 *   board_composite0_connect() uses this to decide whether to advertise the
 *   mass storage interface at all.  A storage fault must not cost us the
 *   CDC/ACM interface, because that is how HCOM talks to the host.
 *
 *   This is about the block driver existing, not about media being present.
 *   The interface is advertised in both modes so that the USB descriptors do
 *   not change shape between boots; whether there is anything in the drive
 *   is decided separately, by whether the LUN gets bound.
 *
 * Returned Value:
 *   True if board_usbmsc_initialize() has successfully prepared the LUN.
 *
 ****************************************************************************/

bool meadow_usbmsc_lun_available(void);

#endif /* CONFIG_USBMSC */
#endif /* __BOARDS_ARM_STM32F7_STM32F777ZIT6_MEADOW_SRC_STM32_USBMSC_H */
