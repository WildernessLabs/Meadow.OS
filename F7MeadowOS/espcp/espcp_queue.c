/****************************************************************************
 * espcp_queue.c
 *
 *   Copyright (C) 2020 Wilderness Labs. All rights reserved.
 *   Author: Mark Stevens
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>
#include <fcntl.h>

#include <arch/irq.h>

#include <sys/socket.h>
#include <nuttx/semaphore.h>
#include <nuttx/net/net.h>
#include <nuttx/net/usrsock.h>
#include <nuttx/mqueue.h>
#include <nuttx/config.h>

#include "espcp_coprocessor.h"
#include "espcp_queue.h"
#include "espcp_message.h"
#include "espcp_shared_enums.h"
#include "espcp_coprocessor.h"
#include "espcp_event_handlers.h"

/****************************************************************************
 * Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: espcp_create_message_queue
 *
 * Description:
 *  Create a named message queue to hold the messages that should be
 *  sent to the ESP32 for processing.
 *
 * Input Parameters:
 *  name - name of the message queue to be created.
 *
 * Returned Value:
 *  ID of the message queue if the queue was created successfully
 *  -1 or an error code if the queue creation failed.
 *
 * Assumptions/Limitations:
 *  Message queue does not already exist.
 *
 ****************************************************************************/
bool espcp_create_message_queues(espcp_configuration_t *config)
{
    int request_queue_result;
    int event_queue_result;
    int incoming_event_queue_result;

    struct mq_attr queue_attributes;
    memset(&queue_attributes, 0, sizeof(queue_attributes));
    mode_t mode;
    memset(&mode, 0, sizeof(mode));

    //
    //  file_mq_open() is used rather than mq_open() because the handle has to
    //  be usable from any context - see the queue members in
    //  espcp_coprocessor.h.  It clears the struct file before use, so the
    //  members need no initialisation here.
    //
    espcp_config_lock();

    queue_attributes.mq_maxmsg = ESPCP_MAXIMUM_MESSAGE_QUEUE_LENGTH;
    queue_attributes.mq_msgsize = sizeof(struct message_and_semaphore_s *);
    queue_attributes.mq_flags = 0;
    request_queue_result = file_mq_open(&config->request_queue, ESPCP_REQUEST_MESSAGE_QUEUE_NAME, O_RDWR | O_CREAT, mode, &queue_attributes);

    queue_attributes.mq_maxmsg = ESPCP_MAXIMUM_MESSAGE_QUEUE_LENGTH;
    queue_attributes.mq_msgsize = sizeof(espcp_event_data_t);
    queue_attributes.mq_flags = 0;
    event_queue_result = file_mq_open(&config->managed_event_queue, ESPCP_EVENT_MESSAGE_QUEUE_NAME, O_RDWR | O_CREAT, mode, &queue_attributes);

    queue_attributes.mq_maxmsg = ESPCP_MAXIMUM_MESSAGE_QUEUE_LENGTH;
    queue_attributes.mq_msgsize = sizeof(espcp_message_t *);
    queue_attributes.mq_flags = 0;
    incoming_event_queue_result = file_mq_open(&config->incoming_event_queue, ESPCP_EVENT_HANDLER_MESSAGE_QUEUE_NAME, O_RDWR | O_CREAT, mode, &queue_attributes);

    espcp_config_unlock();

    if (incoming_event_queue_result < 0)
    {
        syslog(LOG_ERR, "%s@%d-file_mq_open(%s) failed:%d\n", __FILE__, __LINE__, ESPCP_EVENT_HANDLER_MESSAGE_QUEUE_NAME, incoming_event_queue_result);
    }

    return ((request_queue_result == OK) && (event_queue_result == OK));
}

/****************************************************************************
 * Name: espcp_delete_message_queue
 *
 * Description:
 *  Close the message queue and mark it for deletion (unlink the queue).
 * 
 *  Note that the mq_unlink method will only delete the named queue if it is
 *  not opened by another thread / process.  If it is still opened elsewhere
 *  then the it is marked for deletion later (when the final close for this
 *  queue is executed).
 *
 * Input Parameters:
 *   queue_id - ID of the queue to be closed and unlinked.
 *
 * Returned Value:
 *  OK if the queue deletion succeeded.
 *  -1 if an error occurred.
 *
 * Assumptions/Limitations:
 *  None.
 *
 ****************************************************************************/
