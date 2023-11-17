/****************************************************************************
 * boards/arm/stm32f7/stm32f777zit6-meadow/src/stm32f777zit6-meadow.h
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

#ifndef __BOARDS_ARM_MEADOW_SRC_STM32F777ZIT6_MEADOW__H
#define __BOARDS_ARM_MEADOW_SRC_STM32F777ZIT6_MEADOW__H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* procfs File System */

#ifdef CONFIG_FS_PROCFS
#  ifdef CONFIG_NSH_PROC_MOUNTPOINT
#    define STM32_PROCFS_MOUNTPOINT CONFIG_NSH_PROC_MOUNTPOINT
#  else
#    define STM32_PROCFS_MOUNTPOINT "/proc"
#  endif
#endif

#define GPIO_LD1        (GPIO_OUTPUT | GPIO_PUSHPULL | GPIO_SPEED_50MHz | GPIO_OUTPUT_CLEAR | GPIO_PORTA | GPIO_PIN1)
#define GPIO_LD2        (GPIO_OUTPUT | GPIO_PUSHPULL | GPIO_SPEED_50MHz | GPIO_OUTPUT_CLEAR | GPIO_PORTA | GPIO_PIN0)
#define GPIO_LD3        (GPIO_OUTPUT | GPIO_PUSHPULL | GPIO_SPEED_50MHz | GPIO_OUTPUT_CLEAR | GPIO_PORTA | GPIO_PIN2)

#define GPIO_LED_GREEN GPIO_LD1
#define GPIO_LED_BLUE  GPIO_LD2
#define GPIO_LED_RED   GPIO_LD3


#define BOARD_NGPIOIN	1
#define BOARD_NGPIOOUT	1
#define BOARD_NGPIOINT	1

#define GPIO_IN1          0
#define GPIO_OUT1         0 

#define GPIO_INT1         0



#define MGPIO_PIN_COUNT			16

#define MGPIO_PIN_TYPE_INPUT	0
#define MGPIO_PIN_TYPE_OUTPUT	1
#define MGPIO_PIN_TYPE_INT		2

#define MGPIO_SET_CONFIG        1
#define MGPIO_WRITE             3
#define MGPIO_READ              4

#define MUPD_SET_REGISTER       1
#define MUPD_GET_REGISTER       2
#define MUPD_UPDATE_REGISTER    3
#define MUPD_REGISTER_GPIO_IRQ  5

#define MUPD_PWM_SETUP     10
#define MUPD_PWM_SHUTDOWN  11
#define MUPD_PWM_START     12
#define MUPD_PWM_STOP      13

#define MUPD_I2C_SHUTDOWN  20
#define MUPD_I2C_DATA      21

#define MUPD_SPI_DATA      31
#define MUPD_SPI_SPEED      32
#define MUPD_SPI_MODE      33
#define MUPD_SPI_BITS      34

#define MUPD_DIR_ENUM       41

#define MUPD_GET_LAST_ERROR   51

#define MUPD_ESP32_COMMAND              61
#define MUPD_ESP32_GET_EVENT_RESULT     62

#define MUPD_PWR_RESET      71
#define MUPD_PWR_SLEEP1     72
#define MUPD_PWR_SLEEP2     73

#define MUPD_GET_DEVICE_INFO   81
#define MUPD_GET_SET_CONFIGURATION_VALUE   82

/* USB OTG FS */

#define GPIO_OTGFS_VBUS   (GPIO_INPUT|GPIO_FLOAT|GPIO_SPEED_100MHz|\
                           GPIO_OPENDRAIN|GPIO_PORTA|GPIO_PIN9)

#define GPIO_OTGFS_PWRON  (GPIO_OUTPUT|GPIO_FLOAT|GPIO_SPEED_100MHz|\
                           GPIO_PUSHPULL|GPIO_PORTG|GPIO_PIN6)

