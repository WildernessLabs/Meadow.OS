/****************************************************************************
 * bluetooth_tests.c
 *
 *   Copyright (C) 2026 Wilderness Labs. All rights reserved.
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
#include <nuttx/config.h>

#if defined(CONFIG_BLUETOOTH_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)

#pragma message "(--) espcp_bluetooth_tests.c included"

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <nuttx/mqueue.h>          // file_mq_* (kernel-side queue API)
#include <time.h>
#include <unistd.h>
#include <syslog.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <meadow/meadow-upd.h>
#include <meadow/meadow_kernel_tests.h>
#include <meadow/meadow_unit_test_framework.h>
#include "../espcp/espcp_encoders.h"
#include "../espcp/espcp_shared_enums.h"

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

#define LOGGING_LEVEL           1

//
//  Delay (us) after sending a command to allow events to propagate.
//
#define BT_DELAY                2000000

//
//  Name of the NuttX message queue that carries ESP32 events.
//
#define BT_EVENT_QUEUE_NAME     "/Esp32Events"

//
//  Encoded size of an espcp_event_data_t on the queue (bytes).
//
#define BT_EVENT_ENCODED_SIZE   22

//
//  Default receive timeout for automated tests (ms).
//
#define BT_TIMEOUT_MS           10000

/****************************************************************************
 * Private variables and associated macros
 ****************************************************************************/

static uint tests_run = 0;
static uint tests_passed = 0;
static uint tests_failed = 0;

#define TEST_PASSED     tests_run++; tests_passed++;
#define TEST_FAILED     tests_run++; tests_failed++;

#define ALLOCATE_HEAP_STRUCTURES \
    struct mallinfo _heap_before, _heap_after; \
    struct mallinfo _kheap_before, _kheap_after;

#define GET_INITIAL_HEAP_INFORMATION \
    _heap_before = mallinfo(); \
    _kheap_before = kmm_mallinfo();

#define GET_FINAL_HEAP_INFORMATION \
    _heap_after = mallinfo(); \
    _kheap_after = kmm_mallinfo();

#define HEAP_USAGE_PASS_OR_FAIL \
    do { \
        int _udiff = _heap_after.uordblks  - _heap_before.uordblks; \
        int _kdiff = _kheap_after.uordblks - _kheap_before.uordblks; \
        if ((_udiff > 0) || (_kdiff > 0)) { \
            syslog(LOGGING_LEVEL, \
                   "    FAIL: Memory not released: user %d, kernel %d\n", \
                   _udiff, _kdiff); \
            TEST_FAILED; \
        } else { \
            syslog(LOGGING_LEVEL, "    PASS: Heap memory OK\n"); \
            TEST_PASSED; \
        } \
    } while (0)

/****************************************************************************
 * Private JSON config strings (ported verbatim from test_bluetooth_service_json.hpp)
 ****************************************************************************/

//
//  Company identifier is for Espressif (0x02e5).
//
static const char *meadow_a_json = R"(
{
    "deviceName":"MeadowA",
    "companyIdentifier": 741,
    "services": [
        {
            "name": "Service A",
            "uuid": 254,
            "characteristics": [
                {
                    "uuid": "017e99d6-8a61-11eb-8dcd-0242ac130003",
                    "permission": 17,
                    "props": 10,
                    "len": 4
                },
                {
                    "uuid": 10755,
                    "permission": 2,
                    "props": 16,
                    "len": 4
                }
            ]
        }
    ]
})";

//
//  Company ID is for Baidu (0x011c).
//
static const char *meadow_b_json = R"(
{
    "deviceName":"MeadowB",
    "companyIdentifier": 284,
    "services": [
        {
            "name": "Service A",
            "uuid": 254,
            "characteristics": [
                {
                    "uuid": "017e99d6-8a61-11eb-8dcd-0242ac130003",
                    "permission": 17,
                    "props": 10,
                    "len": 4
                },
                {
                    "uuid": 10755,
                    "permission": 2,
                    "props": 16,
                    "len": 4
                }
            ]
        }
    ]
})";

//
//  Meadow device with no company identifier.
//
static const char *meadow_c_json = R"(
{
    "deviceName":"MeadowC",
    "services": [
        {
            "name": "Service A",
            "uuid": 254,
            "characteristics": [
                {
                    "uuid": "017e99d6-8a61-11eb-8dcd-0242ac130003",
                    "permission": 17,
                    "props": 26,
                    "len": 4
                },
                {
                    "uuid": 10755,
                    "permission": 2,
                    "props": 16,
                    "len": 4
                }
            ]
        }
    ]
})";

