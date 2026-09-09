/****************************************************************************
 * Meadow.OS/hcom_nx/diag/hcom_nx_trace_msg_proc.c
 *
 *   Copyright (C) 2021-2026 Wilderness Labs. All rights reserved.
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

#include "../hcom_nx_common.h"

#include <meadow/meadow_apps_core_share.h>
#include <meadow/hcom_protocol.h>
#include <meadow/hcom_bbreg_defn.h>
#include <meadow/hcom_traverse_ku.h>
#include "../hcom_nx_config_manager.h"

#include <nuttx/kthread.h>
#include <nuttx/kmalloc.h>

#include <sys/stat.h>
#include <ctype.h>
#include <poll.h>

#include <signal.h>

#include <arch/board/board.h>
#include "arm_internal.h"   // getreg32()

#include "stm32_gpio.h"

// #define USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>

// #pragma GCC optimize("O0")    // Prevent compiler from changing the code

#if defined (CONFIG_RAMLOG_SYSLOG)
/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
// This is an arbituary value. Nuttx has no limit on syslog messages.
#define HCOM_TRACE_RAMLOG_ASSUME_LARGEST_SYSLOG (512)
#define HCOM_TRACE_RAMLOG_RECONFIG_TIMEOUT (30)   // Seconds to reconfigure

#define HCOM_TRACE_RAMLOG_SERIAL_PORT_NAME_1 ("/dev/ttyS0")    // UART 1
#define HCOM_TRACE_RAMLOG_SERIAL_PORT_NAME_4 ("/dev/ttyS1")    // UART 4
#define HCOM_TRACE_RAMLOG_SERIAL_PORT_NAME_6 ("/dev/ttyS3")    // UART 6

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/
#endif

static char *thisFile = __FILE__;

// This covers most of this file
#if defined (CONFIG_RAMLOG_SYSLOG)

static bool _shutting_down;
// This buffer receives complete syslog lines from fgets().
static uint8_t *_syslogMsgBuf;
static FILE *_ramlog_fp;
static int _uart_fd;
static bool _trace_kthread_running;
static bool _trace_log_to_host;
static bool _trace_log_to_uart;
static bool _profiler_log_to_uart;
static sem_t _startupSem;
static int _ramlog_reader_kthread_pid;
static uint32_t _txRxReCfgStopTime;

/****************************************************************************
 * Private Functions
 ****************************************************************************/
static void *hcom_nx_trace_msg_kthread(int argc, char *argv[]);
static void hcom_nx_trace_read_bbreg_config(void);
static int hcom_nx_trace_msg_make_thread(void);
static int hcom_nx_trace_msg_open_ramlog(void);
static int hcom_nx_trace_msg_lazy_initialization(void);
static void hcom_nx_trace_msg_close_and_delay(bool ramLogClose);
static int hcom_nx_trace_msg_read_ramlog_loop(void);
static int hcom_nx_trace_msg_route_trace_text(char* syslogMsg, size_t syslogMsgLen);
static int hcom_nx_trace_msg_send_msg_to_uart(const char *toUartBuf, size_t numbBytes);
static int hcom_nx_trace_msg_open_uart_serial_port(void);
static void hcom_nx_trace_msg_wait_sem(sem_t *semaphore);
static void hcom_nx_trace_kthread_exit_initiate(void);
static void hcom_nx_trace_kthread_exit_cleanup(void);
static void hcom_nx_trace_msg_sig_recv(int signo, FAR siginfo_t *info, FAR void *context);
static void hcom_nx_uart_direct(int priority, const char *outputMsg, ...);

