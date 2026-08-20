/****************************************************************************
 * Meadow.OS/kerneltests/meadow_interrupt_tests.c
 * 
 *   Copyright (C) 2024-2026 Wilderness Labs. All rights reserved.
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_nx/hcom_nx_common.h"
#include <meadow/hcom_shared_common.h>

// To enable '-u=mint'
// Only build if configured
#if defined(CONFIG_MEADOW_INTERRUPT_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
#pragma message "(--) meadow_interrupt_tests.c"

#include "stm32_gpio.h"             // stm32_configgpio
#include "stm32_exti.h"             // STM32_EXTI_PR
#include "arm_internal.h"           // putreg32
#include "nvic.h"                   // NVIC access
#include <nuttx/clock.h>
#include <nuttx/mqueue.h>          // file_mq_* (kernel-side queue API)
#include <nuttx/kthread.h>
#include "meadow/meadow-upd.h"      // mint_config_interrupt(cfg);
#include <meadow/meadow_interrupt.h>
#include "pwrmgmt/pwrmgmt_local.h"
#include <meadow/meadow_syscall_support.h>

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/

// D05 (PB4) for Version F7FeatherV2 or CCM V1
// For Testing wanted a pin that was Px0-4 to more easily figure out interrupts
// and because these are a high priority interrupts.
#define MINT_TST_GPIO_PIN_V2_D05 (GPIO_INPUT | GPIO_PULLDOWN | GPIO_PORTB | GPIO_PIN4)
#define MINT_TST_GPIO_PIN_V2_D14 (GPIO_OUTPUT | GPIO_PUSHPULL | GPIO_SPEED_100MHz | GPIO_PORTB | GPIO_PIN12)

/************************************************************************************
 * Private Data
 ************************************************************************************/
// Guards against creating more than one mq-monitor thread. There's currently
// no way to signal/kill an already-running one, so a second request while
// one is active is simply refused rather than spawning a duplicate.
static bool _mqMonitorThreadCreated = false;

/************************************************************************************
 * Public Data
 ************************************************************************************/

/************************************************************************************
 * Private Function Prototypes
 ************************************************************************************/
static void meadow_interrupt_test_test_mem_leak_fix_free_1(void);
static void meadow_interrupt_test_config_d05_add_debounce(uint32_t duration);
static void meadow_interrupt_test_test_mem_leak_fix_alloc_5(void);
static void meadow_interrupt_test_test_mem_leak_fix_free_5(void);
static void meadow_interrupt_test_initialize_interrupt_for_wakeup(void);
static void meadow_interrupt_test_initialize_wakeup_and_sleep(void);
static void *meadow_interrupt_test_monitor_mint_mq(int argc, char *argv[]);

/************************************************************************************
 * Public Functions
 ************************************************************************************/