//
//  Company ID is for Baidu (0x011c).
//
static const char *meadow_d_json = R"(
{
    "deviceName":"MeadowD",
    "companyIdentifier": 284,
    "services": [
        {
            "name": "Service A",
            "uuid": 254,
            "characteristics": [
                {
                    "uuid": "017e99d6-8a61-11eb-8dcd-0242ac130003",
                    "permission": 17,
                    "props": 18,
                    "len": 4,
                    "descriptors": [
                        {
                        "uuid": 10498,
                        "permission": 17,
                        "len": 1,
                        "value": 3
                        },
                        {
                        "uuid": 10497,
                        "permission": 1,
                        "len": 20,
                        "value": "Test characteristic"
                        }
                    ]
                },
                {
                    "uuid": 10755,
                    "permission": 2,
                    "props": 16,
                    "len": 4
                }
            ]
        }
    ]
})";

//
//  Meadow device name MeadowF7 with company identifier Espressif (0x02e5).
//
//  This JSON represents the configuration used in the Bluetooth_Basics managed application.
//
static const char *meadow_f7_json = R"({
    "deviceName":"MeadowF7",
    "companyIdentifier":741,
    "services": [
        {
            "name":"GattService",
            "uuid":253,
            "characteristics": [
                {   
                    "uuid":"016e99d6-8a61-11eb-8dcd-0242ac1300ff",
                    "permission":17,
                    "props":10,
                    "len":4
                },
                {
                    "uuid":"017e99d6-8a61-11eb-8dcd-0242ac1300aa",
                    "permission":1,
                    "props":2,
                    "len":4
                },
                {
                    "uuid":"018e99d6-8a61-11eb-8dcd-0242ac1300bb",
                    "permission":17,
                    "props":10,
                    "len":4
                },
                {
                    "uuid":"019e99d6-8a61-11eb-8dcd-0242ac1300cc",
                    "permission":17,
                    "props":10,
                    "len":256
                }
            ]
        }
    ]
})";

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_open_event_queue
 *
 * Description:
 *  Open the ESP32 event message queue for reading.
 *
 *  The queue is held as a struct file and driven through file_mq_*() rather
 *  than as an mqd_t through mq_*(): this is kernel code, and the file_mq_*()
 *  calls return a negated errno instead of writing errno in the current
 *  task's TLS.
 *
 * Input Parameters:
 *  q - struct file to open the queue into.  Zeroed and populated by
 *      file_mq_open().
 *
 * Returned Value:
 *  Zero on success, a negated errno on failure.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
 static int meadow_kt_bluetooth_test_open_event_queue(struct file *q)
{
    int ret = file_mq_open(q, BT_EVENT_QUEUE_NAME, O_RDONLY | O_NONBLOCK);
    if (ret < 0)
    {
        syslog(LOGGING_LEVEL, "    FAIL: Cannot open event queue: %d\n", ret);
    }
    return ret;
}

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_send_command
 *
 * Description:
 *  Build and dispatch a Bluetooth command to the ESP32.
 *
 * Input Parameters:
 *  interface - ESP32 interface (espcp_esp32_interfaces_blue_tooth)
 *  function  - Bluetooth function enum value
 *  payload   - Encoded payload buffer (may be NULL)
 *  length    - Length of payload in bytes
 *
 * Returned Value:
 *  status_code returned by the ESP32 command handler.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
static uint32_t meadow_kt_bluetooth_test_send_command(uint8_t interface, uint32_t function, uint8_t *payload, uint32_t length)
{
    struct upd_esp32_command message;
    memset(&message, 0, sizeof(struct upd_esp32_command));
    message.interface = interface;
    message.function = function;
    message.payload = payload;
    message.payload_length = length;
    message.block = 1;

    upd_handle_esp32_command(&message);

    return message.status_code;
}

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_receive_event
 *
 * Description:
 *  Wait up to timeout_ms milliseconds to receive one event from the ESP32
 *  event queue and decode it into an espcp_event_data_t.
 *
 * Input Parameters:
 *  event_queue - open struct file for the event queue
 *  timeout_ms  - maximum wait in milliseconds
 *  out_event   - set to a heap-allocated espcp_event_data_t on success;
 *                caller must free()
 *
 * Returned Value:
 *  0 on success, -1 on timeout or error.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
