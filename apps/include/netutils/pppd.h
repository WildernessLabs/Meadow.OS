/****************************************************************************
 * apps/include/netutils/pppd.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __APPS_INCLUDE_NETUTILS_PPPD_H
#define __APPS_INCLUDE_NETUTILS_PPPD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stddef.h>
#include <time.h>
#include <netinet/in.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Configuration ************************************************************/

/* Required configuration settings:
 *
 *   CONFIG_NETUTILS_PPPD_PAP - PPPD PAP authentication support
 *     Default: n
 */

#define TTYNAMSIZ               16
#define PAP_USERNAME_SIZE       16
#define PAP_PASSWORD_SIZE       16

/* Meadow cell handler state bits (see struct pppd_cell_handler_s).  The
 * values are shared with the managed cell API, do not renumber them.
 */

#define CELL_RESUMED                0x00
#define CELL_PAUSED                 (1 << 0)
#define CELL_AT_CMD_GPS             (1 << 1)
#define CELL_AT_CMD_SIGNAL_QUALITY  (1 << 2)
#define CELL_AT_CMD_SCAN            (1 << 3)
#define CELL_AT_CMD                 (1 << 4)
#define CELL_CHAT_DONE              (1 << 5)
#define CELL_CHAT_FAILED            (1 << 6)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Meadow: cell error codes passed to disconnect_event.  The values are
 * reported to the managed cell API, do not renumber them.
 */

enum pppd_cell_err_e
{
  CELL_INVALID_SETTING_ERR,
  CELL_INVALID_MODEM_ERR,
  CELL_PPPD_LOST_CONNECTION_ERR,
  CELL_PPPD_TIMEOUT_ERR,
  CELL_PPPD_THREAD_ERR,
};

/* Meadow: lets the application pause the PPP link to run AT commands.
 * While CELL_PAUSED is set the link is dropped; each time CELL_AT_CMD is
 * set, pppd runs 'script' through chat, calls 'callback' with the result
 * and clears CELL_AT_CMD.  The link is re-established once 'state' returns
 * to CELL_RESUMED.
 */

struct pppd_cell_handler_s
{
  volatile int state;
  FAR char *script;
  CODE void (*callback)(int ret);
};

struct pppd_settings_s
{
  /* Serial Interface */

  char ttyname[TTYNAMSIZ];

#ifdef CONFIG_NETUTILS_PPPD_PAP
  /* PAP Authentication Settings */

  char pap_username[PAP_USERNAME_SIZE];
  char pap_password[PAP_PASSWORD_SIZE];
#endif /* CONFIG_NETUTILS_PPPD_PAP */

  /* Chat Scripts */

  FAR const char *connect_script;
  FAR const char *disconnect_script;

  /* Meadow cell extensions, all optional (NULL when unused) */

  /* Receives the modem responses to the chat scripts */

  FAR char *cell_at_cmds_output;
  size_t cell_at_cmds_output_size;

  /* Link events */

  CODE void (*connect_event)(FAR const struct in_addr *local_ip,
                             FAR const struct in_addr *pri_dns,
                             FAR const struct in_addr *sec_dns);
  CODE void (*disconnect_event)(int err_base);
  CODE void (*connecting_event)(void);
  CODE void (*retry_count_exceeded_event)(void);
  CODE void (*connect_script_failed_event)(FAR const char *script,
                                           FAR const char *response);

  /* AT command handler */

  FAR struct pppd_cell_handler_s *cell_handler;
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Functions Definitions
 ****************************************************************************/

/****************************************************************************
 * Name: pppd
 *
 * Description:
 *   Create an pppd connection
 *
 * Input Parameters:
 *    pppd_settings, setting struct for the ppp connection
 *
 * Returned Value:
 *   Returns termination state, blocking as long as the connection is up
 *
 ****************************************************************************/

int pppd(const struct pppd_settings_s *ppp_settings);

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* __APPS_INCLUDE_NETUTILS_PPPD_H */
