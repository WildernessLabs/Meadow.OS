/****************************************************************************
 * F7MeadowOS/specialized/meadow_idle_monitor.c
 * 
 *   Copyright (C) 2023 Wilderness Labs. All rights reserved.
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

// This module measures how much of the time the MCU is idle, for a rough
// load figure (Meadow.Core's GetProcessorUtilization()).
//
// up_idle() in stm32_idle.c calls meadow_idle_mon_before_sleep() just before
// its wfi and meadow_idle_mon_after_sleep() when it wakes. The time between
// the two is added to an idle total. Time is read from the NuttX system
// tick: the tick count plus the SysTick counter's position within the
// current tick. SysTick keeps counting during wfi, since it is what wakes
// the MCU every tick.
//
// Once a second has passed, the idle total is turned into a percentage for
// that second and a new second starts. This is done by whichever runs first,
// the idle hook or meadow_idle_monitor_get_value(), so no timer is needed:
// even if the idle task never runs (100% load), the next get_value() still
// sees the second has ended and reports about 0% idle.
//
// Rough by design, and good enough for its purpose:
//  - The interrupt that wakes the MCU runs before the "after" reading, so
//    its run time is counted as idle.
//  - If that interrupt makes another task ready, the idle task is preempted
//    before the "after" reading. Each sleep is therefore capped at the end
//    of the tick it started in (a wfi never outlasts the next tick), so a
//    preempted sleep over-counts by at most the rest of one tick.
//  - A sleep that crosses the end of a second is counted in the second that
//    is ending.
//  - Stop mode (pwrmgmt_idle_check_and_enter_lowpower()) is not counted.
//
// This used to count idle entries and sample the count once a second with
// TIM6. TIM6 is now free, for use as the DAC trigger timer (issue #657).

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <arch/board/board.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>

#include "arm_internal.h"
#include "nvic.h"


#include "../hcom_nx/hcom_nx_common.h"
#include <meadow/hcom_misc_diag.h>
#include <meadow/hcom_shared_common.h>

#if MEADOW_INCLUDE_IDLE_MONITOR_TESTS_IN_BUILD > 0
// It would be nice if this thread would be replace with the HPWORK worker
// queue since it is only for testing.
#include <nuttx/kthread.h>
#endif

// Diagnostic only
// #define USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_apps_core_share.h>
#include <meadow/meadow_debug_helpers.h>

//================================================================
#if MEADOW_INCLUDE_IDLE_MONITOR_TESTS_IN_BUILD > 0
#define MEADOW_IDLE_MON_TEST_EXECUTE_TIMES (5)
#endif

/************************************************************************************
 * Private Function Prototypes
 ************************************************************************************/

#if MEADOW_INCLUDE_IDLE_MONITOR_TESTS_IN_BUILD > 0
static int meadow_idle_mon_create_test_thread(void);
static void *meadow_idle_mon_test_thread_proc(int argc, char *argv[]);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#if defined (CONFIG_ARCH_IDLE_CUSTOM)
// All times are in SysTick counts (HCLK/8 = 27 MHz on Meadow).
static uint64_t _sleepStart;        // Set by meadow_idle_mon_before_sleep()
static clock_t  _sleepStartTick;    // The system tick _sleepStart falls in
static uint64_t _idleCounts;        // Idle time so far in the current second
static clock_t  _secondStartTick;   // System tick at which the second started
static int      _idlePercent;       // Result for the last complete second
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if defined (CONFIG_ARCH_IDLE_CUSTOM)
//================================================================
// SysTick counts in one system tick
static uint32_t meadow_idle_mon_counts_per_tick(void)
{
  return (getreg32(NVIC_SYSTICK_RELOAD) & NVIC_SYSTICK_RELOAD_MASK) + 1;
}

//================================================================
// The current time in SysTick counts, and the system tick it falls in.
// SysTick counts down, so its position within the tick is reload - current.
// The tick count is read twice so a tick interrupt between the two reads is
// noticed and the read repeated.
static uint64_t meadow_idle_mon_now(uint32_t perTick, clock_t *tickOut)
{
  clock_t  tick;
  uint32_t current;

  do
  {
    tick    = clock_systime_ticks();
    current = getreg32(NVIC_SYSTICK_CURRENT);
  }
  while(tick != clock_systime_ticks());

  *tickOut = tick;
  return ((uint64_t)tick * perTick) + ((perTick - 1) - current);
}

//================================================================
// If at least a second has passed, turn the idle total into a percentage for
// that second and start a new second. Called with interrupts disabled.
static void meadow_idle_mon_end_second_if_due(clock_t nowTick)
{
  clock_t  elapsedTicks = nowTick - _secondStartTick;
  uint64_t elapsedCounts;

  if(elapsedTicks < TICK_PER_SEC)
  {
    return;
  }

  elapsedCounts = (uint64_t)elapsedTicks * meadow_idle_mon_counts_per_tick();
  if(_idleCounts >= elapsedCounts)
  {
    _idlePercent = 100;
  }
  else
  {
    _idlePercent = (int)((_idleCounts * 100) / elapsedCounts);
  }

  _idleCounts = 0;
  _secondStartTick = nowTick;
}
#endif    // #if defined (CONFIG_ARCH_IDLE_CUSTOM)

/****************************************************************************
 * Public Functions
 ****************************************************************************/