static int meadow_kt_bluetooth_test_receive_event(struct file *event_queue, int timeout_ms, espcp_event_data_t **out_event)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long) (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }

    uint8_t encoded[BT_EVENT_ENCODED_SIZE];
    unsigned int priority = 0;
    ssize_t result = file_mq_timedreceive(event_queue, (char *) encoded, sizeof(encoded), &priority, &ts);
    if (result <= 0)
    {
        return -1;
    }

    *out_event = espcp_extract_event_data(encoded);
    return (*out_event != NULL) ? 0 : -1;
}

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_check_event
 *
 * Description:
 *  Verify that a received event matches expected values and record pass/fail.
 *
 * Input Parameters:
 *  event              - decoded event (may be NULL, which counts as FAIL)
 *  expected_interface - expected interface byte
 *  expected_function  - expected function value
 *  expected_status    - expected status_code value
 *  label              - short label for syslog messages
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
static void meadow_kt_bluetooth_test_check_event(espcp_event_data_t *event, uint8_t expected_interface,
                                uint32_t expected_function, uint32_t expected_status,
                                const char *label)
{
    if (event == NULL)
    {
        syslog(LOGGING_LEVEL, "    FAIL: %s - no event received\n", label);
        TEST_FAILED;
        return;
    }

    bool ok = (event->interface == expected_interface) && (event->function == expected_function)
           && (event->status_code == expected_status);

    if (ok)
    {
        syslog(LOGGING_LEVEL, "    PASS: %s\n", label);
        TEST_PASSED;
    }
    else
    {
         syslog(LOGGING_LEVEL, "    FAIL: %s - got interface=%u function=%u status=%u, expected interface=%u function=%u status=%u\n",
               label, (unsigned) event->interface, (unsigned) event->function, (unsigned) event->status_code, (unsigned) expected_interface,
               (unsigned) expected_function, (unsigned) expected_status);
        TEST_FAILED;
    }
}

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_start_service
 *
 * Description:
 *  Send the Bluetooth Start command and verify the expected event sequence:
 *  BluetoothStartingEvent, BluetoothStartedEvent, then a CompletedOk response.
 *
 * Input Parameters:
 *  event_queue - open struct file for the event queue
 *  json_config - JSON configuration string for the BT stack
 *
 * Returned Value:
 *  true if start succeeded (response status was CompletedOk), false otherwise.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
static bool meadow_kt_bluetooth_test_start_service(struct file *event_queue, const char *json_config)
{
    espcp_b_t_stack_config_t stack_config;
    stack_config.config = (char *) json_config;

    uint32_t payload_len = espcp_b_t_stack_config_buffer_size(&stack_config);
    uint8_t *payload = (uint8_t *) malloc(payload_len);
    if (payload == NULL)
    {
        syslog(LOGGING_LEVEL, "    FAIL: meadow_kt_bluetooth_test_start_service - malloc failed\n");
        TEST_FAILED;
        return false;
    }

    espcp_encode_b_t_stack_config(&stack_config, payload);

    uint32_t status = meadow_kt_bluetooth_test_send_command(espcp_esp32_interfaces_blue_tooth,
                                           espcp_bluetooth_function_start,
                                           payload,
                                           payload_len);
    free(payload);

    espcp_event_data_t *event = NULL;

    meadow_kt_bluetooth_test_receive_event(event_queue, BT_TIMEOUT_MS, &event);
    meadow_kt_bluetooth_test_check_event(event, espcp_esp32_interfaces_blue_tooth,
                        espcp_bluetooth_starting_event, 
                        espcp_status_codes_completed_ok,
                        "BluetoothStartingEvent");
    free(event);
    event = NULL;

    meadow_kt_bluetooth_test_receive_event(event_queue, BT_TIMEOUT_MS, &event);
    meadow_kt_bluetooth_test_check_event(event,
                        espcp_esp32_interfaces_blue_tooth,
                        espcp_bluetooth_started_event,
                        espcp_status_codes_completed_ok,
                        "BluetoothStartedEvent");
    free(event);

    if (status == espcp_status_codes_completed_ok)
    {
        syslog(LOGGING_LEVEL, "    PASS: Bluetooth Start response\n");
        TEST_PASSED;
        return true;
    }
    else
    {
        syslog(LOGGING_LEVEL, "    FAIL: Bluetooth Start response - status %u\n", (unsigned) status);
        TEST_FAILED;
        return false;
    }
}

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_stop_bluetooth
 *
 * Description:
 *  Send the Bluetooth Stop command and verify the expected event sequence:
 *  BluetoothStoppingEvent, BluetoothStoppedEvent, then a CompletedOk response.
 *
 * Input Parameters:
 *  event_queue - open struct file for the event queue
 *
 * Returned Value:
 *  true if stop succeeded (response status was CompletedOk), false otherwise.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