// set developer -p 16 comes here
void meadow_kt_meadow_interrupt_tests(uint32_t userData)
{
  static bool firstTime = true;

  syslog(LOG_MTEST, "Meadow interrupt tests received 'set developer -p 16 -v %lu'\n",
    userData);

  switch(userData)
  {
    // Configure using D05 as input to generate and send mq messages.
    // Also used for testing memory leak, in conjunction with #20.
    // No glitch or debounce timeout for 1 but 2 - 6 will configure debounce
    // values.
    case 1:
      meadow_interrupt_test_config_d05_add_debounce(0);
      break;

      // The follow set (2 - 6) configure D05 as interrupt input with a
      // debounce timeout.
    case 2:
      // Debounce duration of 0.1 millisecond
      meadow_interrupt_test_config_d05_add_debounce(1);
      break;

    case 3:
      // Debounce duration of 1 millisecond
      meadow_interrupt_test_config_d05_add_debounce(10);
      break;

    case 4:
      // Debounce duration of 10 millisecond
      meadow_interrupt_test_config_d05_add_debounce(100);
      break;

    case 5:
      // Debounce duration of 100 millisecond
      meadow_interrupt_test_config_d05_add_debounce(1000);
      break;

    case 6:
      // Debounce duration of 1 second
      meadow_interrupt_test_config_d05_add_debounce(10000);
      break;

    // After #1 - 6 has configured a GPIO, this test can be used to the display
    // interrupt information via syslog. It opens the message queue that is
    // normally read by Meadow.Core and displays the data as it is generated.
    // It does this by creating a thread so this call returns and other tests
    // can be run. Toggling D05 will generate a messages at each rising edge.
    // There's currently no way to stop the thread short of resetting
    // Meadow. If a second #10 is received while a thread is running, an error
    // message is displayed and nothing is changed.
    case 10:
    {
      if(_mqMonitorThreadCreated)
      {
        syslog(LOG_MTEST, "Mint test - mq monitor thread is already running\n");
        break;
      }

      int mqMonitorPid = kthread_create("MintMqMonitor", 200, 2048,
        (main_t) meadow_interrupt_test_monitor_mint_mq, (char * const *)NULL);
      if(mqMonitorPid < 0)
      {
        syslog(LOG_MTEST, "Mint test - failed to create mq monitor thread, ret:%d\n",
          mqMonitorPid);
        break;
      }

      _mqMonitorThreadCreated = true;
      syslog(LOG_MTEST, "Mint test - mq monitor thread started, pid:%d\n", mqMonitorPid);
      break;
    }

    // Frees interrupt configuration done in #1 to verify
    // memory leak with a single GPIO.
    case 20:
      meadow_interrupt_test_test_mem_leak_fix_free_1();
      break;
    
    // Test low-power sleeping for 10 seconds as verification before doing
    // the next test. No other test code is needed
    case 30:
      stm32_gpiowrite(MINT_TST_GPIO_PIN_V2_D14, true);
      pwrmgmt_enter_stm32f7_stop_mode(10);    // Sleep for 10 seconds
      stm32_gpiowrite(MINT_TST_GPIO_PIN_V2_D14, false);
      break;

    // Initialize wakeup pin then sleep
    case 31:
      meadow_interrupt_test_initialize_wakeup_and_sleep();
      break;

    // These next 2 are also designed to test a resolved memory leak problem
    // Allocate memory for 5 interrupt GPIOs
    case 32:
      meadow_interrupt_test_test_mem_leak_fix_alloc_5();
      break;

    // Free allocated memory for 5 configureations 
    case 33:
      meadow_interrupt_test_test_mem_leak_fix_free_5();
      break;

    // Do initialization for wakeup
    case 34:
      if(firstTime)
      {
        firstTime = false;
        meadow_interrupt_test_initialize_interrupt_for_wakeup();
      }
      else
      {
        syslog(LOG_MTEST, "Only first time\n");
      }
      break;

    default:
      syslog(LOG_MTEST, "Undefined test for meadow_kt_meadow_interrupt_tests, userData:%lu\n",
        userData);
      break;
  }
}
 
/************************************************************************************
 * Private Functions
 ************************************************************************************/
// Test 1 - 6 - Configure D05 and mint interrupt configuration.
static void meadow_interrupt_test_config_d05_add_debounce(uint32_t duration)
{
  int ret;

  syslog(LOG_MDIAG, "MQ message size is:%d\n", MEADOW_INTERRUPT_MQ_MSG_SIZE);
  syslog(LOG_MDIAG, "syslog outputs 'MQ overflow', if no MQ reader.\n");

  struct mint_gpio_int_config* cfg = malloc(sizeof(struct mint_gpio_int_config));
  if(cfg == NULL)
  {
    syslog(LOG_MTEST, "%s@%d-Error:malloc returned NULL\n",
      __FILE__, __LINE__);
    return;
  }

  // D05 - PB4 Input for GPIO pin
  stm32_configgpio(MINT_TST_GPIO_PIN_V2_D05);
  
  // We need some GPIO to initialize and free
  // Populate config structure for GPIO PB4 (D05 in FeatherV2).
  // This is what Meadow.Core will do when it's been enhanced to support this
  // feature.
  // Note: cfg is not the Nuttx cfgset. The Nuttx cfgset is built by the call
  // to mint_config_interrupt().
  cfg->port = 1;              // port B (D05 in FeatherV2)
  cfg->pin = 4;               // pin 4  (D05 in FeatherV2)
  cfg->configType = gpio_intrpt_cfg_type_add;   // Add
  cfg->risingEdge = 1;
  cfg->fallingEdge = 0;
  cfg->resistorMode = 2;      // 2 = pull down
  cfg->debounceDuration = duration;  // Must be 0 for lp wakeup
  cfg->glitchDuration = 0;    // Must be 0 for lp wakeup

  // Call public configuration function used by managed code
  ret = mint_config_interrupt(cfg);
  if(ret < 0)
  {
    syslog(LOG_MTEST, "Error:mint_config_interrupt returned ret:%d\n",
      ret);
  }
  free (cfg);
}