//=========================================================================
// Returns the current time as a 32-bit number representing millisec time.
static uint32_t hcom_nx_trace_get_time_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return(((uint32_t)ts.tv_sec * MSEC_PER_SEC) + ts.tv_nsec / NSEC_PER_MSEC);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/
int hcom_nx_trace_msg_proc_setup()
{
  _trace_kthread_running = false;
  _ramlog_reader_kthread_pid = 0;
  _txRxReCfgStopTime = 0;
  sem_init(&_startupSem, 0, 1);

  // Check the BBR to determine if initialization is needed, but only
  // during startup. After startup, this is controlled by CLI command
  hcom_nx_trace_read_bbreg_config();

  // Follow through with initialization if needed
  if((_trace_log_to_host || _trace_log_to_uart) && !_profiler_log_to_uart)
    hcom_nx_trace_msg_lazy_initialization();
  return OK;
}

//==========================================================================
// This function reads the battery backed register and sets appropriate flags
// at startup
void hcom_nx_trace_read_bbreg_config()
{
  uint32_t bbrRegValue;

  // If started after nuttx start time recheck the battery
  // backed registers as it could have been long ago.
  bbrRegValue = getreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER);
  if((HCOM_BBREG_ROUTE_TRACE_MSG_TO_UART_BIT & bbrRegValue) > 0)
    _trace_log_to_uart = true;
  else
    _trace_log_to_uart = false;

#if HCOM_FORCE_SYSLOG_MASK_AND_OUTPUT_TO_UART > 0
  _trace_log_to_uart = true;
#endif

  if((HCOM_BBREG_ROUTE_TRACE_MSG_TO_HOST_BIT & bbrRegValue) > 0)
    _trace_log_to_host = true;
  else
    _trace_log_to_host = false;

  if((HCOM_BBREG_ROUTE_PROFILER_BINS_TO_UART_BIT & bbrRegValue) > 0)
    _profiler_log_to_uart = true;
  else
    _profiler_log_to_uart = false;
}

//==========================================================================
// Called when mono starts
int hcom_nx_trace_msg_mono_started()
{
  // When this function is called we know mono has begun running. When mono does
  // begin it will reconfigured all of the GPIOs, including those needed by this
  // code to send messages to the UART. Therefore, we must reconfigures these GPIOs
  // We don't want to do this all the time because there's no reason. So, we'll
  // grab the current time and calculate a reasonable reconfiguration stop time.
  _txRxReCfgStopTime = hcom_nx_trace_get_time_ms();
  _txRxReCfgStopTime += ((uint32_t)HCOM_TRACE_RAMLOG_RECONFIG_TIMEOUT * MSEC_PER_SEC);

  return OK;
}

//==========================================================================
// Because this feature is rarely used we initialize only when requested
// May be called at start or from CLI command or both
int hcom_nx_trace_msg_lazy_initialization()
{
  int ret;

  // Need to guard the thread running flag otherwise may get
  // 2 ramlog reader threads
  hcom_nx_trace_msg_wait_sem(&_startupSem);
  if(_trace_kthread_running)  // Already running? Don't restart
  {
    sem_post(&_startupSem);
    return OK;
  }

  _ramlog_fp = NULL;
  _uart_fd = -1;
  _ramlog_reader_kthread_pid = 0;

  // When trace logging is started and uart is able to output messages via
  // uart we'll output one message very early. This message will not be output
  // using syslog but, directly via the uart. This will indicate that Meadow
  // has started. Also, since executed at startup, if the OS crashes, we
  // should still see this message which gives us a clue why no other trace
  // messages follow.
  if(_trace_log_to_uart && !_profiler_log_to_uart)
  {
    struct tm tmNow;
    char timeBuf[64];

    ret = up_rtc_getdatetime(&tmNow);
    if(ret < 0)
    {
      sem_post(&_startupSem);
      return -EINVAL;
    }

    snprintf_chk(timeBuf, 64, "%04d-%02d-%02d %02d:%02d:%02d",
      tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday,
      tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);

    hcom_nx_uart_direct(0,
      "\nMeadow.OS initialization began at %s Meadow UTC time.\n",
      timeBuf);

    // Close uart file descriptor opened by hcom_nx_uart_direct call.
    // This is because the file descriptor is open by a different thread
    // than the one that will normally handle trace processing.
    close(_uart_fd);
    _uart_fd = -1;
  }

  // Message buffer containing complete syslog lines delivered by fgets()
  _syslogMsgBuf = malloc(HCOM_TRACE_RAMLOG_ASSUME_LARGEST_SYSLOG);
  if (_syslogMsgBuf == NULL)
  {
    hcom_nx_uart_direct(LOG_ERR, "%s@%d-cir buf malloc\n", thisFile, __LINE__);
    sem_post(&_startupSem);
    return -ENOMEM;
  }

  // Create a thread to read the ramlog
  ret = hcom_nx_trace_msg_make_thread();
  if (ret < 0)
  {
    hcom_nx_uart_direct(LOG_ERR, "%s@%d-thread create, errno:%d\n",
              thisFile, __LINE__, errno);

    free(_syslogMsgBuf);
    _syslogMsgBuf = NULL;
    sem_post(&_startupSem);
    return -1;
  }

  return OK;
}