static bool meadow_kt_bluetooth_test_stop_bluetooth(struct file *event_queue)
{
    uint32_t status = meadow_kt_bluetooth_test_send_command(espcp_esp32_interfaces_blue_tooth,
                                           espcp_bluetooth_function_stop,
                                           NULL,
                                           0);

    espcp_event_data_t *event = NULL;

    meadow_kt_bluetooth_test_receive_event(event_queue, BT_TIMEOUT_MS, &event);
    meadow_kt_bluetooth_test_check_event(event, espcp_esp32_interfaces_blue_tooth, espcp_bluetooth_stopping_event,
                        espcp_status_codes_completed_ok, "BluetoothStoppingEvent");
    free(event);
    event = NULL;

    meadow_kt_bluetooth_test_receive_event(event_queue, BT_TIMEOUT_MS, &event);
    meadow_kt_bluetooth_test_check_event(event,
                        espcp_esp32_interfaces_blue_tooth,
                        espcp_bluetooth_stopped_event,
                        espcp_status_codes_completed_ok,
                        "BluetoothStoppedEvent");
    free(event);

    if (status == espcp_status_codes_completed_ok)
    {
        syslog(LOGGING_LEVEL, "    PASS: Bluetooth Stop response\n");
        TEST_PASSED;
        return true;
    }
    else
    {
        syslog(LOGGING_LEVEL, "    FAIL: Bluetooth Stop response - status %u\n", (unsigned) status);
        TEST_FAILED;
        return false;
    }
}

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_start_service_a
 *
 * Description:
 *  Start BT service with meadow_a_json (Espressif company ID).
 *
 * Input Parameters:
 *  value - unused parameter.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
void meadow_kt_bluetooth_test_start_service_a(uint32_t value)
{
    syslog(LOGGING_LEVEL, "********** BT: start meadow_a (Espressif company ID)\n");

    struct file eq;
    int eq_result = meadow_kt_bluetooth_test_open_event_queue(&eq);
    DEBUGASSERT(eq_result >= 0);

    ALLOCATE_HEAP_STRUCTURES;
    GET_INITIAL_HEAP_INFORMATION;

    meadow_kt_bluetooth_test_start_service(&eq, meadow_a_json);

    usleep(BT_DELAY);

    GET_FINAL_HEAP_INFORMATION;
    HEAP_USAGE_PASS_OR_FAIL;

    file_mq_close(&eq);
}

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_start_service_b
 *
 * Description:
 *  Start BT service with meadow_b_json (Baidu company ID).
 *
 * Input Parameters:
 *  value - unused parameter.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
void meadow_kt_bluetooth_test_start_service_b(uint32_t value)
{
    syslog(LOGGING_LEVEL, "********** BT: start meadow_b (Baidu company ID)\n");

    struct file eq;
    int eq_result = meadow_kt_bluetooth_test_open_event_queue(&eq);
    DEBUGASSERT(eq_result >= 0);

    ALLOCATE_HEAP_STRUCTURES;
    GET_INITIAL_HEAP_INFORMATION;

    meadow_kt_bluetooth_test_start_service(&eq, meadow_b_json);

    usleep(BT_DELAY);

    GET_FINAL_HEAP_INFORMATION;
    HEAP_USAGE_PASS_OR_FAIL;

    file_mq_close(&eq);
}

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_start_service_c
 *
 * Description:
 *  Start BT service with meadow_c_json (No company ID).
 *
 * Input Parameters:
 *  value - unused parameter.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
void meadow_kt_bluetooth_test_start_service_c(uint32_t value)
{
    syslog(LOGGING_LEVEL, "********** BT: start meadow_c (No company ID)\n");

    struct file eq;
    int eq_result = meadow_kt_bluetooth_test_open_event_queue(&eq);
    DEBUGASSERT(eq_result >= 0);

    ALLOCATE_HEAP_STRUCTURES;
    GET_INITIAL_HEAP_INFORMATION;

    meadow_kt_bluetooth_test_start_service(&eq, meadow_c_json);

    usleep(BT_DELAY);

    GET_FINAL_HEAP_INFORMATION;
    HEAP_USAGE_PASS_OR_FAIL;

    file_mq_close(&eq);
}

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_start_service_d
 *
 * Description:
 *  Start BT service with meadow_d_json (Baidu company ID).
 *
 * Input Parameters:
 *  value - unused parameter.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
void meadow_kt_bluetooth_test_start_service_d(uint32_t value)
{
    syslog(LOGGING_LEVEL, "********** BT: start meadow_d (Baidu company ID)\n");

    struct file eq;
    int eq_result = meadow_kt_bluetooth_test_open_event_queue(&eq);
    DEBUGASSERT(eq_result >= 0);

    ALLOCATE_HEAP_STRUCTURES;
    GET_INITIAL_HEAP_INFORMATION;

    meadow_kt_bluetooth_test_start_service(&eq, meadow_d_json);

    usleep(BT_DELAY);

    GET_FINAL_HEAP_INFORMATION;
    HEAP_USAGE_PASS_OR_FAIL;

    file_mq_close(&eq);
}

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_start_service_meadow_f7
 *
 * Description:
 *  Start BT service with meadow_f7_json (Bluetooth_Basics app config).
 *
 * Input Parameters:
 *  value - unused parameter.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