//================================================================
// Called during startup
int meadow_idle_monitor_setup(void)
{
  MEADOW_TRACE_INFORMATION("Entered meadow_idle_monitor_setup at startup\n");


#if defined (CONFIG_ARCH_IDLE_CUSTOM)
  irqstate_t flags = enter_critical_section();
  _sleepStart = 0;
  _sleepStartTick = 0;
  _idleCounts = 0;
  _secondStartTick = clock_systime_ticks();
  _idlePercent = 0;
  leave_critical_section(flags);
#endif

#if MEADOW_INCLUDE_IDLE_MONITOR_TESTS_IN_BUILD > 0
  int ret = meadow_idle_mon_create_test_thread();
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-meadow_idle_mon_create_test_thread failed, ret:%d, errno:%d\n",
          __FILE__, __LINE__, ret, errno);
    return ret;
  }
#endif

  return OK;
}

//================================================================
// Return the MCU idle percentage 0 - 100% for the last complete second
int meadow_idle_monitor_get_value(void)
{
  int idlePercent = 0;

#if defined (CONFIG_ARCH_IDLE_CUSTOM)
  irqstate_t flags = enter_critical_section();
  meadow_idle_mon_end_second_if_due(clock_systime_ticks());
  idlePercent = _idlePercent;
  leave_critical_section(flags);
#endif    // #if defined (CONFIG_ARCH_IDLE_CUSTOM)

  return idlePercent;
}

#if defined (CONFIG_ARCH_IDLE_CUSTOM)
//================================================================
// Called by up_idle() in stm32_idle.c just before its wfi. Only called by
// the idle thread.
void meadow_idle_mon_before_sleep(void)
{
  irqstate_t flags = enter_critical_section();
  _sleepStart = meadow_idle_mon_now(meadow_idle_mon_counts_per_tick(),
                                    &_sleepStartTick);
  leave_critical_section(flags);
}

//================================================================
// Called by up_idle() in stm32_idle.c when its wfi returns. Only called by
// the idle thread.
void meadow_idle_mon_after_sleep(void)
{
  irqstate_t flags = enter_critical_section();
  uint32_t perTick = meadow_idle_mon_counts_per_tick();
  clock_t  nowTick;
  uint64_t now = meadow_idle_mon_now(perTick, &nowTick);
  uint64_t tickEnd;

  // The interrupt that ends the wfi can make a higher priority task ready,
  // and the idle task is then preempted before it gets here, perhaps for a
  // long time. A wfi never lasts past the next tick interrupt, so only count
  // up to the end of the tick in which the sleep started. A preempted sleep
  // then counts at most the rest of that tick as idle.
  tickEnd = ((uint64_t)_sleepStartTick + 1) * perTick;
  if(now > tickEnd)
  {
    now = tickEnd;
  }

  // Guard against a reading that went backwards (a SysTick wrap seen before
  // its tick interrupt ran); just skip that sleep.
  if(now > _sleepStart)
  {
    _idleCounts += now - _sleepStart;
  }

  meadow_idle_mon_end_second_if_due(nowTick);
  leave_critical_section(flags);
}
#endif

// Test code follows
#if MEADOW_INCLUDE_IDLE_MONITOR_TESTS_IN_BUILD > 0
//================================================================
static int meadow_idle_mon_create_test_thread(void)
{
  int test_kthrd;

  // Create a thread to run the tests. Note: the priority is really high
  // because this is test code. and if priority is lower will not see the
  // output when doing things like downloading a file.
  test_kthrd = kthread_create("IdleMonTest",
                              248,      // Pri really high for testing
                              2048,     // Stack
                              (main_t) meadow_idle_mon_test_thread_proc,
                              (char *const *) NULL);
  if(test_kthrd <= 0)
  {
    syslog(LOG_ERR, "%s@%d-Creation of %s kthread FAILED\n",
                __FILE__, __LINE__, "IdleMonTest");
    return -ENOEXEC;
  }
  return OK;
}

//================================================================
// Test code for mcu idle measurement. It only reports via syslog the current
// idle percentage calculated.
static void *meadow_idle_mon_test_thread_proc(int argc, char *argv[])
{
#if HCOM_DIAG_OUTPUT_SYSLOG_PID_OF_NEW_THREADS > 0
  syslog(LOG_MDIAG, "New kthread [Thrd:%d][Task:%d] %s\n",
    gettid(), getpid(), "Idle monitor");
#endif

#if defined (CONFIG_ARCH_IDLE_CUSTOM)
  int idleValue;
  uint32_t fullTestLoopCount = 0;
  uint32_t wasteTickTime;
  uint32_t ticksToWaste;
  uint32_t loopCnt;
  uint32_t usecToSleep;

  while(fullTestLoopCount < MEADOW_IDLE_MON_TEST_EXECUTE_TIMES)
  {
    // This loop execute once for each cycle
    // Assumes 1000 ticks / second
    for(loopCnt = 0; loopCnt < 11; loopCnt++)
    {
      // How many ticks to waste? Total time is 1 second
      ticksToWaste = loopCnt * 100;   // 0 - 1000
      usecToSleep = (TICK_PER_SEC - ticksToWaste) * USEC_PER_MSEC;
      wasteTickTime = clock_systime_ticks() + ticksToWaste;

      // Simply waste time wasting loop
      // clock_systime_ticks(); Return ticks since start as clock_t (int64_t)
      while(clock_systime_ticks() < wasteTickTime);

      // Sleep the remaining time
      usleep(usecToSleep);

      // Now show the preceeding load values
      idleValue = meadow_idle_monitor_get_value();
      syslog(LOG_MTEST, "MCU Idle:%3d%%, MCU Load:%3d%% (busy %lu ms of 1000)\n",
        idleValue, 100 - idleValue, ticksToWaste);
    }
    fullTestLoopCount++;
  }

  syslog(LOG_MTEST, "Monitor Idle Test was executed %lu times\n",
    fullTestLoopCount);
#endif

  return NULL;
}
#endif
