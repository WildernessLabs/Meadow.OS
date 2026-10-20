/****************************************************************************
 * /include/meadow/meadow_thread_config.h
 * 
 *  Copyright (C) 2021-2022 Wilderness Labs. All rights reserved.
 *  Author:  Wilderness Labs
 * 
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
#ifndef __MEADOW_THREAD_CONFIG_H
#define __MEADOW_THREAD_CONFIG_H

#include <nuttx/config.h>

/**
 * This file contains the definitions for the threads as used by Meadow OS.  It
 * contains the definitions of the following parameters:
 * 
 *      1. Thread priority
 *      2. Thread name
 *      3. Thread stack size
 * 
 * The thread name is given to kthreads when they are created, and to the hcom
 * pthreads with pthread_setname_np() straight after pthread_create(), so that
 * ps, the debugger and the crash dump (task list) show it.
 * 
 * This file has been put together to make it easier to change the properties of
 * any single thread in context with the other threads in the system.
 * 
 * It is important that each group of properties for a thread includes the reasoning
 * behind the priority and stack size values in order to guide future changes.
 * 
 * The thread information below is presented in priority order with the highest at
 * the top and the lowest at the bottom.  Please preserve this order when adding
 * new entries.
 */


/**
 *      CONFIG_SCHED_HPWORKPRIORITY
 * 
 * This value is set using the NuttX configuration system.
 * 
 * Default value: 224
 * 
 * This is used by the SD card driver.
*/

/**
 * This thread is dedicated to the Point-to-Point Protocol Daemon (PPPD) 
 * utilized in Cell networking for establishing communication with the modules.
*/

#define HCOM_THREAD_PRIORITY_CELL_PPPD 200
#define HCOM_THREAD_NAME_CELL_PPPD "CellPPPD"
#define HCOM_THREAD_STACKSIZE_CELL_PPPD 4096

/**
 * The ESPCP thread is responsible for managing the communication between the STM32
 * main processor and the ESP32 coprocessor.  As such it should be considered to be
 * a hardware driver as the ESP32 is providing network communication for the STM32.
 * 
 * Note that the value for the thread priority is set slightly above the ESP32 event
 * handler thread.  The event handler thread needs communication to have completed
 * before it will have any data to process.
 * 
 * This thread will spend most of its time waiting on a message queue.  Messages can
 * come from the following sources:
 *      1. Network requests from NuttX
 *      2. Events from the ESP32
 *      3. Requests from Meadow.Core (Bluetooth, connect to WiFi etc.)
 * 
 * When a message is received the thread will communicate with the ESP32, process the
 * data and then go to sleep waiting for the next message.
 * 
 * This thread spends most of its time waiting for messages on a message queue.
 * 
 * The fact that this thread can be called independently from the network system suggests
 * we should avoid clashes with Ethernet or cellular drivers.
 */
#define ESPCP_THREAD_PRIORITY                       199
#define ESPCP_THREAD_NAME                           "EspcpMainThread"
#define ESPCP_THREAD_STACKSIZE                      4096

/**
 * The network monitor thread is responsible for monitoring the network control
 * signal UART that connects the ESP32 and the STM32.
 * 
 * This thread needs to be fairly high priority as it is responsible for keeping
 * the main ESP communications thread synchronised with the ESP32.
 */
#define ESPCP_NETWORK_MONITOR_PRIORITY          190
#define ESPCP_NETWORK_MONITOR_NAME              "EspcpUARTMonitor"
#define ESPCP_NETWORK_MONITOR_STACK_SIZE        8192

/**
 *  @brief Priority for the network monitor thread.
 */


/**
 * HCOM is the main communication thread for the Meadow OS.  It is responsible for
 * starting many of the other threads in the system.  It also supervises communication
 * with the host computer.  As such it should have a high priority.
 */
#define HCOM_THREAD_PRIORITY_HCOM_RECEIVE           180
#define HCOM_THREAD_NAME_HCOM_RECEIVE               "HcomRecv"
#define HCOM_THREAD_STACKSIZE_HCOM_RECEIVE          2048