void meadow_kt_bluetooth_test_start_service_meadow_f7(uint32_t value)
{
    syslog(LOGGING_LEVEL, "********** BT: start meadow_f7 (Bluetooth_Basics config)\n");

    struct file eq;
    int eq_result = meadow_kt_bluetooth_test_open_event_queue(&eq);
    DEBUGASSERT(eq_result >= 0);

    ALLOCATE_HEAP_STRUCTURES;
    GET_INITIAL_HEAP_INFORMATION;

    meadow_kt_bluetooth_test_start_service(&eq, meadow_f7_json);

    usleep(BT_DELAY);

    GET_FINAL_HEAP_INFORMATION;
    HEAP_USAGE_PASS_OR_FAIL;

    file_mq_close(&eq);
}

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_stop_service
 *
 * Description:
 *  Start BT service with meadow_a_json (Espressif company ID).
 *
 * Input Parameters:
 *  value - unused parameter.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
void meadow_kt_bluetooth_test_stop_service(uint32_t value)
{
    syslog(LOGGING_LEVEL, "********** BT: stop Bluetooth service\n");

    struct file eq;
    int eq_result = meadow_kt_bluetooth_test_open_event_queue(&eq);
    DEBUGASSERT(eq_result >= 0);

    ALLOCATE_HEAP_STRUCTURES;
    GET_INITIAL_HEAP_INFORMATION;

    meadow_kt_bluetooth_test_stop_bluetooth(&eq);

    usleep(BT_DELAY);

    GET_FINAL_HEAP_INFORMATION;
    HEAP_USAGE_PASS_OR_FAIL;

    file_mq_close(&eq);
}

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_stop_not_started
 *
 * Description:
 *  Stop without a prior Start; verify a failure status in the response.
 *
 * Input Parameters:
 *  None.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
void meadow_kt_bluetooth_test_stop_not_started(void)
{
    syslog(LOGGING_LEVEL, "********** BT: stop without prior start\n");

    ALLOCATE_HEAP_STRUCTURES;
    GET_INITIAL_HEAP_INFORMATION;

    uint32_t status = meadow_kt_bluetooth_test_send_command(espcp_esp32_interfaces_blue_tooth,
                                           espcp_bluetooth_function_stop,
                                           NULL,
                                           0);

    if (status != espcp_status_codes_completed_ok)
    {
        syslog(LOGGING_LEVEL,
               "    PASS: Stop-not-started returned failure status %u\n",
               (unsigned) status);
        TEST_PASSED;
    }
    else
    {
        syslog(LOGGING_LEVEL, "    FAIL: Stop-not-started returned CompletedOk\n");
        TEST_FAILED;
    }

    usleep(BT_DELAY);

    GET_FINAL_HEAP_INFORMATION;
    HEAP_USAGE_PASS_OR_FAIL;
}

/****************************************************************************
 * Name: meadow_kt_bluetooth_start_stop
 *
 * Description:
 *  Start with meadow_a_json, delay, then stop; verify all events.
 *
 * Input Parameters:
 *  None.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
void meadow_kt_bluetooth_start_stop(void)
{
    syslog(LOGGING_LEVEL, "********** BT: start/stop\n");

    struct file eq;
    int eq_result = meadow_kt_bluetooth_test_open_event_queue(&eq);
    DEBUGASSERT(eq_result >= 0);

    ALLOCATE_HEAP_STRUCTURES;
    GET_INITIAL_HEAP_INFORMATION;

    if (meadow_kt_bluetooth_test_start_service(&eq, meadow_a_json))
    {
        usleep(BT_DELAY);
        meadow_kt_bluetooth_test_stop_bluetooth(&eq);
    }

    usleep(BT_DELAY);

    GET_FINAL_HEAP_INFORMATION;
    HEAP_USAGE_PASS_OR_FAIL;

    file_mq_close(&eq);
}

/****************************************************************************
 * Name: meadow_kt_bluetooth_restart_different_config
 *
 * Description:
 *  Start meadow_a_json, stop, then start meadow_b_json.
 *
 * Input Parameters:
 *  None.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
// static void meadow_kt_bluetooth_test_restart_different_config(void)
// {
//     syslog(LOGGING_LEVEL, "********** BT: restart with different config\n");

//     struct file eq;
//     int eq_result = meadow_kt_bluetooth_test_open_event_queue(&eq);
//     DEBUGASSERT(eq_result >= 0);

//     ALLOCATE_HEAP_STRUCTURES;
//     GET_INITIAL_HEAP_INFORMATION;

//     if (meadow_kt_bluetooth_test_start_service(&eq, meadow_a_json))
//     {
//         usleep(BT_DELAY);
//         meadow_kt_bluetooth_test_stop_bluetooth(&eq);
//         usleep(BT_DELAY);
//         meadow_kt_bluetooth_test_start_service(&eq, meadow_b_json);
//     }

//     usleep(BT_DELAY);

//     GET_FINAL_HEAP_INFORMATION;
//     HEAP_USAGE_PASS_OR_FAIL;

//     file_mq_close(&eq);
// }

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_restart_50_times
 *
 * Description:
 *  Stress test: start meadow_a_json / stop / start meadow_b_json / stop x 50.
 *
 * Input Parameters:
 *  None.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *   None
 *
 ****************************************************************************/
