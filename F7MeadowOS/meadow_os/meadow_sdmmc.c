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
#include <nuttx/clock.h>
#include <nuttx/sdio.h>
#include <nuttx/mmcsd.h>
#include <nuttx/fs/automount.h>

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

#ifdef HAVE_MEADOW_NCD
// The NuttX automounter mounts and unmounts the SD-Card (Issue #88). It waits
// for the card detect pin to be stable for DDELAY before acting, and retries
// an unmount every UDELAY while files on the card are still open.
#define MEADOW_SDCARD_AUTOMOUNT_DDELAY  MSEC2TICK(1000)
#define MEADOW_SDCARD_AUTOMOUNT_UDELAY  MSEC2TICK(2000)
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct sdio_dev_s *_SdioDev;

#ifdef HAVE_MEADOW_NCD
static bool _PreviousSdInserted = false;
static bool _CurrentSdInserted;

// Set by the automounter through the hooks below
static automount_handler_t _AutomountHandler;
static FAR void *_AutomountArg;
static bool _AutomountEnabled;

static int meadow_sdcard_am_attach(FAR const struct automount_lower_s *lower,
                                   automount_handler_t isr, FAR void *arg);
static void meadow_sdcard_am_enable(FAR const struct automount_lower_s *lower,
                                    bool enable);
static bool meadow_sdcard_am_inserted(FAR const struct automount_lower_s *lower);

static const struct automount_lower_s _AutomountLower =
{
  .fstype     = MEADOW_SDCARD_FILE_SYS_TYPE,
  .blockdev   = MEADOW_SDCARD_BLOCK_NAME,
  .mountpoint = MEADOW_SDCARD_MOUNT_POINT_NAME,
  .ddelay     = MEADOW_SDCARD_AUTOMOUNT_DDELAY,
  .udelay     = MEADOW_SDCARD_AUTOMOUNT_UDELAY,
  .attach     = meadow_sdcard_am_attach,
  .enable     = meadow_sdcard_am_enable,
  .inserted   = meadow_sdcard_am_inserted,
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_sdcard_am_attach, meadow_sdcard_am_enable,
 *       meadow_sdcard_am_inserted
 *
 * Description:
 *   The automounter's hooks. The card detect interrupt is Meadow's own (see
 *   stm32_ncd_interrupt()), so attach only saves the automounter's handler
 *   and enable only gates the call to it.
 *
 ****************************************************************************/

#ifdef HAVE_MEADOW_NCD
static int meadow_sdcard_am_attach(FAR const struct automount_lower_s *lower,
                                   automount_handler_t isr, FAR void *arg)
{
  irqstate_t flags = enter_critical_section();
  _AutomountHandler = isr;
  _AutomountArg = arg;
  leave_critical_section(flags);
  return OK;
}

static void meadow_sdcard_am_enable(FAR const struct automount_lower_s *lower,
                                    bool enable)
{
  irqstate_t flags = enter_critical_section();
  _AutomountEnabled = enable;
  leave_critical_section(flags);
}

static bool meadow_sdcard_am_inserted(FAR const struct automount_lower_s *lower)
{
  // The pin is pulled low when a card is inserted
  return !stm32_gpioread(GPIO_MEADOW_SDIO_NCD);
}
#endif

/****************************************************************************
 * Name: stm32_ncd_interrupt
 *
 * Description:
 *   Card detect interrupt handler. Tells the MMC/SD driver about a change
 *   at once, and passes every edge to the automounter, which debounces it.
 *
 ****************************************************************************/

#ifdef HAVE_MEADOW_NCD
static int stm32_ncd_interrupt(int irq, FAR void *context, void *arg)
{
  _CurrentSdInserted = !stm32_gpioread(GPIO_MEADOW_SDIO_NCD);

  // Contact bounce gives extra edges with no change, only log a real one
  if (_CurrentSdInserted != _PreviousSdInserted)
  {
    syslog(LOG_INFO, "%s@%d-SDMMC interrupt. Card was %s, now is %s\n",
                __FILE__, __LINE__,
                _PreviousSdInserted ? "In" : "Out",
                _CurrentSdInserted ? "In" : "Out");

    sdio_mediachange(_SdioDev, _CurrentSdInserted);
    _PreviousSdInserted = _CurrentSdInserted;
  }

  if (_AutomountEnabled && _AutomountHandler != NULL)
  {
    _AutomountHandler(&_AutomountLower, _AutomountArg, _CurrentSdInserted);
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

  // syslog(LOG_DEBUG, "Initializing SDIO slot %d\n", SDIO_SLOTNO);

  _SdioDev = sdio_initialize(SDIO_SLOTNO);

  if (!_SdioDev)
  {
    syslog(LOG_ERR, "ERROR: Failed to initialize SDIO slot %d\n", SDIO_SLOTNO);
    return -ENODEV;
  }

  // Now bind the SDIO interface to the MMC/SD driver
  // syslog(LOG_DEBUG, "Bind SDIO to the MMC/SD driver, minor=%d\n", SDIO_MINOR);

  // Also setup insert/remove card interrupt callback
  ret = mmcsd_slotinitialize(SDIO_MINOR, _SdioDev);
  if (ret != OK)
  {
    syslog(LOG_ERR, "%s@%d-ERROR: Failed to bind SDIO to the MMC/SD driver: %d\n",
      __FILE__, __LINE__, ret);
    return ret;
  }

  // syslog(LOG_DEBUG, "Successfully bound SDIO to the MMC/SD driver\n");

#ifdef HAVE_MEADOW_NCD
  // Use SD card detect pin to check if a card is inserted
  _CurrentSdInserted = !stm32_gpioread(GPIO_MEADOW_SDIO_NCD);

  // syslog(LOG_MDIAG, "%s@%d-At startup. Card was %s, now is %s\n",
  //           __FILE__, __LINE__,
  //           _PreviousSdInserted ? "In" : "Out",
  //           _CurrentSdInserted ? "In" : "Out");

  sdio_mediachange(_SdioDev, _CurrentSdInserted);
  _PreviousSdInserted = _CurrentSdInserted;

  // From here on the automounter owns mounting the SD-Card on /sdcard. It
  // reads the card detect pin itself and does the first mount after
  // MEADOW_SDCARD_AUTOMOUNT_DDELAY on the low priority work queue, so boot
  // does not wait for it. Nothing else should mount or unmount the card.
  if (automount_initialize(&_AutomountLower) == NULL)
  {
    syslog(LOG_ERR, "%s@%d-ERROR: SD-Card automounter initialization failed\n",
      __FILE__, __LINE__);
    return -ENOMEM;
  }

  syslog(LOG_DEBUG, "%s@%d-SD-Card configured, card %s\n",
     __FILE__, __LINE__, _CurrentSdInserted ? "inserted" : "not inserted");

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