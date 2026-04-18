/****************************************************************************
 * arch/xtensa/src/common/espressif/esp_ws2812.h
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

#ifndef __ARCH_XTENSA_SRC_COMMON_ESPRESSIF_ESP_WS2812_H
#define __ARCH_XTENSA_SRC_COMMON_ESPRESSIF_ESP_WS2812_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <debug.h>
#include <stdbool.h>

#ifndef __ASSEMBLY__
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

#ifdef CONFIG_WS2812

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: esp_ws2812_setup
 *
 * Description:
 *   This function sets up a WS2812 device instance. It allocates memory for
 *   the device structures, initializes the device with the provided
 *   parameters, and registers the device with the system.
 *
 * Input Parameters:
 *   path         - The device path.
 *   rmt          - Pointer to the RMT device structure.
 *   pixel_count  - The number of pixels in the WS2812 strip.
 *   has_white    - Flag indicating if the WS2812 strip includes a white LED.
 *
 * Returned Value:
 *   Returns a pointer to the WS2812 device structure on successful setup;
 *   NULL is returned on any failure, with errno set appropriately.
 *
 ****************************************************************************/

struct ws2812_dev_s *esp_ws2812_setup(const char *path,
                                        struct rmt_dev_s *rmt,
                                        uint16_t    pixel_count,
                                        bool        has_white);

/****************************************************************************
 * Name: esp_ws2812_release
 *
 * Description:
 *   This function releases a previously opened WS2812 device instance. It
 *   checks if the device is currently open, and if not, it frees the private
 *   data structure and sets the private field of the device to NULL. If the
 *   device is still open, it returns an error.
 *
 * Input Parameters:
 *   driver - Pointer to the instance of the WS2812 device driver to be
 *            released.
 *
 * Returned Value:
 *   Returns OK on successful release of the device; a negated errno value
 *   is returned on any failure.
 *
 ****************************************************************************/

int esp_ws2812_release(void * driver);

#endif

<<<<<<<< HEAD:nuttx/arch/xtensa/src/common/espressif/esp_ws2812.h
========
#ifdef CONFIG_SENSORS_AK09912_SCU
/****************************************************************************
 * Name: ak09912_init
 *
 * Description:
 *   Initialize AK09912 magnetometer device
 *
 * Input Parameters:
 *   i2c     - An instance of the I2C interface to use to communicate with
 *             AK09912
 *   port    - I2C port number
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int ak09912_init(struct i2c_master_s *i2c, int port);

/****************************************************************************
 * Name: ak09912_scu_register
 *
 * Description:
 *   Register the AK09912 character device as 'devpath'
 *
 * Input Parameters:
 *   devpath - The full path to the driver to register. E.g., "/dev/mag0"
 *   minor   - The number of sequencer
 *   i2c     - An instance of the I2C interface to use to communicate with
 *             AK09912
 *   port    - I2C port number
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int ak09912_scu_register(const char *devpath, int minor,
                         struct i2c_master_s *i2c, int port);

#endif /* CONFIG_SENSORS_AK09912_SCU */

>>>>>>>> 9e8753d6256619a7bfa72f4596eb1e481f1c97a2:nuttx/boards/arm/cxd56xx/spresense/include/cxd56_ak09912.h
#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __ARCH_XTENSA_SRC_COMMON_ESPRESSIF_ESP_WS2812_H */