// static void meadow_kt_bluetooth_test_restart_50_times(void)
// {
//     syslog(LOGGING_LEVEL, "********** BT: restart 50 times\n");

//     struct file eq;
//     int eq_result = meadow_kt_bluetooth_test_open_event_queue(&eq);
//     DEBUGASSERT(eq_result >= 0);

//     ALLOCATE_HEAP_STRUCTURES;
//     GET_INITIAL_HEAP_INFORMATION;

//     for (int i = 0; i < 50; i++)
//     {
//         syslog(LOGGING_LEVEL, "    Iteration %d/50\n", i + 1);
//         if (!meadow_kt_bluetooth_test_start_service(&eq, meadow_a_json))
//         {
//             break;
//         }
//         usleep(500000);
//         meadow_kt_bluetooth_stop_service(eq);
//         usleep(500000);

//         if (!meadow_kt_bluetooth_test_start_service(&eq, meadow_b_json))
//         {
//             break;
//         }
//         usleep(500000);
//         meadow_kt_bluetooth_stop_service(eq);
//         usleep(500000);
//     }

//     GET_FINAL_HEAP_INFORMATION;
//     HEAP_USAGE_PASS_OR_FAIL;

//     file_mq_close(&eq);
// }

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_notify_characteristic
 *
 * Description:
 *  Interactive test: start meadow_c_json, wait for a client to connect, send
 *  a ServerDataSet, verify the response.  Requires a BT client device.
 *
 * Input Parameters:
 *  None.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *   Requires a Bluetooth client device to connect for the test to proceed.
 *
 ****************************************************************************/
// static void meadow_kt_bluetooth_test_notify_characteristic(void)
// {
//     syslog(LOGGING_LEVEL, "********** BT: notify characteristic (interactive)\n");

//     struct file eq;
//     int eq_result = meadow_kt_bluetooth_test_open_event_queue(&eq);
//     DEBUGASSERT(eq_result >= 0);

//     ALLOCATE_HEAP_STRUCTURES;
//     GET_INITIAL_HEAP_INFORMATION;

//     if (!meadow_kt_bluetooth_test_start_service(&eq, meadow_c_json))
//     {
//         goto done;
//     }

//     syslog(LOGGING_LEVEL, "    Waiting for client to connect...\n");

//     espcp_event_data_t *event = NULL;
//     int rc = meadow_kt_bluetooth_test_receive_event(&eq, 60000, &event);
//     if (rc != 0)
//     {
//         syslog(LOGGING_LEVEL, "    FAIL: Timed out waiting for client connection\n");
//         TEST_FAILED;
//         meadow_kt_bluetooth_test_stop_bluetooth(&eq);
//         goto done;
//     }

//     meadow_kt_bluetooth_test_check_event(event,
//                         espcp_esp32_interfaces_blue_tooth,
//                         espcp_client_connected_event,
//                         espcp_status_codes_completed_ok,
//                         "ClientConnectedEvent");
//     free(event);
//     event = NULL;

//     syslog(LOGGING_LEVEL, "    Client connected - sending ServerDataSet\n");

//     uint32_t value = 0xa0a0a0a0;
//     espcp_b_t_server_data_set_t data_set;
//     data_set.handle = 0x2a;
//     data_set.set_data = (uint8_t *) &value;
//     data_set.set_data_length = sizeof(value);

//     uint32_t pl_len = espcp_b_t_server_data_set_buffer_size(&data_set);
//     uint8_t *payload = (uint8_t *) malloc(pl_len);
//     if (payload == NULL)
//     {
//         syslog(LOGGING_LEVEL, "    FAIL: malloc for ServerDataSet payload\n");
//         TEST_FAILED;
//         meadow_kt_bluetooth_test_stop_bluetooth(&eq);
//         goto done;
//     }