int espcp_delete_message_queue(struct file *queue)
{
    if (file_mq_close(queue) < 0)
    {
        return (-1);
    }
    return (file_mq_unlink(ESPCP_REQUEST_MESSAGE_QUEUE_NAME));
}

/****************************************************************************
 * Name: espcp_add_message_to_queue
 *
 * Description:
 *  Add a new outbound message to the queue of messages waiting to be sent
 *  to the ESP32.
 *
 * Input Parameters:
 *  queue_id - Message queue ID.
 *  message - Message to be processed.
 *
 * Returned Value:
 *  OK if the message could be added to the queue.
 *  Error code if an error occurred.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
int espcp_add_message_to_queue(struct file *queue, espcp_message_t *message)
{
    int result = OK;

    //
    //  file_mq_send() returns a negated errno and does not touch the errno of
    //  the calling thread, so callers must test the return value.
    //
    result = file_mq_send(queue, (const char *) &message, sizeof(message), ESPCP_DEFAULT_MESSAGE_PRIORITY);

    return (result);
}

/****************************************************************************
 * Name: espcp_queue_add_nonblocking_message
 *
 * Description:
 *  Create an instance of a message on the heap.
 * 
 * Input Parameters:
 *  message_type - Type of the message.
 *  interface - Interface the message is destined for.
 *  function - Function on the interface to be executed.
 *  payload - Binary payload for the message.
 *  payload_length - Size of the binary data (payload)
 *
 * Returned Value:
 *  OK if the message was queued, ERROR if there was a problem.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
int espcp_queue_add_nonblocking_message(uint8_t message_type, uint8_t interface, uint32_t function, uint8_t *payload, uint32_t payload_length)
{
    int result = ERROR;

    espcp_message_t *message = espcp_create_message_on_heap(message_type, interface, function, espcp_status_codes_completed_ok,
                                                            espcp_get_next_message_id(), payload, payload_length);
    if (message != NULL)
    {
        if (espcp_queue_message(message, false) == espcp_status_codes_completed_ok)
        {
            result = OK;
        }
    }
    //
    //  We are adding a non-blocking message to the queue and the queue manager
    //  will free the message and payload when it has been processed.
    //
    return(result);
}

/****************************************************************************
 * Name: espcp_get_message_from_queue
 *
 * Description:
 *  Get a message from the specified message queue.
 *
 * Input Parameters:
 *  queue_id - Message queue ID.
 *
 * Returned Value:
 *  Pointer to the message that has been retrieved, NULL if there was a 
 *  problem.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
void *espcp_get_message_from_queue(struct file *queue)
{
    void *message = NULL;

    ssize_t number_of_bytes = file_mq_receive(queue, (char *) &message, sizeof(message), NULL);
    if (number_of_bytes != sizeof(message))
    {
        message = NULL;
    }

    return message;
}

/****************************************************************************
 * Name: espcp_queue_kill_nuttx_thread_message
 *
 * Description:
 *  Add the "Kill NUTTX Process Thread" message to the message queue.
 * 
 *  This message will eventually cause the NUTTX thread that is processing
 *  the message queue to exit.
 * 
 *  Calling this method will also close the connection to the message
 *  queue.
 * 
 * Input Parameters:
 *  queue_id - Message queue to add the kill message to.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
void espcp_queue_kill_nuttx_thread_message(struct file *queue)
{
    espcp_message_t *kill_thread_message = (espcp_message_t *) zalloc(sizeof(espcp_message_t));
    kill_thread_message->message_type = espcp_message_types_transport;
    kill_thread_message->interface = espcp_esp32_interfaces_transport;
    kill_thread_message->function = espcp_transport_function_kill_nuttx_thread;
    kill_thread_message->semaphore = NULL;

    espcp_add_message_to_queue(queue, kill_thread_message);
}