//=============================================================
// Create a kthread to use as a syslog message reader
int hcom_nx_trace_msg_make_thread()
{
  _ramlog_reader_kthread_pid = kthread_create(HCOM_THREAD_NAME_TRACE_RAMLOG,
                                   HCOM_THREAD_PRIORITY_TRACE_RAMLOG,
                                   HCOM_THREAD_STACKSIZE_TRACE_RAMLOG,
                                   (main_t) hcom_nx_trace_msg_kthread,
                                   (char *const *) NULL);
  if (_ramlog_reader_kthread_pid <= 0)
  {
      return -ENOEXEC;
  }

  return OK;
}

//=================================================================
// This thread reads all ramlog messages received via syslog from
// the ramlog buffer and routes them to the UART and/or CLI. This
// thread once started is not stopped until Meadow reset.
void *hcom_nx_trace_msg_kthread(int argc, char *argv[])
{
  int ret;
  struct sigaction act;

  _trace_kthread_running = true;
  _shutting_down = false;
  sem_post(&_startupSem);

#if HCOM_DIAG_OUTPUT_SYSLOG_PID_OF_NEW_THREADS > 0
  syslog(LOG_MDIAG, "New kthread [Thrd:%d][Task:%d] %s\n",
    gettid(), getpid(), HCOM_THREAD_NAME_TRACE_RAMLOG);
#endif

  // Attach a signal handler to catch the kill command
  act.sa_sigaction = hcom_nx_trace_msg_sig_recv;
  act.sa_flags = SA_SIGINFO;
  sigemptyset(&act.sa_mask);

  ret = sigaction(SIGALRM, &act, NULL);
  if (ret < 0)
  {
    _trace_kthread_running = false;
    return NULL;
  }

  // Only leave this loop when shutting down
  while(!_shutting_down)
  {
    // Must have ramlog, it's the source of all trace info
    ret = hcom_nx_trace_msg_open_ramlog();
    if(_shutting_down) break;
    if(ret < 0)
    {
      hcom_nx_trace_msg_close_and_delay(false);
      continue;
    }

    // Open uart if it's requested
    if(_trace_log_to_uart && !_profiler_log_to_uart)
    {
      ret = hcom_nx_trace_msg_open_uart_serial_port();
      if(_shutting_down) break;
      if(ret < 0)
      {
        hcom_nx_trace_msg_close_and_delay(false);
        continue;
      }
    }

    // Attempt to read from the ramlog buffer and don't return
    ret = hcom_nx_trace_msg_read_ramlog_loop();
    if(_shutting_down) break;
    if(ret < 0)
    {
      hcom_nx_trace_msg_close_and_delay(true);
    }
  }
  //-------------------------------------------

  // Time for this thread to exit
  hcom_nx_trace_kthread_exit_cleanup();

  _trace_kthread_running = false;

  return NULL;
}