// ============================================================================
// Test 10 code executed by kthread
// This test reads the message queue into which interrupt's are placed.
// This simulates what Meadow.Core does.
// It displays the data of all interrupt messages queued.
//
// Execute 'developer -p 16 -v 1 first to configure D05 as DI.
// Execute 'developer -p 16 -v 10 to start this kthread, reading the mq
// for syslog display. Toggling D05 will generate a message at each
// rising edge. There's currently no way to stop the thread short of
// resetting Meadow.
static void *meadow_interrupt_test_monitor_mint_mq(int argc, char *argv[])
{
  int ret;
  // Held as a struct file and driven through file_mq_*() rather than as an
  // mqd_t through mq_*(): this is kernel code, and the file_mq_*() calls
  // return a negated errno instead of writing errno in the current task's
  // TLS. See the comment on _mint_mq in meadow_os/meadow_interrupt.c.
  struct file mint_test_mq;
  mint_send_int_core_t mint_recvd_msg;
  ssize_t nbytes;

  // Open existing mq
  ret = file_mq_open(&mint_test_mq, MINT_MSG_QUEUE_NAME, O_RDONLY);
  if (ret < 0)
  {
    syslog(LOG_ERR, "Error:Mint test open mq, ret:%d\n", ret);
    usleep(20 * 1000);
    _mqMonitorThreadCreated = false;
    return NULL;
  }

  // Read and display all interrupts, until we receive a command to stop
  while(true)
  {
    // syslog(LOG_MTEST, "----> %s@%d-Calling file_mq_receive, waiting\n",
    //   __FILE__, __LINE__);
    // usleep(20 * 1000);

    nbytes = file_mq_receive(&mint_test_mq, (char *)&mint_recvd_msg,
      MEADOW_INTERRUPT_MQ_MSG_SIZE, NULL);
    if(nbytes < 0)
    {
      // Signal. file_mq_receive() reports this in its return value, not in
      // errno, so this tests nbytes - the mq_receive() form of this loop
      // tested errno, which is not set by the file_mq_*() calls at all.
      if(nbytes == -EINTR)
      {
        continue;
      }

      syslog(LOG_ERR, "---->%s@%d-Mint test - Error:file_mq_receive, ret:%d\n",
        __FILE__, __LINE__, nbytes);
        usleep(20 * 1000);
      break;
    }

    if(nbytes == 0)
    {
      syslog(LOG_ERR, "Mint test - read mq, 0-byte message?\n");
      usleep(20 * 1000);
      break;      // No message
    }

    // Display the mq's data.
    if(nbytes == MEADOW_INTERRUPT_MQ_MSG_SIZE)
    {
      // This define is in meadow_interrupt.h
#if (MEADOW_INTERRUPT_INCLUDE_TIME_STAMP > 0)
      // The time sent is currenly a count of ticks since Nuttx was last
      // started.
  #if(1)    // Simple just timestamp
        syslog(LOG_MTEST, "Mint test - Received, PinId:0x%02x, State:0x%02x, Ticks:%lld\n",
          mint_recvd_msg.gpioPinId,
          mint_recvd_msg.gpioState,
          mint_recvd_msg.mintSysTicks);
  #endif
  
  #if(0) // Convert Nuttx ticks to timespec and Nuttx up time
        struct timespec mintTimeSpec;
        clock_ticks2time(&mintTimeSpec, mint_recvd_msg.mintSysTicks);

        syslog(LOG_MTEST, "Mint test - Received, PinId:0x%02x, State:0x%02x, Ticks:%lld (sec:%lld, nsec:%ld)\n",
          mint_recvd_msg.gpioPinId,
          mint_recvd_msg.gpioState,
          mint_recvd_msg.mintSysTicks,
          mintTimeSpec.tv_sec,
          mintTimeSpec.tv_nsec);
  #endif

  #if(0)    // Full execution time calculation
        // Convert Nuttx tick count to wall time.
        struct tm tm_info;
        char timeBuffer[64];
        struct timespec currentWall, delta, interruptWallTime;

        clock_gettime(CLOCK_REALTIME, &currentWall);
        clock_t elapsedTicks = clock_systime_ticks() - mint_recvd_msg.mintSysTicks;
        clock_ticks2time(&delta, elapsedTicks);
        clock_timespec_subtract(&currentWall, &delta, &interruptWallTime);  // currentWall - delta
        gmtime_r(&interruptWallTime.tv_sec, &tm_info);
        strftime(timeBuffer, sizeof(timeBuffer), "UTC-%Y-%m-%d %H:%M:%S", &tm_info);
        syslog(LOG_MTEST, "Mint test - PinId:0x%02x, State:0x%02x, Ticks:%lld (%s.%ld)\n",
          mint_recvd_msg.gpioPinId, mint_recvd_msg.gpioState,
          mint_recvd_msg.mintSysTicks,
          timeBuffer, interruptWallTime.tv_nsec);
  #endif

#else
      syslog(LOG_MTEST, "Mint test - Received, PinId:0x%02x, State:0x%02x\n",
        mint_recvd_msg.gpioPinId,
        mint_recvd_msg.gpioState);
#endif    
    }
    else
    {
      syslog(LOG_MTEST, "Mint test - unexpected length message received, size:%d\n", nbytes);
    }
  }   // while true

  // Close mq
  ret = file_mq_close(&mint_test_mq);
  if (ret < 0)
  {
    syslog(LOG_MTEST, "Mint test read mq, file_mq_close error, ret:%d\n", ret);
  }

  _mqMonitorThreadCreated = false;
  return NULL;
}

