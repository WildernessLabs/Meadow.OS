/****************************************************************************
 * espcp_common.c
 *
 *  Copyright (C) 2020 Wilderness Labs. All rights reserved.
 *  Author: Mark Stevens
 * 
 *  Methods supporting the ESP system functions (e.g. GetBatteryChargeLevel).
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
#include <nuttx/config.h>

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>
#include <syslog.h>
#include <unistd.h>
#include <semaphore.h>

#include <nuttx/semaphore.h>
#include <nuttx/net/net.h>

#include "espcp_common.h"
#include "espcp_message_dispatcher.h"
#include <meadow/hcom_shared_common.h>
#include "../hcom_nx/hcom_nx_config_manager.h"

/****************************************************************************
 * Uncomment the #define below to turn on debug help macros.
 ****************************************************************************/
// #define USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>

/****************************************************************************
 * Definitions
 ****************************************************************************/

/**
 *  How long to wait for the ESP32 to answer a blocking request, and the number of
 *  consecutive unanswered requests after which the ESP32 is declared not responding
 *  (later calls then fail fast with ENETDOWN instead of each waiting out a timeout).
 */
#define ESPCP_RESPONSE_TIMEOUT_MILLISECONDS     30000
#define ESPCP_RESPONSE_TIMEOUT_LIMIT            3

/****************************************************************************
 * Private Data / Variables
 ****************************************************************************/

/**
 *  Number of requests in a row that the ESP32 has not answered.
 */
static volatile int g_espcp_response_timeouts = 0;

/****************************************************************************
 * Function Implementation
 ****************************************************************************/

/****************************************************************************
 * Name: espcp_queue_message_timed_out
 *
 * Description:
 *  Deal with a blocking request that has not been answered in time.
 *
 *  The message is detached from the dispatcher's waiting list before this
 *  returns, so a late response can no longer post the caller's stack
 *  semaphore or write into a message the caller is about to free.
 *
 * Input Parameters:
 *  message - Pointer to the unanswered espcp_message_t object.
 *
 * Returned Value:
 *  espcp_status_codes_timeout if the request was sent and never answered,
 *  espcp_status_codes_completed_ok if the response arrived after all.
 *
 ****************************************************************************/
static uint32_t espcp_queue_message_timed_out(espcp_message_t *message)
{
    if (espcp_cancel_waiting_message(message))
    {
        //
        //  Sent but never answered.  Count the losses and, after enough of them in a row, mark
        //  the ESP32 as not responding so later calls fail fast.
        //
        g_espcp_response_timeouts++;
        if (g_espcp_response_timeouts >= ESPCP_RESPONSE_TIMEOUT_LIMIT)
        {
            syslog(LOG_ERR, "espcp: %d consecutive response timeouts, marking the ESP32 as not responding\n",
                   g_espcp_response_timeouts);
            espcp_config_lock();
            if (espcp_get_configuration() != NULL)
            {
                espcp_get_configuration()->esp_not_responding = true;
            }
            espcp_config_unlock();
        }
        return (espcp_status_codes_timeout);
    }

    //
    //  Not on the waiting list: either the response raced the timeout (the semaphore has already
    //  been posted) or the message has not been transmitted yet.  Allow a little time for the post.
    //  If the message is still unsent after that, fall back to waiting for ever, as returning would
    //  free a message that the espcp thread still owns.
    //
    for (int i = 0; i < 100; i++)
    {
        if (nxsem_trywait(message->semaphore) == 0)
        {
            g_espcp_response_timeouts = 0;
            return (espcp_status_codes_completed_ok);
        }
        usleep(100000);
    }

    syslog(LOG_ERR, "espcp: message 0x%08x is stuck before transmission, waiting for it\n",
           (unsigned int) message->message_id);
    if (net_sem_wait_uninterruptible(message->semaphore) != 0)
    {
        return (espcp_status_codes_failure);
    }
    g_espcp_response_timeouts = 0;
    return (espcp_status_codes_completed_ok);
}