//=================================================================
// Need a signal handler or the default Nuttx behavior kicks in
void hcom_nx_trace_msg_sig_recv(int signo, FAR siginfo_t *info,
  FAR void *context)
{
  return;
}

//==========================================================================
// Initiate the kthread exit.
void hcom_nx_trace_kthread_exit_initiate()
{
  // A pthread cannot kill a kthread so we must indirectly kill the thread
  // by setting _shutting_down true, closing connections and waiting
  _shutting_down = true;

  // Signal the kthread so _shutting_down will be tested, forcing it to exit
  kill(_ramlog_reader_kthread_pid, SIGALRM);

  // Nuttx kthreads have no 'join' functionality. We stay in this loop
  // until the exiting kthread has cleaned up it's resources.
  while(_trace_kthread_running)
  {
    usleep(10 * 1000);
  }

  // sem_destroy(&_readNxtSem);
  // sem_destroy(&_sendCliSem);
}

//==========================================================================
// This function is called by the kthread just before it exits
// and is used to reclaim resources
void hcom_nx_trace_kthread_exit_cleanup()
{
  _trace_log_to_uart = false;
  _trace_log_to_host = false;
  _profiler_log_to_uart = false;

  if(_ramlog_fp != NULL)
  {
    fclose(_ramlog_fp);
    _ramlog_fp = NULL;
  }

  close(_uart_fd);
  _uart_fd = -1;

  if(_syslogMsgBuf != NULL)
  {
    free(_syslogMsgBuf);
    _syslogMsgBuf = NULL;
  }
}

//=================================================================
void hcom_nx_trace_msg_close_and_delay(bool ramLogClose)
{
  if(ramLogClose && _ramlog_fp != NULL)
  {
    fclose(_ramlog_fp);
    _ramlog_fp = NULL;
  }

  if(_uart_fd > -1)
  {
    close(_uart_fd);
    _uart_fd = -1;
  }

  // Wait and try again
  if(!_shutting_down)
    sleep(1);   // Not a special value, just prevent hard infinite looping
}

//=================================================================
int hcom_nx_trace_msg_open_ramlog()
{
  if(_ramlog_fp != NULL)
  {
    fclose(_ramlog_fp);
    _ramlog_fp = NULL;
  }

  _ramlog_fp = fopen(HCOM_TRACE_RAMLOG_DEVICE_NAME, "r");
  if(_shutting_down) return OK;
  if (_ramlog_fp == NULL)
  {
    hcom_nx_uart_direct(LOG_ERR, "%s@%d-fopen %s, errno:%d\n",
            thisFile, __LINE__, HCOM_TRACE_RAMLOG_DEVICE_NAME, errno);
    return -1;
  }

  return OK;
}

//=================================================================
int hcom_nx_trace_msg_open_uart_serial_port()
{
  if(_uart_fd > -1)
  {
    close(_uart_fd);
    _uart_fd = -1;
  }

#if HCOM_DIAG_SYSLOG_UART_NUMBER == 1
  _uart_fd = open(HCOM_TRACE_RAMLOG_SERIAL_PORT_NAME_1, O_WRONLY);
#elif HCOM_DIAG_SYSLOG_UART_NUMBER == 4
  _uart_fd = open(HCOM_TRACE_RAMLOG_SERIAL_PORT_NAME_4, O_WRONLY);
#elif HCOM_DIAG_SYSLOG_UART_NUMBER == 6
  _uart_fd = open(HCOM_TRACE_RAMLOG_SERIAL_PORT_NAME_6, O_WRONLY);
#else
#error "Must select a valid syslog uart"
#endif

  if(_shutting_down) return OK;
  if (_uart_fd < 0)
  {
    _uart_fd = -1;
    return -1;
  }
  return OK;
}

