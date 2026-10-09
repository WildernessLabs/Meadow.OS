/****************************************************************************
 * Meadow.OS/pwrmgmt/pwrmgmt_control.c
 * 
 *   Copyright (C) 2022-2024 Wilderness Labs. All rights reserved.
 *   Author:  Wilderness Labs
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

// This module controls the power management features (low-power stop mode) of
// the Meadow F7.

// Note: Nuttx has it's own power management implementation but after studying
// it, I decided to not use it because it made some assumptions about behavior
// that I thought were not in line with how Meadow was to operate. That said
// I did use the Nuttx implementation for "inspiration". Peter Moody 25Mar22
// It may be that Nuttx's power management is now useable, but for the Rebase
// project, we'll stick with the Legacy code. Peter Moody 21Jul26.

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <syslog.h>
#include <nuttx/irq.h>
#include <nuttx/power/pm.h>
#include <nuttx/semaphore.h>

#include "arm_internal.h"
#include "stm32_pm.h"
#include "stm32_pwr.h"    // FOR TESTING

#include <meadow/hcom_shared_common.h>
#include <meadow/meadow_pwr_mgmt.h>
#include <meadow/hcom_traverse_ku.h>
#include "pwrmgmt_local.h"

#include "hardware/stm32f76xx77xx_pwr.h"
#include "hardware/stm32_exti.h"
#include "nvic.h"

#include <arch/board/board.h>
#include "stm32_gpio.h"

#include "hcom_nx/hcom_nx_common.h"

#include "../espcp/espcp_coprocessor.h"

#include "stm32_alarm.h"

#if defined (CONFIG_MEADOW_PWR_MGMT_SUPPORT)

#if !defined (CONFIG_ARCH_IDLE_CUSTOM)
#error "CONFIG_MEADOW_PWR_MGMT_SUPPORT requires CONFIG_ARCH_IDLE_CUSTOM"
#endif

// Diagnostic only
// #define USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>

// #pragma message "(--) pwrmgmt_control.c"

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/

#define PWR_MGMT_MAX_CALLBACKS_AVAILABLE (6)

/************************************************************************************
 * Private Data
 ************************************************************************************/

static sem_t _lowPwrCtrlEntry;

// Posted by the idle task (pwrmgmt_idle_check_and_enter_lowpower()) once it
// has finished performing a requested low-power transition; the requesting
// task in pwrmgmt_enter_stm32f7_stop_mode() blocks on this.
static sem_t _lowPwrRequestComplete;

// Space for callbacks for notification of entering low-power mode
static pwr_mgmt_notify_callback _regCallback[PWR_MGMT_MAX_CALLBACKS_AVAILABLE];

// The pending low-power request, handed off from whichever task calls
// pwrmgmt_enter_stm32f7_stop_mode() to the idle task. Only ever written by
// the requesting task (under _lowPwrCtrlEntry's mutual exclusion) before
// _lowPwrRequestPending is set, and only ever cleared/read by the idle
// task -- see Issue #436/#601 for why this replaced the previous
// _okayToUseWaitOps cross-task flag design.
static volatile bool _lowPwrRequestPending = false;
static uint32_t _lowPwrRequestedWakeupPeriod;
static int _lowPwrRequestResult;

#if defined (CONFIG_MEADOW_ESPCP_MANAGER)
// Hold the ESP32 in reset while in Stop mode (Issue #680). Only developer
// test 60 clears this, to measure Stop current with the ESP32 left running.
static bool _holdEsp32InResetInStop = true;
#endif

/************************************************************************************
 * Public Data
 ************************************************************************************/

/************************************************************************************
 * Private Functions
 ************************************************************************************/

// Notify subscribers that the power mode will change. This is not a full
// featured implementation. The number that can signup is fixed at build
// time and there's no unsubscribe.
//
// Possible future feature:
// To allow a registered recipient to postpone the entry into low-power
// would require first telling each recipient of the pending change. Each
// recipient can responds with yes or no. In either case the recipient is
// responsible to prevent entry into a state where it will get busy.
// If all respond yes, the low-power mode is entered immediately, by again
// notifying each callback that low-power transition is happening now.
// If one or more callbacks indicated that they were busy then entry into
// the low-power state is postponed and the caller is responsible to keep
// attempting to enter the low-power state until the busy situation passes.
static int pwrmgmt_notify_registered_modules(bool lpStart)
{
  int ret = OK;
  int slotOffset = 0;

  for(slotOffset = 0; slotOffset < PWR_MGMT_MAX_CALLBACKS_AVAILABLE; slotOffset++)
  {
    pwr_mgmt_notify_callback callback = _regCallback[slotOffset];

    if(callback == NULL)
    {
      continue;
    }

    // syslog(LOG_MDIAG, "%s@%d-Notifying - callback:%p, %s\n",
    //   __FILE__, __LINE__, callback, lpStart ? "true" : "false");

    // Notify registered recipient announcing what's about to happen
    ret = callback(lpStart);
    if(ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-Callback:%d returned:%d\n",
                __FILE__, __LINE__, slotOffset + 1, ret);
      return ret;
    }
  }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