/****************************************************************************
 * Name: espcp_queue_message
 *
 * Description:
 *  Add a message to the message queue and wait on the semaphore.
 * 
 * Input Parameters:
 *  message - Pointer to an espcp_message_t object.
 *  block - Wait for a response if true, return immediately if false.
 *
 * Returned Value:
 *  Status code (completed OK for success failure if there is a problem).
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
uint32_t espcp_queue_message(espcp_message_t *message, bool block)
{
    uint32_t result = espcp_status_codes_failure;

    sem_t sem;
    if (block)
    {
        message->semaphore = &sem; // cppcheck-suppress autoVariables
        nxsem_init(message->semaphore, 0, 0);
        nxsem_set_protocol(&sem, SEM_PRIO_NONE);
    }
    else
    {
        message->semaphore = NULL;
    }
    espcp_configuration_t *configuration = espcp_get_configuration();
    if (espcp_add_message_to_queue(&configuration->request_queue, message) == espcp_status_codes_completed_ok)
    {
        result = espcp_status_codes_completed_ok;
        if (block)
        {
            //
            //  Wait for the response, but not for ever.  If the ESP32 stops answering, an unbounded
            //  wait strands every caller: the semaphore is on this stack frame and the message belongs
            //  to the caller, so nothing can ever recover them.
            //
            //  net_sem_timedwait_uninterruptible() waits again if a signal interrupts the wait.  It
            //  returns a negated errno and does not set errno.
            //
            int wait_result = net_sem_timedwait_uninterruptible(message->semaphore, ESPCP_RESPONSE_TIMEOUT_MILLISECONDS);
            if (wait_result == 0)
            {
                g_espcp_response_timeouts = 0;
            }
            else
            {
                if (wait_result == -ETIMEDOUT)
                {
                    result = espcp_queue_message_timed_out(message);
                }
                else
                {
                    result = espcp_status_codes_failure;
                }
            }
        }
    }
    return (result);
}

/****************************************************************************
 * Name: espcp_queue_ethernet_connection_changed_event
 *
 * Description:
 *  Create an event message for a connection change for wired ethernet.
 * 
 * Input Parameters:
 *  connected - true is a connection has been made, false if a connection
 *              is lost.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *  None.
 *
 ****************************************************************************/
void espcp_queue_ethernet_connection_changed_event(bool connected)
{
    espcp_config_lock();
    espcp_configuration_t *config = espcp_get_configuration();
    struct file *queue = &config->incoming_event_queue;
    espcp_config_unlock();

    espcp_message_t *connection_message = (espcp_message_t *) zalloc(sizeof(espcp_message_t));
    connection_message->message_type = espcp_message_types_event;
    connection_message->interface = espcp_esp32_interfaces_wired_ethernet;
    connection_message->semaphore = NULL;
    if (connected)
    {
        connection_message->function = espcp_wi_fi_function_network_connected_event;
        espcp_connect_event_data_t *data = (espcp_connect_event_data_t *) zalloc(sizeof(espcp_connect_event_data_t));
        if (data != NULL)
        {
            bool have_address = false;

            hcom_nx_config_lock();
            meadow_configuration_t *meadow_config = hcom_nx_config_get_pointer();
            if ((meadow_config != NULL) && (meadow_config->default_interface != NULL))
            {
                data->ip_address = meadow_config->default_interface->ip_address;
                data->gateway = meadow_config->default_interface->gateway;
                data->subnet_mask = meadow_config->default_interface->netmask;
                have_address = true;
            }
            hcom_nx_config_unlock();
            if (!have_address)
            {
                free(data);
                free(connection_message);
                return;
            }
            connection_message->payload_length = espcp_connect_event_data_buffer_size(data);
            connection_message->payload = (uint8_t *) zalloc(connection_message->payload_length);
            if (connection_message->payload == NULL)
            {
                free(data);
                free(connection_message);
                return;
            }
            espcp_encode_connect_event_data(data, connection_message->payload);
            free(data);
        }
        else
        {
            free(connection_message);
            return;
        }
    }
    else
    {
        connection_message->function = espcp_wi_fi_function_network_disconnected_event;
    }

    espcp_add_message_to_queue(queue, connection_message);
}