//=================================================================
// Read complete syslog lines from the ramlog using fgets() and
// route each line to the uart and/or host CLI.
// Returning on error causes the caller to close and reopen the stream.
int hcom_nx_trace_msg_read_ramlog_loop(void)
{
  int ret;
  size_t msgLen;

  while(!_shutting_down)
  {
    if(fgets((char *)_syslogMsgBuf, HCOM_TRACE_RAMLOG_ASSUME_LARGEST_SYSLOG,
             _ramlog_fp) == NULL)
    {
      if(_shutting_down) break;

      hcom_nx_uart_direct(LOG_ERR,
        "%s@%d ramlog fgets failed, errno:%d\n",
        thisFile, __LINE__, errno);
      return -errno;    // Close connection, wait and try again
    }

    if(_shutting_down) break;

    // Skip if all logging was disabled while blocked in fgets()
    if(!_trace_log_to_host && !_trace_log_to_uart)
      continue;

    msgLen = strlen((char *)_syslogMsgBuf);

    // Detect truncated message: buffer filled without encountering '\n'
    if(msgLen > 0 && _syslogMsgBuf[msgLen - 1] != '\n')
    {
      hcom_nx_uart_direct(LOG_ERR,
        "%s@%d-syslog message truncated at %zu bytes, no linefeed\n",
        thisFile, __LINE__, msgLen);
    }

    ret = hcom_nx_trace_msg_route_trace_text((char *)_syslogMsgBuf, msgLen);
    if(ret < 0)
    {
      hcom_nx_uart_direct(LOG_ERR,
        "%s@%d-route_trace_text returned:%d\n",
        thisFile, __LINE__, ret);
      return ret;    // Close connection, wait and try again
    }
  }   // while (!_shutting_down)

  return OK;
}

//=================================================================
// Ship the ramlog text message to uart and/or host CLI
// If this function cannot send it, the message is lost....
int hcom_nx_trace_msg_route_trace_text(char* syslogMsg, size_t syslogMsgLen)
{
  int ret;

  // To UART?
  if(_trace_log_to_uart && !_profiler_log_to_uart)
  {
    // Route to uart
    ret = hcom_nx_trace_msg_send_msg_to_uart((char *) syslogMsg, syslogMsgLen);
    if(ret < 0)
    {
      return ret;
    }
  }

  // To HOST?
  if(_trace_log_to_host)
  {
    // This will block till pipe reader opens the read end
    ret = hcom_traverse_ku_syslog_to_host((char *)  syslogMsg, syslogMsgLen);
    if(ret < 0)
    {
      return ret;
    }
  }

  return OK;
}


//==========================================================================
// There's really isn't a good way to handle errors with ramlog because errors
// cannot be written to syslog or we'll end up with an infinite loop. So,
// the best we can do is send them to UART, and hope it's being watched.
//--------------------------------------------------------------------------
// Send a string directly to uart. The priority value is not used but makes
// this function signature like syslog so that this can be used as a substitute
// for syslog.
// Note:This function can ONLY be called from a kthread.
// Note:Calling from a user-land pthread will CRASH Nuttx.
void hcom_nx_uart_direct(int priority, const char *fmt, ...)
{
#define HCOM_NX_UART_DIRECT_BUF_LEN (512)
  va_list ap;
  int stringLen;

  char *completeStr;
  completeStr = malloc(HCOM_NX_UART_DIRECT_BUF_LEN);
  if(completeStr == NULL)
  {
    // Can't do anything else at this point
    return;
  }

  // Create the complete message
  va_start(ap, fmt);

  // Returned value is the string length, including '\0'. But, if buffer is too
  // small the returned value is the needed buffer length excluding terminating
  // '\0'. However, sending direct to uart means a '\0' is not needed anyway.
  stringLen = vsnprintf(completeStr, HCOM_NX_UART_DIRECT_BUF_LEN, fmt, ap);
  va_end(ap);

  // We need the actual length. So if truncated we'll use the buffer's length
  if(stringLen > HCOM_NX_UART_DIRECT_BUF_LEN)
    stringLen = HCOM_NX_UART_DIRECT_BUF_LEN;

  hcom_nx_trace_msg_send_msg_to_uart(completeStr, stringLen);
  
  // Need a carriage return which we usually don't add
  hcom_nx_trace_msg_send_msg_to_uart("\r", 1);
  free(completeStr);
}

