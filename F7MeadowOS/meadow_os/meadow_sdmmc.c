/****************************************************************************
 * Meadow.OS/meadow_os/meadow_sdmmc.c
 *
 *   Copyright (C) 2022-2023 Wilderness Labs. All rights reserved.
 *
 *   Copyright (C) 2016-2017 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
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

// This module initially copied from
// /nuttx/configs/stm32f746-ws/src/stm32_sdmmc.c

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdio.h>
#include <debug.h>
#include <errno.h>

#if defined (CONFIG_STM32F7_SDMMC2)

#include <nuttx/irq.h>
#include <nuttx/sdio.h>
#include <nuttx/mmcsd.h>
#include <sys/mount.h>

#include "stm32_sdmmc.h"
#include <meadow/hcom_shared_common.h>
#include <meadow/meadow_sdmmc.h>
#include "stm32_gpio.h"

#if defined (CONFIG_SD_CARD_TESTS)
#pragma message "(--) meadow_sdmmc.c"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
/* Configuration ************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct sdio_dev_s *_SdioDev;

#ifdef HAVE_MEADOW_NCD
static bool _PreviousSdInserted = false;
static bool _CurrentSdInserted;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: stm32_ncd_interrupt
 *
 * Description:
 *   Card detect interrupt handler.
 *
 ****************************************************************************/

#ifdef HAVE_MEADOW_NCD
static int stm32_ncd_interrupt(int irq, FAR void *context, void *arg)
{
  _CurrentSdInserted = !stm32_gpioread(GPIO_MEADOW_SDIO_NCD);

  syslog(LOG_INFO, "%s@%d-SDMMC interrupt. Card was %s, now is %s\n",
              __FILE__, __LINE__,
              _PreviousSdInserted ? "In" : "Out",
              _CurrentSdInserted ? "In" : "Out");

  if (_CurrentSdInserted != _PreviousSdInserted)
  {
    sdio_mediachange(_SdioDev, _CurrentSdInserted);
    _PreviousSdInserted = _CurrentSdInserted;
  }

  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: stm32_sdio_initialize_meadow
 *
 * Description:
 *   Initialize SDIO-based MMC/SD card support
 *
 ****************************************************************************/

int stm32_sdio_initialize_meadow(void)
{
  int ret;

#ifdef HAVE_MEADOW_NCD
  // Configure the card detect GPIO PG6 defined in
  // Meadow.OS/include/meadow/meadow_sdmmc.h
  //
  // Note: the interrupt handler for this pin is NOT registered here.
  // stm32_gpiosetevent() synchronously unmasks the NVIC/EXTI path before
  // returning, so registering it this early would let stm32_ncd_interrupt()
  // fire (e.g. a card already inserted at boot, or an EXTI edge latched
  // during pin reconfiguration) before _SdioDev is assigned by
  // sdio_initialize() below, or before the initial _CurrentSdInserted/
  // _PreviousSdInserted state is established -- either a NULL-pointer crash
  // in sdio_mediachange() or a race on those unsynchronized globals. The
  // event is armed at the end of this function instead, once _SdioDev is
  // valid, the block driver is bound, and the initial card state matches
  // real hardware.
  stm32_configgpio(GPIO_MEADOW_SDIO_NCD);
#endif

  syslog(LOG_DEBUG, "Initializing SDIO slot %d\n", SDIO_SLOTNO);

  _SdioDev = sdio_initialize(SDIO_SLOTNO);

  if (!_SdioDev)
  {
    syslog(LOG_ERR, "ERROR: Failed to initialize SDIO slot %d\n", SDIO_SLOTNO);
    return -ENODEV;
  }

  // Now bind the SDIO interface to the MMC/SD driver
  syslog(LOG_DEBUG, "Bind SDIO to the MMC/SD driver, minor=%d\n", SDIO_MINOR);

  // Also setup insert/remove card interrupt callback
  ret = mmcsd_slotinitialize(SDIO_MINOR, _SdioDev);
  if (ret != OK)
  {
    syslog(LOG_ERR, "ERROR: Failed to bind SDIO to the MMC/SD driver: %d\n", ret);
    return ret;
  }

  syslog(LOG_DEBUG, "Successfully bound SDIO to the MMC/SD driver\n");

#ifdef HAVE_MEADOW_NCD
  // Use SD card detect pin to check if a card is inserted
  _CurrentSdInserted = !stm32_gpioread(GPIO_MEADOW_SDIO_NCD);

  // syslog(LOG_MDIAG, "%s@%d-At startup. Card was %s, now is %s\n",
  //           __FILE__, __LINE__,
  //           _PreviousSdInserted ? "In" : "Out",
  //           _CurrentSdInserted ? "In" : "Out");

  sdio_mediachange(_SdioDev, _CurrentSdInserted);

  // Note: I found no way to auto-mount via the interrupts, directly or using
  // Nuttx work threads. I think it might be possible to send a signal to HCOM
  // and use it's thread, but I didn't have time to attempt implementing this.
  // Nuttx does have an Automount feature but with the rehosting to Nuttx V12
  // in progress decided to wait for it. (12Dec23 Peter)
  _PreviousSdInserted = _CurrentSdInserted;
  if(_CurrentSdInserted)
  {
    syslog(LOG_DEBUG, "%s@%d-At startup SD-Card is inserted, will attempt to mount\n",
       __FILE__, __LINE__);

    ret = mount(MEADOW_SDCARD_BLOCK_NAME, MEADOW_SDCARD_MOUNT_POINT_NAME,
      MEADOW_SDCARD_FILE_SYS_TYPE, 0, NULL);
    if(ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-ERROR: Startup - SDCard mount failed. ret:%d, errno:%d\n",
        __FILE__, __LINE__, ret, errno);
      return ret;
    }
  }

  // Now that _SdioDev is valid, the block driver is bound, and the initial
  // card state matches real hardware, it's safe to register the interrupt
  // handler for the card detect pin -- see the comment above
  // stm32_configgpio(GPIO_MEADOW_SDIO_NCD) for why this can't happen
  // earlier.
  (void)stm32_gpiosetevent(
    GPIO_MEADOW_SDIO_NCD,
    true,
    true,
    true,
    stm32_ncd_interrupt,
    NULL);
#else
  /* Assume that the SD card is inserted.  What choice do we have? */
  sdio_mediachange(_SdioDev, true);
#endif

  return OK;
}

#else

int stm32_sdio_initialize_meadow(void)
{
  return OK;
}

#endif