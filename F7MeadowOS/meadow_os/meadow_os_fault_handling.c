/****************************************************************************
 * meadow_os_fault_handling.c
 *
 *   Copyright (C) 2024 Wilderness Labs. All rights reserved.
 *   Author:  Wilderness Labs (Mark Stevens)
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

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <nuttx/kstring.h>

#include <meadow/hcom_shared_common.h>
#include <meadow/hcom_bbreg_defn.h>
#include <meadow/meadow_os_fault_handling.h>
#include <meadow/meadow_os_battery_backed_domain.h>
#include "../hcom_nx/hcom_nx_common.h"

/****************************************************************************
 * Uncomment the #define below to turn on debug help macros.
 ****************************************************************************/
// #define USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/**
 * @brief Number of bytes to reserve for the fault message.
 */
#define FAULT_BUFFER_LENGTH         256

/**
 * @brief Name and location of the OS crash report file.
 */
#define MEADOW_OS_CRASH_FILE_NAME   CRASH_DIR "/oscrash_report.txt"

/****************************************************************************
 * Local type definitions.
 ****************************************************************************/

/****************************************************************************
 * External data
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/**
 * @brief Status code from the last reset.  This will be read from the
 *        HCOM_NX_MEADOW_RESET_SOURCE_INFO_BBR_NUM register.
 */
static uint32_t _fault_status = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: find_line_start
 *
 * Description:
 *  Find the start of a line in the buffer moving back from the start until
 *  then start of the line is found or the endstop is reached.
 * 
 * Input Parameters:
 *  start - point in the buffer from which to search backwards.
 *  endstop - point where we will always stop even if we have not found the
 *            start of the line.
 *
 * Returned Value:
 *  Pointer to the first character of the line containing start, or endstop
 *  if the start of the line was not found before reaching it.
 *
 * Assumptions/Limitations:
 *  None.
 *
 ****************************************************************************/
static char *find_line_start(const char *start, const char *endstop)
{
    while ((start > endstop) && ((*start != '\n') && (*start != '\r')))
    {
        start--;
    }
    if ((*start == '\n') || (*start == '\r'))
    {
        start++;
    }
    return((char *) start);
}

/****************************************************************************
 * Name: find_line_end
 *
 * Description:
 *  Find the end of line (or the end of the buffer).
 * 
 * Input Parameters:
 *  buffer - Line of text to search.
 *
 * Returned Value:
 *  Pointer to the terminating character for this line.
 *
 * Assumptions/Limitations:
 *  None.
 *
 ****************************************************************************/