//==========================================================================
// Forward the message to the uart for transmission
int hcom_nx_trace_msg_send_msg_to_uart(const char *toUartBuf, size_t numbBytes)
{
  int ret;

  // If uart not opened do this now
  if(_uart_fd < 0 && !_profiler_log_to_uart)
  {
    ret = hcom_nx_trace_msg_open_uart_serial_port();
    if(ret < 0)
    {
      return ret;
    }
  }

  // Because the mono/meadow runtime may reconfigure all the GPIOs used by the
  // board we need to reconfigure them after mono starts. The problem is we
  // don't know when this is is. Therefore, when mono starts we set
  // _txRxReCfgStopTime to a predetermined amount of time.
  if(_txRxReCfgStopTime != 0)
  {
    // Reconfigure uart (takes about 32usec to do all 4 commands)
#if HCOM_DIAG_SYSLOG_UART_NUMBER == 1
    stm32_unconfiggpio(GPIO_USART1_TX); // PB14
    stm32_configgpio(GPIO_USART1_TX);
    stm32_unconfiggpio(GPIO_USART1_RX); // PB15
    stm32_configgpio(GPIO_USART1_RX);
#elif HCOM_DIAG_SYSLOG_UART_NUMBER == 4
    stm32_unconfiggpio(GPIO_UART4_RX); // PI9
    stm32_configgpio(GPIO_UART4_RX);
    stm32_unconfiggpio(GPIO_UART4_TX); // PH13
    stm32_configgpio(GPIO_UART4_TX);
#elif HCOM_DIAG_SYSLOG_UART_NUMBER == 6
    stm32_unconfiggpio(GPIO_USART6_RX); // PC7
    stm32_configgpio(GPIO_USART6_RX);
    stm32_unconfiggpio(GPIO_USART6_TX); // PC6
    stm32_configgpio(GPIO_USART6_TX);
#else
#error "Must select a valid syslog uart"
#endif

    // Run until stop time is exceeded then stop the process
    if(_txRxReCfgStopTime < hcom_nx_trace_get_time_ms())
    {
      _txRxReCfgStopTime = 0;      // Past so timeout no longer needed
    }
  }

  // Write string out the uart. A blocking write can return a short count if
  // interrupted by a signal mid-transfer (this kthread's own shutdown sends
  // itself SIGALRM) -- retry until every byte is sent or a real error occurs,
  // so a truncated message isn't silently dropped.
  size_t totalWritten = 0;
  while (totalWritten < numbBytes)
  {
    ssize_t nbytes = write(_uart_fd, toUartBuf + totalWritten,
                           numbBytes - totalWritten);
    if (nbytes < 0)
    {
      if (errno == EINTR)
        continue;

      return nbytes;
    }

    totalWritten += (size_t)nbytes;
  }

  return OK;
}

//===========================================================================
// Wait for the thread holding the semaphore to release it
void hcom_nx_trace_msg_wait_sem(sem_t *semaphore)
{
  int ret;  

  do
  {
    ret = sem_wait(semaphore);    // Take the semaphore (perhaps waiting)
  }
  while (ret == -EINTR);
}

#endif    // #if defined (CONFIG_RAMLOG_SYSLOG)