#ifdef CONFIG_USBHOST
#  define GPIO_OTGFS_OVER (GPIO_INPUT|GPIO_EXTI|GPIO_FLOAT|\
                           GPIO_SPEED_100MHz|GPIO_PUSHPULL|\
                           GPIO_PORTG|GPIO_PIN7)

#else
#  define GPIO_OTGFS_OVER (GPIO_INPUT|GPIO_FLOAT|GPIO_SPEED_100MHz|\
                           GPIO_PUSHPULL|GPIO_PORTG|GPIO_PIN7)
#endif


/* Pushbutton B1, labelled "User", is connected to GPIO PA0.  A high
 * value will be sensed when the button is depressed. Note that
 * the EXTI interrupt is configured.
 */

#define GPIO_BTN_USER      (GPIO_INPUT | GPIO_FLOAT | GPIO_EXTI | GPIO_PORTA | GPIO_PIN0)

/* Sporadic scheduler instrumentation.
 * This configuration has been used for evaluating the NuttX sporadic
 * scheduler.
 * In this evaluation, two GPIO outputs are used. One indicating the priority
 * (high or low) of the sporadic thread and one indicating where the thread
 * is running or not.
 *
 * There is nothing special about the pin selections:
 *
 *   Arduino D2 PJ1 - Indicates priority1
 *   Arduino D4 PJ0 - Indicates that the thread is running
 */

#define GPIO_SCHED_HIGHPRI (GPIO_OUTPUT | GPIO_PUSHPULL | GPIO_SPEED_50MHz | GPIO_OUTPUT_CLEAR | \
                            GPIO_PORTJ | GPIO_PIN1)
#define GPIO_SCHED_RUNNING (GPIO_OUTPUT | GPIO_PUSHPULL | GPIO_SPEED_50MHz | GPIO_OUTPUT_CLEAR | \
                            GPIO_PORTJ | GPIO_PIN0)

#define GPIO_OTGFS_VBUS    (GPIO_INPUT|GPIO_FLOAT|GPIO_SPEED_100MHz|\
                            GPIO_OPENDRAIN|GPIO_PORTA|GPIO_PIN9)

/****************************************************************************
 * Public data
 ****************************************************************************/

#ifndef __ASSEMBLY__

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: stm32_bringup
 *
 * Description:
 *   Perform architecture-specific initialization
 *
 *   CONFIG_BOARD_LATE_INITIALIZE=y :
 *     Called from board_late_initialize().
 *
 *   CONFIG_BOARD_LATE_INITIALIZE=y && CONFIG_BOARDCTL=y :
 *     Called from the NSH library
 *
 ****************************************************************************/

int stm32_bringup(void);

/****************************************************************************
 * Name: stm32_pwm_setup
 *
 * Description:
 *   Initialize PWM and register the PWM device.
 *
 ****************************************************************************/

#ifdef CONFIG_PWM
int stm32_pwm_setup(void);
#endif

/****************************************************************************
 * Name: stm32_spidev_initialize
 *
 * Description:
 *   Called to configure SPI chip select GPIO pins for the stm32f769i-disco
 *   board.
 *
 ****************************************************************************/

void weak_function stm32_spidev_initialize(void);

/****************************************************************************
 * Name: arch_sporadic_initialize
 *
 * Description:
 *   This configuration has been used for evaluating the NuttX sporadic
 *   scheduler.
 *
 ****************************************************************************/

#ifdef CONFIG_SPORADIC_INSTRUMENTATION
void arch_sporadic_initialize(void);
#endif

/****************************************************************************
 * Name: stm32_sdram_initialize
 *
 * Description:
 *   Called from stm32_bringup to initialize external SDRAM access.
 *
 ****************************************************************************/

#ifdef CONFIG_STM32F7_FMC
void stm32_sdram_initialize(void);
void stm32_disablefmc(void);
#endif

void stm32_quadspi_init(void);

#endif /* __ASSEMBLY__ */
#endif /* __BOARDS_ARM_MEADOW_SRC_STM32F777ZIT6_MEADOW_H */
