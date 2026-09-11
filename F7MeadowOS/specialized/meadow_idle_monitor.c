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

// The module uses the idle information from stm32_idle.c to
// determine the current MPU idle percentage.

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <arch/board/board.h>
#include <nuttx/timers/timer.h>
#include "stm32_tim.h"
#include "arm_internal.h"

#include "../hcom_nx/hcom_nx_common.h"
#include <meadow/hcom_misc_diag.h>
#include <meadow/hcom_shared_common.h>
#include "hardware/stm32f76xx77xx_rcc.h"

#if MEADOW_INCLUDE_IDLE_MONITOR_TESTS_IN_BUILD > 0
// It would be nice if this thread would be replace with the HPWORK worker
// queue since it is only for testing.
#include <nuttx/kthread.h>
#endif

// Diagnostic only
// #define USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_apps_core_share.h>
#include <meadow/meadow_debug_helpers.h>

// #pragma GCC optimize("O0")    /* Prevent optimization from removing test code */

//================================================================
// Timer 6 is one of the 2 basic timers. It has no connected GPIO I/O
// Both of the following #defines must match
#define MEADOW_IDLE_MON_USE_TIMER_NUMBER (6)
#define MEADOW_IDLE_MON_TIMER_BASE (STM32_TIM6_BASE)

// 65536 Will overflow 1 time each second
#define MEADOW_IDLE_MON_TARGET_FREQUENCY (65536)

// Since Meadow has a systick every millisecond, we'll need a value that
// is a bit over the systick rate. This value was tuned since 1000
// resulted in less that a 0-100 result. Being called 1000 times is related to
// this configuration entry (CONFIG_USEC_PER_TICK=1000)
// This is a reasonable maximum for the no load idle count.
#define MEADOW_IDLE_MON_MAX_100_PER_CENT_COUNT (1060)

#if MEADOW_INCLUDE_IDLE_MONITOR_TESTS_IN_BUILD > 0
#define MEADOW_IDLE_MON_TEST_EXECUTE_TIMES (5)
#endif

// For diagnostics
// #define DEBUG_PIN_V2_D14  (0x00040c1c)

/************************************************************************************
 * Private Function Prototypes
 ************************************************************************************/

static int meadow_idle_mon_timer_init(uint32_t timerBase);
static int meadow_idle_mon_isr(int irq, void *context, void *arg);

#if MEADOW_INCLUDE_IDLE_MONITOR_TESTS_IN_BUILD > 0
static int meadow_idle_mon_create_test_thread(void);
static void *meadow_idle_mon_test_thread_proc(int argc, char *argv[]);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#if defined (CONFIG_ARCH_IDLE_CUSTOM)
static volatile uint32_t _idleLoopCounter;
static volatile uint32_t _idleOneSecSnapShot;
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/
// This ISR designed to be called once/second via a timer interrupt.
static int meadow_idle_mon_isr(int irq, void *context, void *arg)
{
  // Check the timer's Status Register
  uint16_t timStatusReg = getreg16(MEADOW_IDLE_MON_TIMER_BASE + STM32_BTIM_SR_OFFSET);

  if(timStatusReg & BTIM_SR_UIF)
  {
    timStatusReg &= ~BTIM_SR_UIF;

#if defined (CONFIG_ARCH_IDLE_CUSTOM)
    // Once per second, capture the idle count.
    // The higher the count, the more often the OS dropped into idle mode.
    // This is an indirect indication of how busy the system is. When really
    // busy, the idle thread might not run for for several seconds.
    if(_idleLoopCounter < MEADOW_IDLE_MON_MAX_100_PER_CENT_COUNT)
    {
      // Save count for user
      _idleOneSecSnapShot = _idleLoopCounter;
    }
    else
    {
      // Save count for user
      _idleOneSecSnapShot = MEADOW_IDLE_MON_MAX_100_PER_CENT_COUNT;
    }

    // Reset counter
    _idleLoopCounter = 0;

#endif

    putreg16(timStatusReg, MEADOW_IDLE_MON_TIMER_BASE + STM32_BTIM_SR_OFFSET);
  }

  return OK;
}

//================================================================
// Called during startup
int meadow_idle_monitor_setup(void)
{
  int ret;

  MEADOW_TRACE_INFORMATION("Entered meadow_idle_monitor_setup at startup\n");

#if defined (CONFIG_ARCH_IDLE_CUSTOM)
  _idleLoopCounter = 0;
  _idleOneSecSnapShot = 0;
#endif

  // Initialized the timer itself
  ret = meadow_idle_mon_timer_init(MEADOW_IDLE_MON_TIMER_BASE);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Idle monitor init failed:%d\n", __FILE__, __LINE__, ret);
    return ret;
  }

  return OK;
}