//===============================================================
// Called from up_idle() (Idle_Task only) on every idle iteration. If a
// low-power stop-mode request is pending, performs the entire hardware
// transition synchronously right here -- clock switch, alarm config, the
// actual stop-mode wfi/wfe (inside pwrmgmt_enter_stop_mode()), and
// switching back -- then returns true so up_idle() knows not to also
// execute its own separate wfi/wfe this iteration.
//
// This function, and the whole hardware transition it performs, only ever
// runs on the Idle_Task. There is no longer a second task independently
// deciding to execute wfi/wfe, so there is nothing left to race: ordinary
// RTOS preemption of this function by a higher-priority task mid-
// transition is safe by construction, since execution simply resumes here
// once the idle task is next scheduled. This replaced the previous
// _okayToUseWaitOps cross-task flag design after two attempts to close
// that race with interrupt masking both caused a full boot failure on
// real hardware -- see Issue #436/#601.
bool pwrmgmt_idle_check_and_enter_lowpower(void)
{
  int ret;

  if(!_lowPwrRequestPending)
  {
    return false;
  }

  _lowPwrRequestPending = false;

  // Switch on LSI clock
  // Note: this must be early in this configuration process because it does
  // a backup domain reset, which would otherwise, clear some of the registers
  // configured in the following steps.
  ret = meadow_pwr_mgmt_use_lsi_for_rtc();
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error:\n", __FILE__, __LINE__);
    goto done;
  }

  // Configure Wakeup/Alarm hardware and stop period.
  // Using the RTC Alarm allows waking up at a future time. However, since
  // there's no year or month comparison available, only day of the month,
  // this only allows, at most, a period of one month ahead. This has been
  // limited to 28 days - 1 second so it is consistent and not different for
  // each month.
  ret = pwrmgmt_config_rtc_alarm_wakeup_seconds(_lowPwrRequestedWakeupPeriod);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error:\n", __FILE__, __LINE__);
    goto done;
  }

  //---------------------------------------------------------------------
  // Enter stop mode and wait for specified time to expire. Actually, not
  // "waiting" but stopped. The pwrmgmt_enter_stop_mode() call returns when
  // the F7 has returned to normal operation.
  ret = pwrmgmt_enter_stop_mode();
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error:\n", __FILE__, __LINE__);
    goto done;
  }

  // Running again
  //---------------------------------------------------------------------

  // Switch back to crystal controlled HSE clock.
  ret = meadow_pwr_mgmt_use_hse_for_rtc();
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error:\n", __FILE__, __LINE__);
  }

done:
  _lowPwrRequestResult = ret;
  nxsem_post(&_lowPwrRequestComplete);
  return true;
}

// This function is called when a module wants to subscribe for power
// management notifications.
int pwrmgmt_subscribe_for_low_pwr_notifications(pwr_mgmt_notify_callback callback)
{
  int slotOffset;
  int ret = -EBADSLT;

  // syslog(LOG_MDIAG, "%s@%d-Subscribing callback:%p\n",
  //   __FILE__, __LINE__, callback);

  // Guard the find-free-slot-then-write sequence against a concurrent
  // subscriber racing for the same slot. Uses a critical section rather
  // than _lowPwrCtrlEntry (the notify-side semaphore) since that one is
  // lazily initialized on first low-power entry, which may not have
  // happened yet when a module subscribes at startup.
  irqstate_t flags = enter_critical_section();

  // Find free slot
  for(slotOffset = 0; slotOffset < PWR_MGMT_MAX_CALLBACKS_AVAILABLE; slotOffset++)
  {
    if(_regCallback[slotOffset] == NULL)
    {
      // Found a slot save the callback
      _regCallback[slotOffset] = callback;
      ret = OK;
      break;
    }
  }

  leave_critical_section(flags);

  if(ret < 0)
  {
    syslog(LOG_ERR, "Notification for Low-Power failed. %d slots are not enough.\n",
              PWR_MGMT_MAX_CALLBACKS_AVAILABLE);
  }

  return ret;
}

//==============================================================
// This function is called during initialization and is responsible for calling
// the other initialization function within this block of code.
int meadow_power_mgmt_initialize(void)
{
  int ret = OK;

  for(int i = 0; i < PWR_MGMT_MAX_CALLBACKS_AVAILABLE; i++)
  {
    _regCallback[i] = NULL;
  }

  // Create semaphore to only allow single threaded access to
  // pwrmgmt_enter_stm32f7_stop_mode()
  nxsem_init(&_lowPwrCtrlEntry, 0, 1);

  // Posted by the idle task once it completes a requested transition;
  // starts at 0 so the requesting task blocks until that happens.
  nxsem_init(&_lowPwrRequestComplete, 0, 0);

  // Initialize internals needed for the LSI clock to be used with RTC
  ret = pwrmgmt_init_lsi_calib();
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error:\n", __FILE__, __LINE__);
    return ret;
  }
 
  ret = pwrmgmt_init_rtc_clk_switch();
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error:\n", __FILE__, __LINE__);
  }

  return ret;
}