// ============================================================================
// Test 20 Remove D05 from interrupt configuration
static void meadow_interrupt_test_test_mem_leak_fix_free_1(void)
{
  // Configure a GPIO
  int ret;
  struct mint_gpio_int_config* cfg = malloc(sizeof(struct mint_gpio_int_config));
  if(cfg == NULL)
  {
    syslog(LOG_MTEST, "%s@%d-Error:malloc returned NULL\n",
      __FILE__, __LINE__);
    return;
  }
  syslog(LOG_MTEST, "TEST-Freeing memory - for D05\n");
  usleep(29 * 1000);

  // PB4
  cfg->port = 1;              // port B (D05 in FeatherV2)
  cfg->pin = 4;               // pin 4  (D05 in FeatherV2)
  cfg->configType = gpio_intrpt_cfg_type_remove;  // Delete
  cfg->risingEdge = 1;
  cfg->fallingEdge = 0;
  cfg->resistorMode = 2;      // 2 = pull down
  cfg->debounceDuration = 5;  // Must be 0 for lp wakeup
  cfg->glitchDuration = 0;    // Must be 0 for lp wakeup

  // Call public configuration function to delete interrupt GPIO
  ret = mint_config_interrupt(cfg);
  if(ret < 0)
  {
    syslog(LOG_MTEST, "Error while removing GPIO:mint_config_interrupt() ret:%d\n",
      ret);
  }

  free (cfg);
}