//     espcp_encode_b_t_server_data_set(&data_set, payload);

//     uint32_t status = meadow_kt_bluetooth_test_send_command(meadow_kt_bluetooth_test_interfaces_blue_tooth,
//                                            espcp_bluetooth_function_server_data_set, payload, pl_len);
//     free(payload);

//     if (status == espcp_status_codes_completed_ok)
//     {
//         syslog(LOGGING_LEVEL, "    PASS: ServerDataSet response\n");
//         TEST_PASSED;
//     }
//     else
//     {
//         syslog(LOGGING_LEVEL, "    FAIL: ServerDataSet response - status %u\n", (unsigned) status);
//         TEST_FAILED;
//     }

//     meadow_kt_bluetooth_stop_service(eq);

// done:
//     GET_FINAL_HEAP_INFORMATION;
//     HEAP_USAGE_PASS_OR_FAIL;
//     file_mq_close(&eq);
// }

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_notify_with_descriptors
 *
 * Description:
 *  Interactive test: same as meadow_kt_bluetooth_test_notify_characteristic but with
 *  meadow_d_json (characteristic includes a descriptor).
 *
 * Input Parameters:
 *  None.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *   Requires a Bluetooth client device to connect for the test to proceed.
 *
 ****************************************************************************/
// static void meadow_kt_bluetooth_test_notify_with_descriptors(void)
// {
//     syslog(LOGGING_LEVEL, "********** BT: notify with descriptors (interactive)\n");

//     struct file eq;
//     int eq_result = meadow_kt_bluetooth_test_open_event_queue(&eq);
//     DEBUGASSERT(eq_result >= 0);

//     ALLOCATE_HEAP_STRUCTURES;
//     GET_INITIAL_HEAP_INFORMATION;

//     if (!meadow_kt_bluetooth_test_start_service(&eq, meadow_d_json))
//     {
//         goto done;
//     }

//     syslog(LOGGING_LEVEL, "    Waiting for client to connect...\n");

//     espcp_event_data_t *event = NULL;
//     int rc = meadow_kt_bluetooth_test_receive_event(&eq, 60000, &event);
//     if (rc != 0)
//     {
//         syslog(LOGGING_LEVEL, "    FAIL: Timed out waiting for client connection\n");
//         TEST_FAILED;
//         meadow_kt_bluetooth_test_stop_bluetooth(&eq);
//         goto done;
//     }

//     meadow_kt_bluetooth_test_check_event(event, espcp_esp32_interfaces_blue_tooth, espcp_client_connected_event,
//                         espcp_status_codes_completed_ok, "ClientConnectedEvent");
//     free(event);
//     event = NULL;

//     syslog(LOGGING_LEVEL, "    Client connected - sending ServerDataSet\n");

//     uint32_t value = 0xa0a0a0a0;
//     espcp_b_t_server_data_set_t data_set;
//     data_set.handle = 0x2a;
//     data_set.set_data = (uint8_t *) &value;
//     data_set.set_data_length = sizeof(value);

//     uint32_t pl_len = espcp_b_t_server_data_set_buffer_size(&data_set);
//     uint8_t *payload = (uint8_t *) malloc(pl_len);
//     if (payload == NULL)
//     {
//         syslog(LOGGING_LEVEL, "    FAIL: malloc for ServerDataSet payload\n");
//         TEST_FAILED;
//         meadow_kt_bluetooth_test_stop_bluetooth(&eq);
//         goto done;
//     }

//     espcp_encode_b_t_server_data_set(&data_set, payload);

//     uint32_t status = meadow_kt_bluetooth_test_send_command(espcp_esp32_interfaces_blue_tooth, espcp_bluetooth_function_server_data_set,
//                                            payload, pl_len);
//     free(payload);

//     if (status == espcp_status_codes_completed_ok)
//     {
//         syslog(LOGGING_LEVEL, "    PASS: ServerDataSet response\n");
//         TEST_PASSED;
//     }
//     else
//     {
//         syslog(LOGGING_LEVEL, "    FAIL: ServerDataSet response - status %u\n", (unsigned) status);
//         TEST_FAILED;
//     }

//     meadow_kt_bluetooth_test_stop_bluetooth(&eq);

// done:
//     GET_FINAL_HEAP_INFORMATION;
//     HEAP_USAGE_PASS_OR_FAIL;
//     file_mq_close(&eq);
// }

