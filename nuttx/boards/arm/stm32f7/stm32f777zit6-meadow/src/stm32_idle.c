/****************************************************************************
 * configs/stm32f777zit6-meadow/src/stm32_idle.c
 *
 *   Copyright (C) 2019 Geoff Norton <grompf@gmail.com>
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
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <debug.h>
#include <meadow/hcom_misc_diag.h>
#include <meadow/meadow_pwr_mgmt.h>

#if defined (CONFIG_MEADOW_PWR_MGMT_SUPPORT) && !defined (CONFIG_ARCH_IDLE_CUSTOM)
#error "CONFIG_MEADOW_PWR_MGMT_SUPPORT requires CONFIG_ARCH_IDLE_CUSTOM"
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_idle
 *
 * Description:
 *   up_idle() is the logic that will be executed when their is no other
 *   ready-to-run task.  This is processor idle time and will continue until
 *   some interrupt occurs to cause a context switch from the idle task.
 *
 *   Processing in this state may be processor-specific. e.g., this is where
 *   power management operations might be performed.
 *
 ****************************************************************************/

void up_idle(void)
{
#if defined(CONFIG_SUPPRESS_INTERRUPTS) || defined(CONFIG_SUPPRESS_TIMER_INTS)
  /* If the system is idle and there are no timer interrupts, then process
   * "fake" timer interrupts. Hopefully, something will wake up.
   */

  nxsched_process_timer();
  
#else   // #if defined(CONFIG_SUPPRESS_INTERRUPTS) || defined(CONFIG_SUPPRESS_TIMER_INTS)

  #if defined(CONFIG_ARCH_IDLE_CUSTOM)
    // Count the calls to up_idle
    meadow_idle_mon_entering_idle_mode();

    #if defined (CONFIG_MEADOW_PWR_MGMT_SUPPORT)
      // If a low-power stop-mode request is pending, perform the entire
      // hardware transition synchronously right here, on the idle task,
      // instead of racing a separate task's own hardware configuration
      // against this function's own wfi/wfe.
      //
      // This replaced a design (Issue #436) where a separate pwrmgmt task
      // cleared a shared flag to tell up_idle() not to sleep while it
      // configured stop mode elsewhere -- a check-then-act race, since
      // up_idle() could be preempted between reading the flag and
      // executing wfi/wfe. Two attempts to close that race by masking
      // interrupts around the check+wfi/wfe both caused a full boot
      // failure on real hardware, for reasons tied to how this specific
      // silicon's wfi interacts with BASEPRI-based masking (see Issue
      // #601 for the full investigation). Moving the whole transition
      // (including the real stop-mode wfi/wfe, inside
      // pwrmgmt_enter_stop_mode()) onto this same task removes the race
      // entirely: only the idle task ever executes wfi/wfe now, so
      // ordinary RTOS preemption of this call is safe by construction.
      if(pwrmgmt_idle_check_and_enter_lowpower())
      {
        // The transition above already performed the real stop-mode
        // wfi/wfe as part of its own sequence; don't also do the plain
        // one below on this iteration.
        return;
      }

      #ifdef CONFIG_PM_WFE
        asm volatile ("wfe");
      #else
        asm volatile ("wfi");
      #endif
    #else
      // Only CONFIG_ARCH_IDLE_CUSTOM defined
      #ifdef CONFIG_PM_WFE
        asm volatile ("wfe");
      #else
        asm volatile ("wfi");
      #endif
    #endif

  #endif  //  #if defined(CONFIG_ARCH_IDLE_CUSTOM)
// Without CONFIG_ARCH_IDLE_CUSTOM do nothing

#endif   // #if defined(CONFIG_SUPPRESS_INTERRUPTS) || defined(CONFIG_SUPPRESS_TIMER_INTS)
}