// ============================================================================
// Test 31
// Should this be in power_management_tests instead of here?
// This test is used to determine if an interrupt can wakeup the F7 from a
// low-power mode. It simulates being configured via Meadow.Core.
static void meadow_interrupt_test_initialize_wakeup_and_sleep(void)
{
  int ret;

  struct mint_gpio_int_config* cfg = malloc(sizeof(struct mint_gpio_int_config));
  if(cfg == NULL)
  {
    syslog(LOG_MTEST, "%s@%d-Error:malloc returned NULL\n",
      __FILE__, __LINE__);
    return;
  }

  // Only used by this module
  stm32_configgpio(MINT_TST_GPIO_PIN_V2_D14);
  stm32_gpiowrite(MINT_TST_GPIO_PIN_V2_D14, true); // HIgh while sleeping
  
  // D05 - PB4 Input for GPIO wakeup pin
  stm32_configgpio(MINT_TST_GPIO_PIN_V2_D05);
  
  // Populate config structure for GPIO wakeup of PB4 (D05 in FeatherV2).
  // This is what Meadow.Core will do when it's been enhanced to support this
  // feature.
  // Note: cfg is not the Nuttx cfgset. The Nuttx cfgset is built by the call
  // to mint_config_interrupt().
  cfg->port = 1;              // port B (D05 in FeatherV2)
  cfg->pin = 4;               // pin 4  (D05 in FeatherV2)
  cfg->configType = gpio_intrpt_cfg_type_wakeup;
  cfg->risingEdge = 1;
  cfg->fallingEdge = 0;
  cfg->resistorMode = 2;      // 2 = pull down
  cfg->debounceDuration = 0;  // Must be 0 for lp wakeup
  cfg->glitchDuration = 0;    // Must be 0 for lp wakeup

  // Configure interrupt pin via public function used by managed code
  ret = mint_config_interrupt(cfg);
  if(ret < 0)
  {
    syslog(LOG_MTEST, "Error:mint_config_interrupt returned ret:%d\n",
      ret);
  }
  free (cfg);

  syslog(LOG_MTEST, "%s@%d - Going into Low-power sleep for 30 seconds unless interrupted.\n",
    __FILE__, __LINE__);
  // Need a bit of time to insure message is received before low-power mode
  usleep(50 * 1000);

  stm32_gpiowrite(MINT_TST_GPIO_PIN_V2_D14, true);  // Time sleep

  // Put Meadow to sleep for either time or till interrupt
  ret = pwrmgmt_enter_stm32f7_stop_mode(30);
  if(ret < 0)
  {
    syslog(LOG_MTEST, "Error:pwrmgmt_enter_stm32f7_stop_mode returned ret:%d\n",
      ret);
  }

  stm32_gpiowrite(MINT_TST_GPIO_PIN_V2_D14, false);  // Time sleep

  // Verify wakeup reason
  int wakeReason = pwrmgmt_most_recent_wakeup_reason();
  syslog(LOG_MTEST, "%s@%d - Low-power sleep ended, reason:%d\n",
    __FILE__, __LINE__, wakeReason);
  usleep(20 * 1000);
}

// ============================================================================
// Test 32
// These tests exercise the fix for the memory leak in meadow_interrupt.c
// Meadow_Issue 346
static void meadow_interrupt_test_test_mem_leak_fix_alloc_5(void)
{
  // Setup a GPIO
  int ret;
  struct mint_gpio_int_config* cfg = malloc(sizeof(struct mint_gpio_int_config));
  if(cfg == NULL)
  {
    syslog(LOG_MTEST, "%s@%d-Error:malloc returned NULL\n", __FILE__, __LINE__);
    return;
  }
  syslog(LOG_MTEST, "TEST-Configuring 5 points\n"); usleep(29 * 1000);

  // We need some GPIOs to initialize and free
  // Populate config structure for GPIO wakeup of PB4 (D05 in FeatherV2).
  // This is what Meadow.Core will do when it's been enhanced to support this
  // feature.
  // Note: cfg is not the Nuttx cfgset. The Nuttx cfgset is built by the call
  // to mint_config_interrupt().
  // PB7 - PB11
  for(int i = 7; i <= 11; i++)
  {
    uint8_t pinDesignation = 0x10 | i;
    syslog(LOG_MTEST, "TEST-Config point:0x%02x\n", pinDesignation);
    usleep(29 * 1000);

    cfg->port = 1;              // port B
    cfg->pin = i;               // pin 7-11
    cfg->configType = gpio_intrpt_cfg_type_add;   // New
    cfg->risingEdge = 1;
    cfg->fallingEdge = 0;
    cfg->resistorMode = 2;      // 2 = pull down
    cfg->debounceDuration = 5;  // Must be 0 for lp wakeup
    cfg->glitchDuration = 0;    // Must be 0 for lp wakeup

    // Call public configuration function used by managed code
    ret = mint_config_interrupt(cfg);
    if(ret < 0)
    {
      syslog(LOG_MTEST, "Error:mint_config_interrupt returned ret:%d\n",
        ret);
    }

  }
  free (cfg);
}

