/****************************************************************************
<<<<<<<< HEAD:nuttx/boards/arm/nrf53/common/src/nrf53_bmm150.c
 * boards/arm/nrf53/common/src/nrf53_bmm150.c
========
 * boards/arm/stm32/common/src/stm32_mfrc522.c
>>>>>>>> 5d8cdeaea847bcaf0b9145dd922e0a83045067ca:nuttx/boards/arm/stm32/common/src/stm32_mfrc522.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <stdio.h>

<<<<<<<< HEAD:nuttx/boards/arm/nrf53/common/src/nrf53_bmm150.c
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/sensors/bmm150.h>

#include "nrf53_i2c.h"
========
#include <nuttx/board.h>
#include <nuttx/spi/spi.h>
#include <nuttx/contactless/mfrc522.h>

#include "stm32.h"
#include "stm32_spi.h"

#if defined(CONFIG_SPI) && defined(CONFIG_STM32_SPI1) && defined(CONFIG_CL_MFRC522)
>>>>>>>> 5d8cdeaea847bcaf0b9145dd922e0a83045067ca:nuttx/boards/arm/stm32/common/src/stm32_mfrc522.c

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nrf53_bmm150_init
 *
 * Description:
 *   Initialize and register the BMM150 as uorb sensor
 *
 * Input Parameters:
 *   devno - The user specifies device number, from 0.
 *   busno - I2C bus number
 *   addr  - The I2C address
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int nrf53_bmm150_init(int devno, int busno, uint8_t addr)
{
<<<<<<<< HEAD:nuttx/boards/arm/nrf53/common/src/nrf53_bmm150.c
  struct bmm150_config_s  config;
  struct i2c_master_s       *i2c;
  int                        ret;

  sninfo("Initializing BMM150!\n");

  /* Initialize I2C */

  i2c = nrf53_i2cbus_initialize(busno);
  if (!i2c)
========
  struct spi_dev_s *spi;
  int ret;
  spi = stm32_spibus_initialize(MFRC522_SPI_PORTNO);
  if (!spi)
>>>>>>>> 5d8cdeaea847bcaf0b9145dd922e0a83045067ca:nuttx/boards/arm/stm32/common/src/stm32_mfrc522.c
    {
      return -ENODEV;
    }

  /* Then register the barometer sensor */

  config.i2c  = i2c;
  config.addr = addr;

  ret = bmm150_register_uorb(devno, &config);
  if (ret < 0)
    {
      snerr("ERROR: Error registering BMM150\n");
    }

  return ret;
}
