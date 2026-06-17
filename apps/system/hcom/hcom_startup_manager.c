/****************************************************************************
 * \apps\examples\hcom\hcom_startup_manager.c
 * 
 *   Copyright (C) 2019 - 2020 Wilderness Labs. All rights reserved.
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

// This module is where everything starts. The thread created by nuttx for
// running apps enters here and hcom begins.

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "hcom_common.h"
#include <meadow/hcom_shared_common.h>

#if defined (CONFIG_MEADOW_CLOUD)
#include "ota/hcom_ota.h"
#endif
#include "cell/hcom_pppd.h"
#include "misc/espcp_utils.h"

#if defined (CONFIG_HCOM_ESP32_COMMS)
#include "esp32/hcom_esp32_comms.h"
#include "esp32/hcom_esp32_network_monitor.h"
#endif

#include <meadow/meadow_os.h>

// #pragma GCC optimize("O0")    // Prevent code optimization

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

extern int hcom_main (int argc, char* argv[]);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static char *thisFile = __FILE__;
static int _semaphoreRet;
static sem_t _startupWaitSem;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

//===========================================================================
// Wait for the thread holding the semaphore to release it
static int hcom_startup_mgr_takesem(sem_t *semaphore)
{
  int ret;
  _semaphoreRet = OK;
  
  do
  {
    ret = sem_wait(semaphore);    // Take the semaphore (perhaps waiting)
  }
  while (ret == -EINTR);
  
  return _semaphoreRet;
}

//===========================================================================
// Called by setup code in different modules to release this startup thread
// to continue the setup
void hcom_startup_mgr_release_sem()
{
  hcom_startup_mgr_release_sem_err(OK);
}

//---------------------------------------------------------------------------
// Called to report an error
void hcom_startup_mgr_release_sem_err(int semaphoreRet)
{
  _semaphoreRet = semaphoreRet;

  sem_post(&_startupWaitSem);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// This is the hcom tasks main pthread, created by Nuttx when it has finished
// initializing and starting the OS.
// This pthread will do all the following initialization of hcom, then becomes
// the thread that processes CLI messages. Any pthread created by this pthread
// or a child one of those pthreads, will all share the same group of file
// descriptors, streams, etc. In Nuttx this grouping of pthread are called a
// "task group."
// For more information look at the following links.
// https://cwiki.apache.org/confluence/display/NUTTX/Tasks+vs.+Threads+FAQ
// https://cwiki.apache.org/confluence/pages/viewpage.action?pageId=158862687

// MEADOW.OS REBASE Note: Name was defined using
// CONFIG_USER_ENTRYPOINT="hcom_main" in Legacy Meadow. Nuttx 12 renamed this
// to CONFIG_INIT_ENTRYPOINT.
int hcom_main(int argc, char *argv[])
{
  int ret;

#if HCOM_DIAG_OUTPUT_SYSLOG_PID_OF_NEW_THREADS > 0
  syslog(LOG_MDIAG, "Nuttx startup pthread [Thrd:%d][Task:%d] %s\n",
     gettid(), getpid(), "hcom_main()");
  usleep(10 * 1000);
#endif

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Startup Manager 1\n"); usleep(10 * 1000);
#endif

  // To better control the startup sequence a semaphore is used.
  // This thread will wait for those setup routines that immediately
  // create a thread to complete before continuing with the startup.
  // Initialize value to 0 for a 'signaling' semaphore to block
  // the calling thread (this one) until it is okay for it to proceed.
  sem_init(&_startupWaitSem, 0, 0);
  // Special non-standard nuttx function required for signaling semaphores
  sem_setprotocol(&_startupWaitSem, SEM_PRIO_NONE);

  #if defined(CONFIG_HCOM_MONO_STDERR_STDOUT)
  //
  //  We take a copy of the config and set and application global variables first
  //  to make them available to the various components that need them.  Doing this
  //  at startup saves having to make repeated calls to get the configuration from
  //  the OS.
  //
  meadow_configuration_t *config = meadow_os_deep_copy_config();
  if (config == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Deep copy config failed\n", thisFile, __LINE__);
    return -ENOMEM;
  }
  #if defined (CONFIG_HCOM_MONO_STDERR_STDOUT)
  g_copy_application_output_to_uart = config->copy_application_output_to_uart && config->use_uart1_for_trace;
  #endif

  meadow_os_config_free_resources(config);
#endif

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Startup Manager 1\n"); usleep(20 * 1000);
#endif
  
  // Opens the nuttx upd driver to allow nuttx-side access.
  ret = hcom_via_nx_upd_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup hcom nx access:%d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 2\n"); usleep(20 * 1000);
#endif

  // Restore previous syslog mask from the battery backed register (BBR).
  // Note: This needs to be third because all hcom_logging_syslog
  // calls are filtered by the results of this call. Nothing to wait for.
  ret = hcom_diag_trace_priority_level_init();
  if(ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-hcom_diag_trace_priority_level_init failed:%d\n", thisFile, __LINE__, ret);
    return ret; 
  }

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 3\n"); usleep(20 * 1000);
#endif

  // Does nothing
  ret = hcom_common_utils_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup hcom utils:%d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 4\n"); usleep(20 * 1000);
#endif

  // Does nothing
  ret = hcom_diag_misc_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup misc %d\n", thisFile, __LINE__, ret);
    return ret;
  }

  //
  //  Start the ESP UART network monitor thread and then reset the ESP32.
  //
#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 5\n"); usleep(20 * 1000);
#endif

  hcom_esp32_network_monitor_start();
  
#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 6\n"); usleep(20 * 1000);
#endif

#if defined(CONFIG_MEADOW_ESPCP_MANAGER)
  meadow_os_espcp_reset();
#endif

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 7\n"); usleep(20 * 1000);
#endif

  // Only sets a bool if CONFIG_SYSTEM_NSH set
  ret = hcom_diag_nsh_support_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-nsh setup %d\n", thisFile, __LINE__, ret);
    return ret;
  }
  
#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 8\n"); usleep(20 * 1000);
#endif

  // Does nothing
  ret = hcom_misc_rqst_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup misc %d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 9\n"); usleep(20 * 1000);
#endif

  // Sets internal variable state
  ret = hcom_file_dnld_stm32f7_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup file download %d\n", thisFile, __LINE__, ret);
    return ret;
  }

  // Sets internal variable state
  ret = hcom_file_dnld_esp32_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup esp32 file download %d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 10\n"); usleep(20 * 1000);
#endif

  // Sets internal variable state
  ret = hcom_file_upld_proc_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup file upload %d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 11\n"); usleep(20 * 1000);
#endif

  // Allocates memory and sets a few internal variable states
  ret = hcom_file_write_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup file cmds %d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 12\n"); usleep(20 * 1000);
#endif

  // Allocates memory and initializes hcom circular buffer
  ret = hcom_host_enq_deq_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup host request %d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 13\n"); usleep(20 * 1000);
#endif

  // Sets one variable
  ret = hcom_host_route_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup host request %d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 14\n"); usleep(20 * 1000);
#endif

#if defined(CONFIG_HCOM_MONO_STDERR_STDOUT)
  // Creates a fifo and a thread to receive stdout
  ret = hcom_mono_stdxxx_read_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup mono stdxxx fifo %d\n", thisFile, __LINE__, ret);
    return ret;
  }
  
#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 15\n"); usleep(20 * 1000);
#endif

  ret = hcom_startup_mgr_takesem(&_startupWaitSem);
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup mono stdxxx fifo %d\n", thisFile, __LINE__, ret);
    return ret;
  }
#endif

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 16\n"); usleep(20 * 1000);
#endif

#if defined(CONFIG_HCOM_MONO_REMOTE_DEBUGGING)
  // Sets a few variables
  ret = hcom_mono_remote_dbg_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup remote dbg %d\n", thisFile, __LINE__, ret);
    return ret;
  }
#endif

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 17\n"); usleep(20 * 1000);
#endif

#if defined (CONFIG_HCOM_ESP32_COMMS)
  // Sets a few internal variables
  ret = hcom_esp32_uart_comms_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup esp32 comms %d\n", thisFile, __LINE__, ret);
    return ret;
  }
#endif

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 18\n"); usleep(20 * 1000);
#endif
  // Allocates memory and sets some internal variables
  ret = hcom_host_send_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup host msg builder:%d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 19\n"); usleep(20 * 1000);
#endif

  // Creates a thread to run hcom USB serial receive
  ret = hcom_host_recv_setup();  // Handle CLI commands
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup Host comms %d\n", thisFile, __LINE__, ret);
    return ret;
  }  // Wait, hcom_host_recv_setup creates a new thread that must start before we continue
  ret = hcom_startup_mgr_takesem(&_startupWaitSem);
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup Host comms %d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 20\n"); usleep(20 * 1000);
#endif

  // This is for transporting syslog messages to CLI. But, only if BBR requests it.
  ret = hcom_trace_to_cli_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-provide cli access %d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 21\n"); usleep(20 * 1000);
#endif

  // This is for transporting text messages to Host.
  ret = hcom_host_text_transport_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-provide text transport %d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 22\n"); usleep(20 * 1000);
#endif

  // Minor setup, configures blue led as output
  ret = hcom_mono_ctrl_mono_main_setup();  // Handle CLI commands
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup mono main %d\n", thisFile, __LINE__, ret);
    return ret;
  }

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 23\n"); usleep(20 * 1000);
#endif

  // Open ESP32 events message queue on user space side
  espcp_open_esp32_events_message_queue();

  // Run system updaters, which apply any OS and filesystem updates that have been staged
#if(0)
  os_update();
  app_update();
  firmware_update();
#endif

// MEADOW.OS REBASE the 3 lines above caused the following build errors:
//
// arm-none-eabi-ld: /home/peter/wl/Meadow.OS/nuttx/staging//libapps.a(hcom_startup_manager.c.home.peter.wl.Meadow.OS.apps.system.hcom.o): in function `hcom_main':
// /home/peter/wl/Meadow.OS/apps/system/hcom/hcom_startup_manager.c:461: undefined reference to `os_update'
// arm-none-eabi-ld: /home/peter/wl/Meadow.OS/apps/system/hcom/hcom_startup_manager.c:462: undefined reference to `app_update'
// arm-none-eabi-ld: /home/peter/wl/Meadow.OS/apps/system/hcom/hcom_startup_manager.c:463: undefined reference to `firmware_update'
// arm-none-eabi-ld: /home/peter/wl/Meadow.OS/apps/system/hcom/hcom_startup_manager.c:473: undefined reference to `hcom_pppd_start'

// Last stop, start mono
// MEADOW.OS REBASE - Besides a build error without mono built attempting to
// start it is a bad idea.
#if(0)
  int monoStartResult = hcom_mono_ctrl_start_mono_main();
#endif
// The above line cause the following build errors:
// arm-none-eabi-ld: /home/peter/wl/Meadow.OS/nuttx/staging//libapps.a(hcom_startup_manager.c.home.peter.wl.Meadow.OS.apps.system.hcom.o): in function `hcom_main':
// /home/peter/wl/Meadow.OS/apps/system/hcom/hcom_startup_manager.c:480: undefined reference to `hcom_pppd_start'

  // Start PPPD app needed by cell driver, when cell interface is enabled
  // and Mono is running
#if(0)
  if (monoStartResult == OK)
  {
    ret = hcom_pppd_start();
    if (ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-start pppd %d\n", thisFile, __LINE__, ret);
    }
  }
#endif
// MEADOW.OS REBASE the above not useable without previous code

#if HCOM_DIAG_INCLUDE_APP_STARTUP_SYSLOG > 0
  syslog(LOG_MDIAG, "Apps startup manager 24 - All successful\n"); usleep(20 * 1000);
#endif

  // This startup thread was created by Nuttx. It will now be used to run the
  // task of processing CLI messages. So, it won't return from this call.
  ret = hcom_host_process_setup();
  if (ret < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setup host request %d\n", thisFile, __LINE__, ret);
    return ret;
  }

  sem_destroy(&_startupWaitSem);
  return OK;
}

//=========================================================================
// Notify all interested children that we are shutting down. This closes the
// comms file descriptor which will cause the worker thread to exit.
// At present this is never called
void hcom_manager_shutdown()
{  
  hcom_host_recv_shutdown();
#if defined(CONFIG_HCOM_MONO_STDERR_STDOUT)
  hcom_mono_stdxxx_read_shutdown();
#endif
  hcom_common_utils_shutdown();
  hcom_host_route_shutdown();  
  hcom_host_enq_deq_shutdown();
  hcom_host_send_shutdown();
  hcom_file_write_shutdown();
  hcom_mono_remote_dbg_shutdown();
#if defined (CONFIG_HCOM_ESP32_COMMS)
  hcom_esp32_uart_comms_shutdown();
#endif
}