/**
 * Testing showed with priority of Process being higher than Receive there
 * were very rare download errors. This is probably in hcom_host_enq_deq.c.
 * With equal priority no errors have been detected.
 * I believe there is room for improvement in hcom_host_enq_deq.c.
 * 
 * Stack size is set by CONFIG_USERMAIN_STACKSIZE.
 * 
 * This value is set using the NuttX configuration system.
 */
#define HCOM_THREAD_PRIORITY_HCOM_PROCESS           180
#define HCOM_THREAD_NAME_HCOM_PROCESS               "HcomProc"
#define HCOM_THREAD_STACKSIZE_HCOM_PROCESS          CONFIG_USERMAIN_STACKSIZE

/**
 * The ESPCP event handler thread is responsible for processing the events that are
 * generated by the ESP32 coprocessor.  These events are mainly for Meadow.Core but
 * there are a few events which will impact the OS (network disconnect etc).
 * 
 * This thread relies upon data being made available through the main ESPCP thread.
 * 
 * The thread spend most of its time waiting for messages on the event message queue and
 * follows the same pattern as the ESP32 main thread.  Messages on this thread should
 * be rare.
 */
#define ESPCP_EVENT_HANDLER_THREAD_PRIORITY         170
#define ESPCP_EVENT_HANDLER_THREAD_NAME             "EspcpEventHandler"
#define ESPCP_EVENT_HANDLER_THREAD_STACKSIZE        4096

/**
 * This thread is used for remote debugging mono apps.  This thread takes the
 * requests from HCOM and passes them on to the mono debugger.
 */
#define HCOM_THREAD_PRIORITY_REMOTE_DBG             130
#define HCOM_THREAD_NAME_REMOTE_DBG                 "RemoteDbg"
#define HCOM_THREAD_STACKSIZE_REMOTE_DBG            2048

/**
 * Ensure HCOM recv thread runs before ESP32 recv, which is only used to program the
 * ESP32 from HCOM. Here this thread's priority is boosted ahead of most of the HCOM 
 * threads.
 */
#define HCOM_THREAD_PRIORITY_ESP32_RECEIVE          125
#define HCOM_THREAD_NAME_ESP32_RECEIVE              "EspRecv"
#define HCOM_THREAD_STACKSIZE_ESP32_RECEIVE         2048

/**
 * The ramlog is part of nuttx and contains the syslog text
 */
#define HCOM_THREAD_PRIORITY_TRACE_RAMLOG           120
#define HCOM_THREAD_NAME_TRACE_RAMLOG               "RamlogRead"
#define HCOM_THREAD_STACKSIZE_TRACE_RAMLOG          2048

/**
 * Copies syslog from the ramlog to the second USB port, only in a
 * 'build.sh --dualcdc' developer build. Below RamlogRead so the UART and
 * host syslog outputs are served first.
 */
#define HCOM_THREAD_PRIORITY_TRACE_USB              110
#define HCOM_THREAD_NAME_TRACE_USB                  "RamlogUsb"
#define HCOM_THREAD_STACKSIZE_TRACE_USB             2048

/**
 * Plays a .WAV file on a DAC channel (meadow_dac_play_file(), issue #664),
 * one thread per playing channel. Above managed code (MONO_TASK_PRIORITY)
 * and the low-priority work queue so neither can delay it, below the ESP32
 * and HCOM threads. See Documentation/MeadowDac.md, "File Player".
 */
#define DAC_PLAYER_THREAD_PRIORITY                  110
#define DAC_PLAYER_THREAD_NAME                      "DacPlayer"
#define DAC_PLAYER_THREAD_STACKSIZE                 2048

/**
 * This thread reads stdout and stderr and forwards text to the Host
 */
#define HCOM_THREAD_PRIORITY_STDXXX_REDIRECT        120
#define HCOM_THREAD_STACKSIZE_STDXXX_REDIRECT       2048
#define HCOM_THREAD_NAME_STDXXX_REDIRECT            "MonoStdxxx"