//======================================================================================
// The following functions are always built. They are used to process
// hcom commands received from CLI that control the tracing feature.
//======================================================================================
//
// Called from Meadow.CLI to enable tracing to host.
int hcom_nx_exec_trace_do_send_to_host(struct hcom_nx_cmd_data *cmdData)
{
  char *sendMsgToHost;
  int ret;

  // Set the appropriate battery backed register bit
  modifyreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER,
              0, HCOM_BBREG_ROUTE_TRACE_MSG_TO_HOST_BIT);

  // If ramlog configured, need to init ramlog now. This insures
  // that Meadow.CLI is listening
#if defined (CONFIG_RAMLOG_SYSLOG)
  if(_trace_log_to_host)
  {
    sendMsgToHost = "No change. Trace logs already sent to CLI";
  }
  else
  {
    _trace_log_to_host = true;

    // Initialize as needed
    ret = hcom_nx_trace_msg_lazy_initialization();
    if(ret < 0)
    {
      _trace_log_to_host = false;
      sendMsgToHost = "Trace logging failed to initialize";
    }
    else if(_trace_log_to_uart)
      sendMsgToHost = "Trace logs now sent to CLI and UART";
    else
      sendMsgToHost = "Trace logs now sent to CLI";
  }
#else
  sendMsgToHost = "Trace logging not available";
#endif

  cmdData->send_host_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
          sendMsgToHost, thisFile, __LINE__);
  return OK;
}

//======================================================================================
// Called from Meadow.CLI for ramlog output to host
int hcom_nx_exec_trace_do_not_send_to_host(struct hcom_nx_cmd_data *cmdData)
{
  char *sendMsgToHost;

  // Clear the appropriate battery backed register bit
  modifyreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER,
              HCOM_BBREG_ROUTE_TRACE_MSG_TO_HOST_BIT, 0);

#if defined (CONFIG_RAMLOG_SYSLOG)
  if(_trace_log_to_host)
  {
    // Disable ramlogs to host
    _trace_log_to_host = false;

    if(!_trace_log_to_uart)
    {
      // Since no longer needed, terminate the main ramlog reader thread
      // and clean up it's resources
      hcom_nx_trace_kthread_exit_initiate();
      sendMsgToHost = "Trace logging will be terminated";
    }
    else
    {
      sendMsgToHost = "Will no longer send trace logs to CLI";
    }
  }
  else
  {
    sendMsgToHost = "No change. Trace logs still not sent to CLI";
  }
#else
  sendMsgToHost = "Trace logging not available";
#endif

  cmdData->send_host_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
          sendMsgToHost, thisFile, __LINE__);

  return OK;
}

//======================================================================================
// Called from Meadow.CLI to enable tracing to uart.
int hcom_nx_exec_trace_forward_to_uart(struct hcom_nx_cmd_data *cmdData)
{
  char *sendMsgToHost;
  int ret;

  // Set the appropriate battery backed register bit
  modifyreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER,
              0, HCOM_BBREG_ROUTE_TRACE_MSG_TO_UART_BIT);

  // If ramlog configured, need to init ramlog now. This insures
  // that Meadow.CLI is listening
#if defined (CONFIG_RAMLOG_SYSLOG)
  if(_trace_log_to_uart)
  {
    sendMsgToHost = "No change. Trace logs already sent to UART";
  }
  else
  {
    _trace_log_to_uart = true;

    // Initialize if needed
    ret = hcom_nx_trace_msg_lazy_initialization();
    if(ret < 0)
    {
      _trace_log_to_uart = false;
      sendMsgToHost = "Trace logging failed to initialize";
    }
    else if(_trace_log_to_host)
      sendMsgToHost = "Trace logs now sent to UART and CLI";
    else
      sendMsgToHost = "Trace logs now sent to UART";
  }
#else
  sendMsgToHost = "Trace logging not available";
#endif

  cmdData->send_host_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
          sendMsgToHost, thisFile, __LINE__);

  return OK;
}

//======================================================================================
// Called from Meadow.CLI to stop ramlog output to uart
int hcom_nx_exec_trace_do_not_send_to_uart(struct hcom_nx_cmd_data *cmdData)
{
  char *sendMsgToHost;

  // Clear the appropriate battery backed register bit
  modifyreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER,
              HCOM_BBREG_ROUTE_TRACE_MSG_TO_UART_BIT, 0);