//================================================================
// This function uses Timer 6 to create a periodic timer that will call
// the above ISR once / second. It assumes timer #6.
static int meadow_idle_mon_timer_init(uint32_t timerBase)
{
  int ret;

#if defined (CONFIG_ARCH_IDLE_CUSTOM)
  _idleLoopCounter = 0;
#endif

  // Setup the clock enable for timer 6
  modifyreg32(STM32_RCC_APB1ENR, 0, RCC_APB1ENR_TIM6EN);

  // Must be between 0 and 0xffff. Set the prescaler value of 0 to allow
  // highest speed. A prescaler value of 1 will divide the clock by 2.
  // Find proper pre-scaler value so all timers run at the same speed
  uint16_t prescaler = (STM32_APB1_TIM6_CLKIN/MEADOW_IDLE_MON_TARGET_FREQUENCY) - 1;
  putreg16(prescaler, timerBase + STM32_BTIM_PSC_OFFSET);

  // The value put into the ARR is maximum
  uint32_t maxARRValue = 0xffff;
  putreg32(maxARRValue, timerBase + STM32_BTIM_ARR_OFFSET);

  uint16_t regval = getreg16(timerBase + STM32_BTIM_CR1_OFFSET);
  regval |= BTIM_CR1_ARPE;    // Auto Reload Pre-Load enable bit
  putreg16(regval, timerBase + STM32_BTIM_CR1_OFFSET);

  // Timer 6 only supports UIE interrupt
  putreg16(BTIM_DIER_UIE, timerBase + STM32_BTIM_DIER_OFFSET);

  // The interupts are handled this ISR
  ret = irq_attach(STM32_IRQ_TIM6, meadow_idle_mon_isr, NULL);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-irq_attach failed, ret:%d, errno:%d\n",
          __FILE__, __LINE__, ret, errno);
    return ret;
  }

  up_enable_irq(STM32_IRQ_TIM6);

  // Enable the timer
  uint16_t cr1Val = getreg16(timerBase + STM32_BTIM_CR1_OFFSET);
  cr1Val |= BTIM_CR1_CEN;

  uint16_t egrVal = getreg16(timerBase + STM32_BTIM_EGR_OFFSET);
  egrVal |= BTIM_EGR_UG;

  putreg16(egrVal, timerBase + STM32_BTIM_EGR_OFFSET);
  putreg16(cr1Val, timerBase + STM32_BTIM_CR1_OFFSET);

#if MEADOW_INCLUDE_IDLE_MONITOR_TESTS_IN_BUILD > 0
  ret = meadow_idle_mon_create_test_thread();
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-meadow_idle_mon_create_test_thread failed, ret:%d, errno:%d\n",
          __FILE__, __LINE__, ret, errno);
    return ret;
  }
#endif
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// Return the current MCU idle percentage 0 - 100%
int meadow_idle_monitor_get_value(void)
{
  static int IdlePercent = 0;

#if defined (CONFIG_ARCH_IDLE_CUSTOM)
  irqstate_t flags = enter_critical_section();
  // Calculate the percentage
  IdlePercent =
      (_idleOneSecSnapShot * 100) / MEADOW_IDLE_MON_MAX_100_PER_CENT_COUNT;
  leave_critical_section(flags);
#endif    // #if defined (CONFIG_ARCH_IDLE_CUSTOM)

  return IdlePercent;
}

//================================================================
// Called by idle thread in stm32_idle. This happens whenever the STM32 is
// entering idle mode. This function is only called by the idle thread.
#if defined (CONFIG_ARCH_IDLE_CUSTOM)
void meadow_idle_mon_entering_idle_mode(void)
{
  irqstate_t flags = enter_critical_section();
  _idleLoopCounter++;
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
      syslog(LOG_MTEST, "MCU Idle:%3d%%, MCU Load:%3d%% (Count:%4lu)\n",
        idleValue, 100 - idleValue, _idleOneSecSnapShot);
    }
    fullTestLoopCount++;
  }

  syslog(LOG_MTEST, "Monitor Idle Test was executed %lu times\n",
    fullTestLoopCount);
#endif

  return NULL;
}
#endif