static char *find_line_end(const char *buffer)
{
    while ((*buffer != '\0') && ((*buffer != '\n') && (*buffer != '\r')))
    {
        buffer++;
    }
    return((char *) buffer);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_os_fault_logging_phase_string
 *
 * Description:
 *  Create a string representation of the fault logging phases and if they
 *  have started and completed.
 *
 * Input Parameters:
 *  fault - The fault status.
 *  buffer - Pointer to a buffer to store the string.
 *  length - The length of the buffer.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *  None.
 *
 ****************************************************************************/
static void meadow_os_fault_logging_phase_string(uint8_t fault, char *buffer, uint32_t length)
{
    snprintf(buffer, length, "BKPSRAM %s (%s), Persistent storage %s (%s)",
        (fault & FAULT_LOGGING_PHASE1_STARTED) ? "started" : "not started",
        (fault & FAULT_LOGGING_PHASE1_COMPLETED) ? "completed" : "incomplete",
        (fault & FAULT_LOGGING_PHASE2_STARTED) ? "started" : "not started",
        (fault & FAULT_LOGGING_PHASE2_COMPLETED) ? "completed" : "incomplete");
}

/****************************************************************************
 * Name: meadow_os_fault_handler_process_os_fault_message
 *
 * Description:
 *  Decode the OS fault status and send the results to syslog.
 *
 * Input Parameters:
 *  fault - The fault status.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *  - The fault has already been identified as an OS issue.
 *  - BKPSRAM will be cleared by the caller.
 *
 ****************************************************************************/
static void meadow_os_fault_handler_process_os_fault_message(uint8_t fault)
{
    char *message;

    message = (char *) malloc(FAULT_BUFFER_LENGTH);
    if (message == NULL)
    {
        MEADOW_TRACE_INFORMATION("OS Fault: Unable to allocate memory for fault message.\n");
    }
    else
    {
        meadow_os_fault_logging_phase_string(fault, message, FAULT_BUFFER_LENGTH);
        MEADOW_TRACE_INFORMATION("OS Fault: %s\n", message);
        free(message);
    }
    char *fault_string = (char *) malloc(MEADOW_OS_BBD_SRAM_SIZE);
    if (fault_string == NULL)
    {
        MEADOW_TRACE_INFORMATION("OS Fault: Unable to allocate memory for fault message.\n");
    }
    else
    {
        mkdir(CRASH_DIR, 0777);
        FILE *crash_file = fopen(MEADOW_OS_CRASH_FILE_NAME, "w");

        meadow_os_bbd_strdup_from_sram(fault_string, MEADOW_OS_BBD_SRAM_SIZE);
        MEADOW_TRACE_INFORMATION("Fault message:\n");
        char *line = fault_string;
        while (strlen(line) > 0)
        {
            while (!isalpha(*line))
            {
                line++;
            }
            if (strlen(line) > 0)
            {
                char *end = find_line_end(line);
                *end = 0;
                if ((memcmp(line, "up_", 3) == 0) || (memcmp(line, "arm_", 4) == 0))
                {
                    MEADOW_TRACE_INFORMATION("%s\n", line);
                    if (crash_file)
                    {
                        fprintf(crash_file, "%s\n", line);
                    }
                }
                line = end + 1;
                if ((*line == '\n') || (*line == '\r'))
                {
                    line++;
                }
            }
        }
        if (crash_file)
        {
            fclose(crash_file);
        }
        free(fault_string);
    }
}

/****************************************************************************
 * Name: meadow_os_fault_handler_process_rt_fault_message
 *
 * Description:
 *  Decode the RT fault status and send the results to syslog.
 *
 * Input Parameters:
 *  fault - The fault status.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *  BKPSRAM will be cleared by the caller.
 *
 ****************************************************************************/
static void meadow_os_fault_handler_process_rt_fault_message(uint8_t fault)
{
    char *message;

    message = (char *) malloc(FAULT_BUFFER_LENGTH);
    if (message == NULL)
    {
        MEADOW_TRACE_INFORMATION("RT Fault: Unable to allocate memory for fault message.\n");
    }
    else
    {
        meadow_os_fault_logging_phase_string(fault, message, FAULT_BUFFER_LENGTH);
        MEADOW_TRACE_INFORMATION("RT Fault: %s\n", message);
        free(message);
        if (fault & FAULT_LOGGING_RT_FILE_ERROR)
        {
            MEADOW_TRACE_INFORMATION("RT Fault: Error writing fault information to crash log file.\n");
        }
    }
    char *fault_string = (char *) malloc(MEADOW_OS_BBD_SRAM_SIZE);
    if (fault_string == NULL)
    {
        MEADOW_TRACE_INFORMATION("OS Fault: Unable to allocate memory for fault message.\n");
    }
    else
    {
        meadow_os_bbd_strdup_from_sram(fault_string, MEADOW_OS_BBD_SRAM_SIZE);
        MEADOW_TRACE_INFORMATION("Fault message:\n");
        MEADOW_TRACE_INFORMATION("%s\n", fault_string);
        free(fault_string);
    }
}

/****************************************************************************
 * Name: meadow_os_fault_handler_check_fault_code
 *
 * Description:
 *  Check the fault code following a reboot and determine the system (OS or
 *  RT) that has faulted and send the fault information to syslog.
 * 
 *  The fault code will be reset at the end of this method.
 *
 * Input Parameters:
 *  None.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *  None.
 *
 ****************************************************************************/
void meadow_os_fault_handler_check_fault_code(void)
{
    if (meadow_os_bbd_register_get_value(HCOM_NX_MEADOW_RESET_SOURCE_INFO_BBR_NUM, &_fault_status) != 0)
    {
        return;
    }
    MEADOW_TRACE_INFORMATION("Fault status: 0x%08x\n", _fault_status);
    if (_fault_status != 0)
    {
        if (_fault_status & FAULT_LOGGING_OS_COMPONENT_ERRORED)
        {
            meadow_os_fault_handler_process_os_fault_message(_fault_status & 0xff);
        }
        //
        //  We will check the runtime fault status in case the RT reporting faulted and
        //  created an error in the OS (which will have been reported above).
        //
        if (_fault_status & FAULT_LOGGING_RT_COMPONENT_ERRORED)
        {
            meadow_os_fault_handler_process_rt_fault_message((_fault_status >> FAULT_LOGGING_RT_BIT_SHIFT) & 0xff);
        }
        meadow_os_bbd_clear_sram();
        //
        //  Clear any fault codes before we exit.
        //
        meadow_os_bbd_register_set_value(HCOM_NX_MEADOW_RESET_SOURCE_INFO_BBR_NUM, 0);
    }
}