/**
 * This thread receives HCOM messages from the kernel side and forwards them
 * to CLI via HCOM.
 */
#define HCOM_THREAD_PRIORITY_RECV_TRAVERSE          120
#define HCOM_THREAD_NAME_RECV_TRAVERSE              "Traverse"
#define HCOM_THREAD_STACKSIZE_RECV_TRAVERSE         2048

/**
 * This thread works out how far the LSI drifts from real time.  The thread
 * is transient running for about 5 seconds at startup.  The thread priority
 * is set to 120, matching the HCOM threads used for communications with mono
 * (HCOM_THREAD_PRIORITY_TRACE_RAMLOG / _STDXXX_REDIRECT / _RECV_TRAVERSE).
 * Equal priority only shares CPU time round-robin between these threads; it
 * does not exclude them from running concurrently with this one. No issue
 * has been observed from this in practice since the thread is transient and
 * runs at startup, but this priority value alone should not be relied on for
 * mutual exclusion if that is ever actually required.
 */
#define PWRMGMT_CAL_LSI_THREAD_NAME                 "LSI Calibrate"
#define PWRMGMT_CAL_LSI_THREAD_PRIORITY             (120)
#define PWRMGMT_CAL_LSI_THREAD_STACKSIZE            (2048)

/**
 *      CONFIG_SCHED_LPWORKPRIORITY
 * 
 * This value is set using the NuttX configuration system.
 * 
 * Default value: 100
 * 
 * This is used by the Ethernet and cell drivers.
*/

/**
 * This thread waits for the Mono task to exit and then resets the board.  It
 * sleeps in waitpid() for the life of Mono, so the priority only decides how
 * quickly the reset follows an exit.  100 matches the hcom bring-up thread that
 * used to be inherited, and is above MONO_TASK_PRIORITY.
 *
 * The stack size is a measured value, not a guess: see "Measuring a thread
 * stack" in Documentation/Debugging.md.  Peak use while waiting in waitpid()
 * was 704 bytes (measured with CONFIG_STACK_COLORATION, Mono running).  The
 * code that runs after Mono exits (the reset syscall) is not measured, so the
 * size keeps about 1100 bytes of margin, as the other 2048 byte hcom threads do.
 */
#define HCOM_THREAD_PRIORITY_MONO_MONITOR           100
#define HCOM_THREAD_NAME_MONO_MONITOR               "MonoMonitor"
#define HCOM_THREAD_STACKSIZE_MONO_MONITOR          2048


/**
 * Task monitoring for USB device connect / disconnect.
 */
#define USBHOST_TASK_PRIORITY                       100
#define USBHOST_TASK_NAME                           "USBHost"
#define USBHOST_TASK_STACKSIZE                      1024

/**
 * This task runs the mono runtime system and any threads created as part of
 * application execution.  Note that threads launched by Mono run at this priority
 * too, for a managed ThreadPriority of Normal.  This has the effect of putting in a
 * round-robin scheduling pattern.  Care should be taken when creating any other
 * threads at this priority as they will be impacted by the number of threads created
 * by mono and the user application.
 * 
 * The other managed priorities are spread over a band around this one, 70 (Lowest)
 * to 90 (Highest) in steps of 5, which is below every other thread in this file.
 * 
 * **** IMPORTANT ****
 * The runtime has its own definitions of these priorities,
 * MONO_NUTTX_THREAD_PRIORITY_LOWEST, _NORMAL and _HIGHEST in
 * mono/mono/utils/mono-threads.h.  _NORMAL must equal this priority, and the band
 * must stay below the lowest priority of the system threads above (100 today).  Any
 * change here must be replicated there.
 */
#define MONO_TASK_PRIORITY                          80
#define MONO_TASK_NAME                              "Mono"
#define MONO_TASK_STACKSIZE                         CONFIG_PTHREAD_STACK_DEFAULT

#endif // __MEADOW_THREAD_CONFIG_H