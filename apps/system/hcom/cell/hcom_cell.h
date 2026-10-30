/****************************************************************************
 * apps/system/hcom/cell/hcom_cell.h
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

#ifndef __CONFIGS_MEADOW_SRC_HCOM_CELL_CELL__H
#define __CONFIGS_MEADOW_SRC_HCOM_CELL_CELL__H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>

#include <meadow/hcom_shared_common.h>

/****************************************************************************
 * Shared Definitions
 ****************************************************************************/

// The size of the modem output returned by meadow_get_cell_at_cmds_output();
// the managed caller passes a 1024 byte buffer.
#define CELL_AT_CMDS_OUTPUT_MAX_SIZE 1024

// Request bits passed by the managed API to meadow_cell_change_state(). The
// values are fixed by Meadow.Core; 0 means resume, which is a no-op here
// because the link is never paused.
#define CELL_AT_CMD_GPS             (1 << 1)
#define CELL_AT_CMD_SIGNAL_QUALITY  (1 << 2)
#define CELL_AT_CMD_SCAN            (1 << 3)

/****************************************************************************
 * Public Data
 ****************************************************************************/

// Error codes returned by meadow_get_cell_error(). The values are reported
// to the managed API, do not renumber them.
enum hcom_cell_err_e
{
  CELL_INVALID_SETTING_ERR,
  CELL_INVALID_MODEM_ERR,
  CELL_PPPD_LOST_CONNECTION_ERR,
  CELL_PPPD_TIMEOUT_ERR,
  CELL_PPPD_THREAD_ERR,
};
typedef enum hcom_cell_err_e hcom_cell_err_t;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int hcom_cell_start(void);

// The native API also used by the managed side (apps/system/mono/mappings-meadow.h).
bool meadow_cell_is_connected(void);
void meadow_cell_change_state(int state);
int meadow_get_cell_at_cmds_output(unsigned char *buf);
int meadow_get_cell_error(void);

#if defined(CONFIG_CELL_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)
// Entry points for the native cell tests (tests/cell_tests.c).
int hcom_cell_test_create_scripts(const cell_settings_t *settings, char *connect_script, size_t connect_size,
                                  char *disconnect_script, size_t disconnect_size);
int hcom_cell_test_at(int fd, const char *command, int timeout_ms, char *out, size_t out_size);
bool hcom_cell_test_gga_has_fix(const char *reply);
#endif

#endif //__CONFIGS_MEADOW_SRC_HCOM_CELL_CELL__H