#if defined (CONFIG_MEADOW_ESPCP_MANAGER)
//=======================================================================
// Developer test support: choose whether the ESP32 is held in reset during
// the next Stop mode. Normally true; test 60 sets it false for one sleep.
void pwrmgmt_set_esp32_reset_in_stop(bool holdInReset)
{
  _holdEsp32InResetInStop = holdInReset;
}
#endif

//=======================================================================
// This is the public entry point for mono to initiate entering stop mode.
// It contains the steps to put F7 into Stop mode and recover. If a
// registered module can't connect it will return -EBUSY (-16).
int pwrmgmt_enter_stm32f7_stop_mode(uint32_t wakeupPeriod)
{
  int ret;

  MEADOW_TRACE_INFORMATION( "Received command to sleep for %d seconds\n",
          wakeupPeriod);

  // Don't sleep for less than 2 seconds
  if(wakeupPeriod < 2)
    return PWRMGMT_SLEEP_SKIPPED_TOO_SHORT;

  // The STM32F7's internal alarm clock uses day of month and HH:mm:ss but not
  // the month or year. Therefore, the worse case, maximum length, of a delay
  // is 28 days minus 1 second. It could be longer during some months but for
  // consistency this establishes a known maximum.
  // ((28 days * 24 * 60 * 60 = 2419200) - 1 second) = 2419199
  if(wakeupPeriod > 2419199)
  {
    return -ETIME;      // -62
  }

  // Get the semaphore to insure only one caller at a time
  ret = nxsem_trywait(&_lowPwrCtrlEntry);
  if(ret < 0)
  {
    return -EALREADY;    // Error exit
  }

  // This will route a text message to app side and on to CLI.
  char *lowPowerNext = "Entering low-power mode\n";
  ret = hcom_traverse_ku_text_to_host(HCOM_HOST_REQUEST_TEXT_NEXT_LOW_PWR,
            lowPowerNext, strlen(lowPowerNext));
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Error attempting to send msg to host, ret:%d\n",
              __FILE__, __LINE__, ret);
    // Keep going, this is unavoidable
  }

  // Notify registered modules that low-power is about to begin.
  // Currently, (10Jul23) there are 3 modules that are notified before entering
  // a low-power state. These are hcom_host_receive, hcom_host_send,
  // hcom_stdxxx_redirect.
  // They have call hcom_via_nx_register_pwr_mgmt_callback() on the apps side.
  ret = pwrmgmt_notify_registered_modules(true);
  if(ret != OK)
  {
    // Some code module is busy.
    nxsem_post(&_lowPwrCtrlEntry);
    return -EBUSY;
  }

#if defined (CONFIG_MEADOW_ESPCP_MANAGER)
  // Hold the ESP32 in reset (EN low) while in Stop mode. Left running it
  // draws about 18 mA, more than half of Meadow's Stop current (Issue #680).
  // The F7 keeps driving the pin low in Stop mode. Skipped when a debugger is
  // attached to the ESP32 (reset_esp32_at_startup is 0).
  // Note: anything using the network when Meadow sleeps is not torn down
  // first. That needs an espcp teardown API (Issue #683).
  bool esp32HeldInReset = _holdEsp32InResetInStop;
  if(esp32HeldInReset)
  {
    espcp_hold_in_reset();
  }
#endif

  // Hand off the actual hardware transition (clock switch, alarm config,
  // the real stop-mode wfi/wfe, and switching back) to the idle task --
  // see pwrmgmt_idle_check_and_enter_lowpower(), called from up_idle().
  // This eliminates the up_idle()/pwrmgmt cross-task race documented in
  // Issue #436/#601: only the idle task ever executes wfi/wfe or touches
  // this hardware sequence, so there's nothing left to race against.
  _lowPwrRequestedWakeupPeriod = wakeupPeriod;
  _lowPwrRequestPending = true;

  // Wait for the idle task to finish the transition and post the result.
  nxsem_wait_uninterruptible(&_lowPwrRequestComplete);
  ret = _lowPwrRequestResult;

#if defined (CONFIG_MEADOW_ESPCP_MANAGER)
  // Release the ESP32 from reset. It reboots, and espcp_reset() marks the
  // reset as expected so it isn't reported as an ESP32 fault.
  if(esp32HeldInReset)
  {
    espcp_reset();
  }
#endif

  // Notify concerned modules that low-power mode has ended. If a module has
  // a problem restarting it will be returned as an error, which will be output
  // and things will continue.
  (void) pwrmgmt_notify_registered_modules(false);

  // Can now be reentered
  nxsem_post(&_lowPwrCtrlEntry);
  return ret;
}

#endif    // #if defined (CONFIG_MEADOW_PWR_MGMT_SUPPORT)