#if defined (CONFIG_RAMLOG_SYSLOG)
  if(_trace_log_to_uart)
  {
    _trace_log_to_uart = false;

    if(!_trace_log_to_host)
    {
      // Since no longer needed, terminate the main ramlog reader thread
      // and clean up it's resources
      hcom_nx_trace_kthread_exit_initiate();

      sendMsgToHost = "Trace logging will be terminated";
    }
    else
    {
      sendMsgToHost = "Will no longer send trace logs to UART";
    }
  }
  else
  {
    sendMsgToHost = "No change. Trace logs still not sent to UART";
  }
#else
  sendMsgToHost = "Trace logging not available";
#endif
  cmdData->send_host_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
            sendMsgToHost, thisFile, __LINE__);
  
  return OK;
}

//======================================================================================
// Called from Meadow.CLI to enable profiling data output to uart.
int hcom_nx_exec_profiler_forward_to_uart(struct hcom_nx_cmd_data *cmdData)
{
  char *sendMsgToHost;
  
   // Set the appropriate battery backed register bit
  modifyreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER,
              0, HCOM_BBREG_ROUTE_PROFILER_BINS_TO_UART_BIT);

  // If ramlog configured, need to init ramlog now. This insures
  // that Meadow.CLI is listening
#if defined (CONFIG_RAMLOG_SYSLOG)
  if(_profiler_log_to_uart)
  {
    sendMsgToHost = "No change. UART is still exclusive for profiling data";
  }
  else
  {
    _profiler_log_to_uart = true;
    sendMsgToHost = "UART is now exclusive for profiling data";
  }
#else
  sendMsgToHost = "Profiler logging not available";
#endif

  cmdData->send_host_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
          sendMsgToHost, thisFile, __LINE__);

  return OK;
}

//======================================================================================
// Called from Meadow.CLI to disable profiling data output to uart
int hcom_nx_exec_profiler_do_not_send_to_uart(struct hcom_nx_cmd_data *cmdData)
{
  char *sendMsgToHost;

  // Clear the appropriate battery backed register bit
  modifyreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER,
              HCOM_BBREG_ROUTE_PROFILER_BINS_TO_UART_BIT, 0);

#if defined (CONFIG_RAMLOG_SYSLOG)
  if(_profiler_log_to_uart)
  {
    _profiler_log_to_uart = false;
    sendMsgToHost = "UART not exclusive for profiler, profiling data might still be visible if enabled in Mono.";
  }
  else
  {
    sendMsgToHost = "No change. UART still not exclusive for profiler, profiling data might still be visible if enabled in Mono";
  }
#else
  sendMsgToHost = "Profiler logging not available";
#endif
  cmdData->send_host_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
            sendMsgToHost, thisFile, __LINE__);
  
  return OK;
}

#if defined (CONFIG_RAMLOG_SYSLOG)
//======================================================================================
// Called by meadow configuration after it has started. Once the meadow configuration
// has been parsed this method is called if it determines tracing should be enabled.
void hcom_nx_trace_insure_correct_config(bool uartTracing, bool cliTracing,
    bool uartProfiling)
{

  bool needToInit = false;

  if (uartProfiling)
  {
    _profiler_log_to_uart = true;
    _trace_log_to_uart = false;
  }
  else if(uartTracing && (!_trace_log_to_uart))
  {
    _trace_log_to_uart = true;
    needToInit = true;
  }

  if(cliTracing && (!_trace_log_to_host))
  {
    _trace_log_to_host = true;
    needToInit = true;
  }

  // At startup this initialization would not have been called so do it now
  if(needToInit)
  {
    hcom_nx_trace_msg_lazy_initialization();
  }

}
#endif
