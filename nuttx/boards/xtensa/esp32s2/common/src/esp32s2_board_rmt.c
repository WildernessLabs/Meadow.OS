/****************************************************************************
 * boards/xtensa/esp32s2/common/src/esp32s2_board_rmt.c
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

<<<<<<<< HEAD:nuttx/boards/xtensa/esp32s2/common/src/esp32s2_board_rmt.c
#include <nuttx/config.h>
========
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <nuttx/signal.h>
#include <arch/board/board.h>
>>>>>>>> 9e8753d6256619a7bfa72f4596eb1e481f1c97a2:nuttx/boards/arm/cxd56xx/common/src/cxd56_bcm20706.c

#include <errno.h>
#include <debug.h>
#include <stdio.h>

#include "xtensa.h"

#include <nuttx/kmalloc.h>
#include <nuttx/rmt/rmtchar.h>
#ifdef CONFIG_WS2812_NON_SPI_DRIVER
#include <nuttx/leds/ws2812.h>

#include "espressif/esp_ws2812.h"
#endif

#include "espressif/esp_rmt.h"

#ifdef CONFIG_ESP_RMT

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_rmt_rxinitialize
 *
 * Description:
 *   Initialize the RMT peripheral and register a RX device.
 *
 * Input Parameters:
 *   ch  - the RMT's channel that will be used
 *   pin - The pin used for the RX channel
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int board_rmt_rxinitialize(int ch, int pin)
{
  int ret;

  struct rmt_dev_s *rmt = esp_rmt_rx_init(ch, pin);

  ret = rmtchar_register(rmt);
  if (ret < 0)
    {
      rmterr("ERROR: rmtchar_register failed: %d\n", ret);
      return ret;
    }

  return ret;
}

/****************************************************************************
 * Name: board_rmt_txinitialize
 *
 * Description:
 *   Initialize the RMT peripheral and register a TX device.
 *
 * Input Parameters:
 *   ch  - the RMT's channel that will be used
 *   pin - The pin used for the TX channel
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int board_rmt_txinitialize(int ch, int pin)
{
  int ret;
  struct rmt_dev_s *rmt;
#ifdef CONFIG_WS2812_NON_SPI_DRIVER
  struct ws2812_dev_s *led;
#endif

  rmt = esp_rmt_tx_init(ch, pin);
  if (rmt == NULL)
    {
      rmterr("ERROR: esp_rmt_tx_init failed\n");
      return -ENODEV;
    }

  ret = rmtchar_register(rmt);
  if (ret < 0)
    {
      rmterr("ERROR: rmtchar_register failed: %d\n", ret);
      return ret;
    }

#ifdef CONFIG_WS2812_NON_SPI_DRIVER
  led = esp_ws2812_setup("/dev/leds0", rmt, CONFIG_WS2812_LED_COUNT, false);
  if (led == NULL)
    {
      rmterr("ERROR: esp_ws2812_setup failed\n");
      return -ENODEV;
    }
#endif

  return ret;
}
<<<<<<<< HEAD:nuttx/boards/xtensa/esp32s2/common/src/esp32s2_board_rmt.c
#endif
========

/****************************************************************************
 * Name: board_bluetooth_uart_pin_cfg
 *
 * Description:
 *   Setup UART pin configuration for bcm20706.
 *
 ****************************************************************************/

int board_bluetooth_uart_pin_cfg(void)
{
  int ret = 0;

  /* BCM20706 evaluation board might set pull-down.
   * Set float for UART2 CTS
   */

  ret = board_gpio_config(PIN_UART2_CTS, 1, 1, 0, 0);

  return ret;
}

/****************************************************************************
 * Name: board_bluetooth_reset
 *
 * Description:
 *   Reset BCM20706.
 *
 ****************************************************************************/

void board_bluetooth_reset(void)
{
  cxd56_gpio_write(BCM20707_RST_N, false);
  nxsig_usleep(BCM20707_RST_DELAY);
  cxd56_gpio_write(BCM20707_RST_N, true);
  nxsig_usleep(BCM20707_RST_DELAY);
}

/****************************************************************************
 * Name: board_bluetooth_power_control
 *
 * Description:
 *   Power ON/OFF BCM20706.
 *
 ****************************************************************************/

int board_bluetooth_power_control(bool en)
{
  int ret = 0;
  ret = board_power_control(POWER_BTBLE, en);
  return ret;
}

/****************************************************************************
 * Name: board_bluetooth_enable_sleep
 *
 * Description:
 *   Sleep mode ON/OFF BCM20706.
 *
 ****************************************************************************/

void board_bluetooth_enable_sleep(bool en)
{
  cxd56_gpio_write(BCM20707_DEV_WAKE, en);
}
>>>>>>>> 9e8753d6256619a7bfa72f4596eb1e481f1c97a2:nuttx/boards/arm/cxd56xx/common/src/cxd56_bcm20706.c