// ============================================================================
// Test 33
// Delete one GPIO
static void meadow_interrupt_test_test_mem_leak_fix_free_5(void)
{
  // Setup a GPIO
  int ret;
  struct mint_gpio_int_config* cfg = malloc(sizeof(struct mint_gpio_int_config));
  if(cfg == NULL)
  {
    syslog(LOG_MTEST, "%s@%d-Error:malloc returned NULL\n",
      __FILE__, __LINE__);
    return;
  }
  syslog(LOG_MTEST, "TEST-Freeing 5 points\n");
  usleep(29 * 1000);

  // PB7 - PB11 but remove in reverse order
  for(int i = 11; i >= 7; i--)
  {
    uint8_t pinDesignation = 0x10 | i;
    syslog(LOG_MTEST, "TEST-Free point:0x%02x\n", pinDesignation);
    usleep(29 * 1000);
    
    cfg->port = 1;              // port B
    cfg->pin = i;               // pin 7-11
    cfg->configType = gpio_intrpt_cfg_type_remove;   // Delete
    cfg->risingEdge = 1;
    cfg->fallingEdge = 0;
    cfg->resistorMode = 2;      // 2 = pull down
    cfg->debounceDuration = 5;  // Must be 0 for lp wakeup
    cfg->glitchDuration = 0;    // Must be 0 for lp wakeup

    // Call public configuration function used by managed code
    ret = mint_config_interrupt(cfg);
    if(ret < 0)
    {
      syslog(LOG_MTEST, "Error:mint_config_interrupt returned ret:%d\n",
        ret);
    }
  }

  syslog(LOG_MTEST, "EXITING meadow_interrupt_test_test_mem_leak_fix_free_5\n");
  free (cfg);
}

// ============================================================================
// Test 34
// This test is used to verify that an interrupt can wakeup the F7 from a
// low-power mode. It simulates being configured via Meadow.Core.
// It only configures the interrupt part. The input needs to be configured
// elsewhere before making this call
static void meadow_interrupt_test_initialize_interrupt_for_wakeup(void)
{
  // Setup a GPIO to generate an interrupt to wakeup
  int ret;
  struct mint_gpio_int_config* cfg = malloc(sizeof(struct mint_gpio_int_config));
  if(cfg == NULL)
  {
    syslog(LOG_MTEST, "%s@%d-Error:malloc returned NULL\n",
      __FILE__, __LINE__);
    return;
  }

  stm32_configgpio(MINT_TST_GPIO_PIN_V2_D14); // On while sleeping
  stm32_gpiowrite(MINT_TST_GPIO_PIN_V2_D14, false);

  // Populate config structure for GPIO wakeup of PB4 (D05 in FeatherV2).
  // This is what Meadow.Core will do when it's been enhanced to support this
  // feature.
  // Note: cfg is not the Nuttx cfgset. The Nuttx cfgset is built by the call
  // to mint_config_interrupt().
  cfg->port = 1;              // port B (D05 in FeatherV2)
  cfg->pin = 4;               // pin 4  (D05 in FeatherV2)
  cfg->configType = gpio_intrpt_cfg_type_wakeup;
  cfg->risingEdge = 1;
  cfg->fallingEdge = 0;
  cfg->resistorMode = 2;      // 2 = pull down
  cfg->debounceDuration = 0;  // Must be 0 for lp wakeup
  cfg->glitchDuration = 0;    // Must be 0 for lp wakeup

  // Call public configuration function used by managed code
  ret = mint_config_interrupt(cfg);
  if(ret < 0)
  {
    syslog(LOG_MTEST, "Error:mint_config_interrupt returned ret:%d\n",
      ret);
  }
  free (cfg);
}
#endif  // #if defined(CONFIG_MEADOW_INTERRUPT_TESTS)