/****************************************************************************
 * Name: meadow_kt_bluetooth_test_soak_write_characteristic
 *
 * Description:
 *  Interactive soak test: start meadow_f7_json, retrieve handles, then loop
 *  receiving ClientWriteRequestEvents and echoing the data back via
 *  ServerDataSet until the client sends a zero-length payload.
 *
 * Input Parameters:
 *  None.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *   Requires a Bluetooth client device connected and writing data for the test.
 *
 ****************************************************************************/
// static void meadow_kt_bluetooth_test_soak_write_characteristic(void)
// {
//     syslog(LOGGING_LEVEL, "********** BT: soak write characteristic (interactive)\n");

//     struct file eq;
//     int eq_result = meadow_kt_bluetooth_test_open_event_queue(&eq);
//     DEBUGASSERT(eq_result >= 0);

//     ALLOCATE_HEAP_STRUCTURES;
//     GET_INITIAL_HEAP_INFORMATION;

//     if (!meadow_kt_bluetooth_test_start_service(&eq, meadow_f7_json))
//     {
//         goto done;
//     }

//     /* Retrieve service/characteristic handles. */
//     uint32_t status = meadow_kt_bluetooth_test_send_command(espcp_esp32_interfaces_blue_tooth,
//                                            espcp_bluetooth_function_get_handles,
//                                            NULL,
//                                            0);
//     if (status != espcp_status_codes_completed_ok)
//     {
//         syslog(LOGGING_LEVEL, "    FAIL: GetHandles - status %u\n", (unsigned) status);
//         TEST_FAILED;
//         meadow_kt_bluetooth_test_stop_bluetooth(&eq);
//         goto done;
//     }

//     syslog(LOGGING_LEVEL, "    Waiting for client writes (send 0 bytes to stop)...\n");

//     bool running = true;
//     while (running)
//     {
//         espcp_event_data_t *event = NULL;
//         if (meadow_kt_bluetooth_test_receive_event(&eq, 60000, &event) != 0)
//         {
//             syslog(LOGGING_LEVEL, "    Soak: timed out waiting for event\n");
//             break;
//         }

//         if ((event->interface != espcp_esp32_interfaces_blue_tooth)
//          || (event->function != espcp_bluetooth_function_client_write_request_event))
//         {
//             free(event);
//             continue;
//         }

//         espcp_event_data_payload_t req;
//         memset(&req, 0, sizeof(espcp_event_data_payload_t));
//         req.message_id = event->message_id;

//         const uint32_t buf_len = 4000;
//         req.payload_length = buf_len;
//         if (event->message_id != 0)
//         {
//             req.payload = (uint8_t *) malloc(buf_len);
//             upd_handle_esp32_get_event_result(&req);
//         }

//         free(event);

//         if (req.payload_length == 0 || req.payload == NULL)
//         {
//             syslog(LOGGING_LEVEL, "    Soak: client sent 0 bytes - stopping\n");
//             if (req.payload != NULL)
//             {
//                 free(req.payload);
//             }
//             running = false;
//             break;
//         }

//         espcp_b_t_data_write_request_t *write_req =
//             espcp_extract_b_t_data_write_request(req.payload);
//         free(req.payload);

//         if (write_req == NULL)
//         {
//             syslog(LOGGING_LEVEL, "    Soak: failed to decode ClientWriteRequest\n");
//             break;
//         }

//         syslog(LOGGING_LEVEL, "    Soak: echoing %u bytes back on handle 0x%04x\n",
//                (unsigned) write_req->data_length, (unsigned) write_req->handle);

//         espcp_b_t_server_data_set_t echo;
//         echo.handle = write_req->handle;
//         echo.set_data = write_req->data;
//         echo.set_data_length = write_req->data_length;

//         uint32_t echo_len = espcp_b_t_server_data_set_buffer_size(&echo);
//         uint8_t *echo_buf = (uint8_t *) malloc(echo_len);
//         if (echo_buf != NULL)
//         {
//             espcp_encode_b_t_server_data_set(&echo, echo_buf);
//             meadow_kt_bluetooth_test_send_command(espcp_esp32_interfaces_blue_tooth,
//                                  espcp_bluetooth_function_server_data_set,
//                                  echo_buf,
//                                  echo_len);
//             free(echo_buf);
//         }

//         if (write_req->data != NULL)
//         {
//             free(write_req->data);
//         }
//         free(write_req);
//     }

//     meadow_kt_bluetooth_test_stop_bluetooth(&eq);

// done:
//     GET_FINAL_HEAP_INFORMATION;
//     HEAP_USAGE_PASS_OR_FAIL;
//     file_mq_close(&eq);
// }

#endif  /* defined(CONFIG_BLUETOOTH_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS) */
