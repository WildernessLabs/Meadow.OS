/****************************************************************************
 * \apps\examples\hcom\cell\hcom_pppd.c
 *
 *   Copyright (C) 2020 Wilderness Labs. All rights reserved.
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

#include <meadow/hcom_protocol.h>
#include <meadow/meadow_os.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <inttypes.h>
#include <mqueue.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "netutils/chat.h"
#include "netutils/pppd.h"

#include "hcom_pppd.h"
#include "../hcom_common.h"
#include "../misc/espcp_utils.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

// Note:
// These connection scripts are used by PPPD to send AT commands to the 
// module to connect using cell network
#define CONNECT_SCRIPT_MAX_SIZE 1024
#define DISCONNECT_SCRIPT_MAX_SIZE 64
#define AUTHENTICATION_CMD_MAX_SIZE 128
#define OPERATOR_SELECTION_CMD_MAX_SIZE 128
#define GPS_AT_CMD_TIMEOUT 600
#define NETWORK_SCAN_AT_CMD_TIMEOUT 600
#define GET_CSQ_AT_CMD_TIMEOUT 120

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool cell_connected = false;
static char *cell_at_cmds_output;
static hcom_pppd_handler_t hcom_cell_handler;
static hcom_cell_err_t cell_err;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void hcom_pppd_connected_event(const struct in_addr *local_ip,
                                      const struct in_addr *pri_dns,
                                      const struct in_addr *sec_dns);
static void hcom_pppd_disconnected_event(int err_base);
static void hcom_pppd_at_cmd_event(int ret);
static void hcom_pppd_retry_exceeded_event(void);
static void hcom_pppd_connecting_event(void);
static void hcom_pppd_connect_script_failed_event(const char *script, const char *response);

//====================================================================
// Settings the configuration leaves out are NULL: treat them as empty.
static const char *hcom_pppd_str(const char *s)
{
  return (s == NULL) ? "" : s;
}

static char *hcom_pppd_strdup(const char *s)
{
  return (s == NULL) ? NULL : strdup(s);
}

//====================================================================
// Free a settings copy made by hcom_pppd_copy_settings().
static void hcom_pppd_free_settings(cell_settings_t *settings)
{
  if (settings == NULL)
  {
    return;
  }

  free(settings->module);
  free(settings->apn);
  free(settings->operator);
  free(settings->mode);
  free(settings->ttyname);
  free(settings->timeout);
  free(settings->pap_user);
  free(settings->pap_password);
  free(settings);
}

//====================================================================
// Deep copy of the cell settings for the PPPD thread, which owns it. A struct
// copy is not enough: the strings belong to the configuration copy, which is
// freed as soon as the thread has been started. Only the members the
// configuration copy sets are taken; the others are zeroed.
static cell_settings_t *hcom_pppd_copy_settings(const cell_settings_t *source)
{
  cell_settings_t *copy = calloc(1, sizeof(cell_settings_t));
  if (copy == NULL)
  {
    return NULL;
  }

  copy->module_id = source->module_id;
  copy->scan_mode = source->scan_mode;
  copy->module = hcom_pppd_strdup(source->module);
  copy->apn = hcom_pppd_strdup(source->apn);
  copy->operator = hcom_pppd_strdup(source->operator);
  copy->mode = hcom_pppd_strdup(source->mode);
  copy->ttyname = hcom_pppd_strdup(source->ttyname);
  copy->timeout = hcom_pppd_strdup(source->timeout);
  copy->pap_user = hcom_pppd_strdup(source->pap_user);
  copy->pap_password = hcom_pppd_strdup(source->pap_password);

  if (((source->module != NULL) && (copy->module == NULL)) ||
      ((source->apn != NULL) && (copy->apn == NULL)) ||
      ((source->operator != NULL) && (copy->operator == NULL)) ||
      ((source->mode != NULL) && (copy->mode == NULL)) ||
      ((source->ttyname != NULL) && (copy->ttyname == NULL)) ||
      ((source->timeout != NULL) && (copy->timeout == NULL)) ||
      ((source->pap_user != NULL) && (copy->pap_user == NULL)) ||
      ((source->pap_password != NULL) && (copy->pap_password == NULL)))
  {
    hcom_pppd_free_settings(copy);
    return NULL;
  }

  return copy;
}

//====================================================================
// This function is used to generate the connection and disconnection script
// based on cell settings and is later passed to the pppd() function
static int hcom_pppd_create_connect_scripts(cell_settings_t *cell_settings, char **connect_script, char **disconnect_script)
{
  const char *pap_user = hcom_pppd_str(cell_settings->pap_user);
  const char *pap_password = hcom_pppd_str(cell_settings->pap_password);
  const char *operator = hcom_pppd_str(cell_settings->operator);
  const char *mode = hcom_pppd_str(cell_settings->mode);
  const char *timeout = hcom_pppd_str(cell_settings->timeout);
  const char *apn = hcom_pppd_str(cell_settings->apn);

  char *authentication_cmd = (char *)malloc(AUTHENTICATION_CMD_MAX_SIZE * sizeof(char));
  if (authentication_cmd == NULL)
  {
    syslog(LOG_ERR, "%s-%d-Failed to allocate authentication\n", __FILE__, __LINE__);
    return -ENOMEM;
  }

  char *operator_selection_cmd = (char *)malloc(OPERATOR_SELECTION_CMD_MAX_SIZE * sizeof(char));
  if (operator_selection_cmd == NULL)
  {
    syslog(LOG_ERR, "%s-%d-Failed to allocate operator\n", __FILE__, __LINE__);
    free(authentication_cmd);
    return -ENOMEM;
  }

  snprintf_chk(authentication_cmd, AUTHENTICATION_CMD_MAX_SIZE,
      pap_user[0] != '\0' && pap_password[0] != '\0'
          ? "AT+CGAUTH=1,1,\\\"%s\\\",\\\"%s\\\" PAUSE 3 OK "
          : "",
      pap_user,
      pap_password
  );

  // If the carrier operator code or the network operator mode is missing, the 
  // automatic network selection will be used (AT+COPS=0)
  snprintf_chk(operator_selection_cmd, OPERATOR_SELECTION_CMD_MAX_SIZE,
      operator[0] != '\0' && mode[0] != '\0'
          ? "AT+COPS=1,2,\\\"%s\\\",%s PAUSE 3 OK "
          : "AT+COPS=0 PAUSE 3 OK ",
      operator,
      mode
  );

  switch (cell_settings->module_id)
  {
    case CELL_BG770A_MODULE:
        snprintf_chk(*connect_script, CONNECT_SCRIPT_MAX_SIZE, 
          "ECHO ON " 
          "TIMEOUT %s "
          "\"\" AT+CMEE=2 "
          "PAUSE 3 "
          "OK AT+GSN "
          "PAUSE 3 "
          "OK AT+CGDCONT=1,\\\"IP\\\",\\\"%s\\\" "
          "PAUSE 3 "
          "OK %s"
          "AT+QCSQ "
          "PAUSE 3 "
          "OK AT+CSQ "
          "PAUSE 3 "
          "OK %s"
          "ATD*99# "
          "CONNECT \\c",
          timeout, 
          apn,
          authentication_cmd,
          operator_selection_cmd
      );
    break;
  
    case CELL_M95_MODULE:
      snprintf_chk(*connect_script, CONNECT_SCRIPT_MAX_SIZE, 
        "ECHO ON " 
        "TIMEOUT %s "
        "\"\" AT+QACCM=0,0 "
        "PAUSE 3 "
        "OK AT+GSN "
        "PAUSE 3 "
        "OK AT+CGDCONT=1,\\\"IP\\\",\\\"%s\\\" "
        "PAUSE 3 "
        "OK AT+CSQ "
        "PAUSE 3 "
        "OK ATD*99# "
        "CONNECT \\c",
        timeout, 
        apn
      );
    break;
    
    case CELL_BG95M3_MODULE:
      snprintf_chk(*connect_script, CONNECT_SCRIPT_MAX_SIZE, 
        "ECHO ON "
        "TIMEOUT %s "
        "\"\" AT+CFUN=1,1 "
        "PAUSE 15 "
        "OK AT+CMEE=2 "
        "PAUSE 3 "
        "OK AT+GSN "
        "PAUSE 3 "
        "OK AT+CGDCONT=1,\\\"IP\\\",\\\"%s\\\" "
        "PAUSE 3 "
        "OK AT+QCSQ "
        "PAUSE 3 "
        "OK AT+CSQ "
        "PAUSE 3 "
        "OK %s"
        "ATD*99# "
        "CONNECT \\c",
        timeout, 
        apn,
        operator_selection_cmd
      );
    break;

    case CELL_EG21GL_MODULE:
      snprintf_chk(*connect_script, CONNECT_SCRIPT_MAX_SIZE,
        "ECHO ON "
        "TIMEOUT %s "
        "\"\" AT+CMEE=2 "
        "PAUSE 3 "
        "OK AT+GSN "
        "PAUSE 3 "
        "OK AT+CGDCONT=1,\\\"IP\\\",\\\"%s\\\" "
        "PAUSE 3 "
        "OK AT+QCSQ "
        "PAUSE 3 "
        "OK AT+CSQ "
        "PAUSE 3 "
        "OK %s"
        "ATD*99# "
        "CONNECT \\c",
        timeout, 
        apn,
        operator_selection_cmd
      );
    break;
    
    default:
      syslog(LOG_ERR, "%s-%d-Failed getting connect script\n", __FILE__, __LINE__);
      free(authentication_cmd);
      free(operator_selection_cmd);
      return -EINVAL;
    break;
  }

  snprintf_chk(*disconnect_script, DISCONNECT_SCRIPT_MAX_SIZE,
    "AT+CFUN=1,1 "
    "PAUSE 10 "
    "OK " "\"\" ATZ "
    "OK \\c"
  );

  free(authentication_cmd);
  free(operator_selection_cmd);

  return OK;
}

//====================================================================
// This function is to get the script according to the state (GPS, Signal Quality
// or Scan). After the selected script will be performed in PPPD thread.
static void hcom_pppd_get_script(int state, char *script)
{
  // TODO: Add GPS timeout to cell config yaml
  // TODO: Add a parameter to specify the desired NMEA sentences
  switch (state)
  {
    case CELL_AT_CMD_GPS:
      syslog(LOG_INFO, "%s-%d-Cell GPS/GNSS\n", __FILE__, __LINE__);
      snprintf_chk(hcom_cell_handler.script, CONNECT_SCRIPT_MAX_SIZE,
        "TIMEOUT %d \"\" "
        "AT+QGPS=1,2,180,1 PAUSE 3 OK " 
        "AT+QCFG=\\\"gpio\\\",1,64,1,0,0,1 PAUSE 3 OK "
        "AT+QCFG=\\\"gpio\\\",3,64,1,1 PAUSE 3 OK "
        "AT+QGPSCFG=\\\"nmeasrc\\\",1 PAUSE 120 OK "
        "AT+QGPSGNMEA=\\\"GSV\\\" PAUSE 3 OK "
        "AT+QGPSGNMEA=\\\"GGA\\\" PAUSE 3 OK "
        "AT+QGPSGNMEA=\\\"RMC\\\" PAUSE 3 OK "
        "AT+QGPSGNMEA=\\\"GSA\\\" PAUSE 3 OK "
        "AT+QGPSGNMEA=\\\"VTG\\\" PAUSE 3 OK "
        "AT+QGPSEND PAUSE 3 OK "
        "AT+QCFG=\\\"gpio\\\",1,64,1,0,0,1 PAUSE 3 OK "
        "AT+QCFG=\\\"gpio\\\",3,64,0,1 PAUSE 3 OK "
        "\\c", GPS_AT_CMD_TIMEOUT);
      break;

    case CELL_AT_CMD_SIGNAL_QUALITY:
      syslog(LOG_INFO, "%s-%d-Cell Signal Quality\n", __FILE__, __LINE__);
      snprintf_chk(hcom_cell_handler.script, CONNECT_SCRIPT_MAX_SIZE,
        "TIMEOUT %d \"\" AT+CSQ PAUSE 3 OK \\c",
        GET_CSQ_AT_CMD_TIMEOUT);
      break;

    case CELL_AT_CMD_SCAN:
      syslog(LOG_INFO, "%s-%d-Cell Scan Network\n", __FILE__, __LINE__);
      snprintf_chk(hcom_cell_handler.script, CONNECT_SCRIPT_MAX_SIZE,
        "TIMEOUT %d \"\" AT+COPS=? PAUSE 3 OK \\c",
        NETWORK_SCAN_AT_CMD_TIMEOUT);
      break;

    default:
      break;
  }
}

//====================================================================
// Queue a cell event for the managed side. espcp_queue_event_messages()
// copies the encoded bytes into the message queue (mq_send), so both buffers
// can live on the stack.
static int hcom_pppd_raise_event(uint32_t function, uint32_t status_code)
{
  espcp_event_data_t message;
  uint8_t encodedData[ESPCP_EVENT_DATA_SIZE];

  memset(&message, 0, sizeof(message));
  message.interface = ESPCP_CELL_INTERFACE;
  message.function = function;
  message.status_code = status_code;
  message.message_id = ESPCP_SIMPLE_EVENT_MESSAGE_ID;

  espcp_encode_event_data(&message, encodedData);
  return espcp_queue_event_messages(encodedData);
}

static void hcom_pppd_connecting_event(void)
{
  syslog(LOG_INFO, "%s-%d-Cell network starts to connecting\n", __FILE__, __LINE__);
  int result = ERROR;

  result = hcom_pppd_raise_event(ESPCP_CELL_CONNECTING_EVENT, ESPCP_COMPLETED_OK_STATUS_CODE);
  syslog(LOG_INFO, "%s-%d-Cell event message result: %d\n", __FILE__, __LINE__, result);
}

//====================================================================
// Report a failed connect script, and the modem response to it, to the host
// so that cell configuration problems can be diagnosed.
static void hcom_pppd_connect_script_failed_event(const char *script, const char *response)
{
  syslog(LOG_INFO, "%s-%d-Cell connect script failed, retrying\n", __FILE__, __LINE__);

#ifdef HCOM_CELL_DEBUG_LOGS
  hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
      "Cell connect script failed, retrying... Script:\n", __FILE__, __LINE__);
  hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
      (char *)script, __FILE__, __LINE__);
  hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
      "Response:\n", __FILE__, __LINE__);
  hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
      (response != NULL) ? (char *)response : "", __FILE__, __LINE__);
#else
  (void)script;
  (void)response;
#endif
}

static void hcom_pppd_retry_exceeded_event(void)
{
  syslog(LOG_INFO, "%s-%d-Cell network retry exceeded\n", __FILE__, __LINE__);
  int result = ERROR;

  result = hcom_pppd_raise_event(ESPCP_CELL_RETRY_EXCEEDED_EVENT, ESPCP_COMPLETED_OK_STATUS_CODE);
  syslog(LOG_INFO, "%s-%d-Cell event message result: %d\n", __FILE__, __LINE__, result);
}

static void hcom_pppd_at_cmd_event(int ret)
{
  if (ret < 0)
  {
    syslog(LOG_INFO, "%s-%d-Cell chat failed: %d\n", __FILE__, __LINE__, ret);
    return;
  }

  if ((cell_at_cmds_output != NULL) && (strlen(cell_at_cmds_output) > 0))
  {
    syslog(LOG_INFO, "%s-%d-Cell: %s \n", __FILE__, __LINE__, cell_at_cmds_output);

    int result = hcom_pppd_raise_event(ESPCP_CELL_AT_CMD_EVENT, ESPCP_COMPLETED_OK_STATUS_CODE);
    syslog(LOG_INFO, "%s-%d-Cell event message result: %d\n", __FILE__, __LINE__, result);
  }
}

//====================================================================
// Put the modem UART into raw mode. COM1 (/dev/ttyS0) is also the NuttX serial
// console, which defaults to echo, canonical (line) mode and CR/LF translation.
// Echo sends everything the modem says straight back to it, including its echo
// of AT+CFUN=1,1 (which resets it again), and line mode garbles what chat reads.
// The settings belong to the device, so they stay in force when pppd opens it.
static int hcom_pppd_set_tty_raw(const char *ttyname)
{
  struct termios tio;
  int ret = OK;

  int fd = open(ttyname, O_RDWR);
  if (fd < 0)
  {
    syslog(LOG_ERR, "%s-%d-Failed to open %s, errno=%d\n", __FILE__, __LINE__, ttyname, errno);
    return -errno;
  }

  if (tcgetattr(fd, &tio) < 0)
  {
    ret = -errno;
  }
  else
  {
    cfmakeraw(&tio);
    if (tcsetattr(fd, TCSANOW, &tio) < 0)
    {
      ret = -errno;
    }
  }

  if (ret < 0)
  {
    syslog(LOG_ERR, "%s-%d-Failed to set %s to raw mode, ret=%d\n", __FILE__, __LINE__, ttyname, ret);
  }

  close(fd);
  return ret;
}

//====================================================================
// The handler script and the AT command output are published to the managed
// API (meadow_cell_change_state, meadow_get_cell_at_cmds_output), so they are
// allocated once and never freed.
static int hcom_pppd_create_handler(void)
{
  hcom_cell_handler.state = CELL_RESUMED;
  hcom_cell_handler.callback = hcom_pppd_at_cmd_event;
  if (hcom_cell_handler.script == NULL)
  {
    hcom_cell_handler.script = (char *)calloc(1, CONNECT_SCRIPT_MAX_SIZE);
  }

  if (cell_at_cmds_output == NULL)
  {
    cell_at_cmds_output = (char *)calloc(1, CONNECT_SCRIPT_OUTPUT_MAX_SIZE);
  }

  if ((hcom_cell_handler.script == NULL) || (cell_at_cmds_output == NULL))
  {
    syslog(LOG_ERR, "%s-%d-Failed to allocate cell handler script or AT command output\n", __FILE__, __LINE__);
    return -ENOMEM;
  }

  return OK;
}

//====================================================================
// This is the PPPD (Point-to-Point Protocol Daemon) thread, which is 
// responsible to send AT commands to the modem, through the chat app, 
// and to manage the PPP connection. The thread owns the settings copy it is
// given and frees it on every exit path.
static void *pppd_thread(void *cell_settings_ptr)
{
#if HCOM_DIAG_OUTPUT_SYSLOG_PID_OF_NEW_THREADS > 0
  syslog(LOG_MDIAG, "New pthread [Thrd:%d][Task:%d] %s\n",
     gettid(), getpid(), "pppd_thread");
#endif

    cell_settings_t *cell_settings = (cell_settings_t *) cell_settings_ptr;

    if (cell_settings == NULL)
    {
        syslog(LOG_ERR, "%s-%d-Failed getting cell settings\n", __FILE__, __LINE__);
        sleep(20);
        syslog(LOG_INFO, "%s-%d-Failed starting PPPD\n", __FILE__, __LINE__);
        cell_err = CELL_INVALID_SETTING_ERR;
        hcom_pppd_disconnected_event(cell_err);
        return NULL;
    }

    if (cell_settings->module_id == CELL_UNKNOWN_MODULE)
    {
        syslog(LOG_INFO, "%s-%d-Invalid cell module id: %" PRIu32 "\n", __FILE__, __LINE__, cell_settings->module_id);
        sleep(20);
        syslog(LOG_INFO, "%s-%d-Failed starting PPPD\n", __FILE__, __LINE__);
        cell_err = CELL_INVALID_MODEM_ERR;
        hcom_pppd_disconnected_event(cell_err);
        hcom_pppd_free_settings(cell_settings);
        return NULL;
    }

    char *connect_script = (char *)malloc(CONNECT_SCRIPT_MAX_SIZE * sizeof(char));
    char *disconnect_script = (char *)malloc(DISCONNECT_SCRIPT_MAX_SIZE * sizeof(char));
    if ((connect_script == NULL) || (disconnect_script == NULL))
    {
        syslog(LOG_ERR, "%s-%d-Failed to allocate memory for the chat scripts\n", __FILE__, __LINE__);
        free(connect_script);
        free(disconnect_script);
        hcom_pppd_free_settings(cell_settings);
        return NULL;
    }

    int ret;
    ret = hcom_pppd_create_connect_scripts(cell_settings, &connect_script, &disconnect_script);
    if (ret < 0)
    {
        syslog(LOG_ERR, "%s-%d-Failed to generate connect scripts, ret=%d\n", __FILE__, __LINE__, ret);
        free(connect_script);
        free(disconnect_script);
        hcom_pppd_free_settings(cell_settings);
        return NULL;
    }

    ret = hcom_pppd_create_handler();
    if (ret < 0)
    {
        syslog(LOG_ERR, "%s-%d-Failed to create pppd handler, ret=%d\n", __FILE__, __LINE__, ret);
        free(connect_script);
        free(disconnect_script);
        hcom_pppd_free_settings(cell_settings);
        return NULL;
    }

    syslog(LOG_INFO, "%s-%d-Chat scripts created: %s\n %s\n",
                        __FILE__, __LINE__, connect_script, disconnect_script);

    struct pppd_settings_s pppd_settings =
    {
        .disconnect_script = disconnect_script,
        .connect_script = connect_script,
        .connect_event = hcom_pppd_connected_event,
        .disconnect_event = hcom_pppd_disconnected_event,
        .connecting_event = hcom_pppd_connecting_event,
        .retry_count_exceeded_event = hcom_pppd_retry_exceeded_event,
        .connect_script_failed_event = hcom_pppd_connect_script_failed_event,
        .cell_at_cmds_output = cell_at_cmds_output,
        .cell_at_cmds_output_size = CONNECT_SCRIPT_OUTPUT_MAX_SIZE,
        .cell_handler = &hcom_cell_handler,
    };

    strlcpy(pppd_settings.ttyname, hcom_pppd_str(cell_settings->ttyname), sizeof(pppd_settings.ttyname));
#ifdef CONFIG_NETUTILS_PPPD_PAP
    strlcpy(pppd_settings.pap_username, hcom_pppd_str(cell_settings->pap_user), sizeof(pppd_settings.pap_username));
    strlcpy(pppd_settings.pap_password, hcom_pppd_str(cell_settings->pap_password), sizeof(pppd_settings.pap_password));
#endif

    // Not fatal: pppd reports the failure through the connect script events.
    (void)hcom_pppd_set_tty_raw(pppd_settings.ttyname);

    syslog(LOG_INFO, "%s-%d-Starting PPPD\n", __FILE__, __LINE__);
    pppd(&pppd_settings);

    // pppd() only returns if it could not start (no tun device or tty).
    free(connect_script);
    free(disconnect_script);
    hcom_pppd_free_settings(cell_settings);

    sleep(20);
    syslog(LOG_INFO, "%s-%d-Failed after starting PPPD\n", __FILE__, __LINE__);
    hcom_pppd_disconnected_event(cell_err);

    return NULL;
}

bool meadow_cell_is_connected(void)
{
    return cell_connected;
}

void meadow_cell_change_state(int state)
{
  if (hcom_cell_handler.script != NULL)
    {
      memset(hcom_cell_handler.script, 0x00, CONNECT_SCRIPT_MAX_SIZE);
      hcom_pppd_get_script(state, hcom_cell_handler.script);

      if (state != 0)
        {
          if (strlen(hcom_cell_handler.script) > 0)
            {
              syslog(LOG_INFO, "%s-%d-Cell script: %s\n", __FILE__, __LINE__, hcom_cell_handler.script);
              pppd_set_state(&hcom_cell_handler, CELL_AT_CMD);
            }
          pppd_set_state(&hcom_cell_handler, CELL_PAUSED);
        }
      else
        {
          // Waiting until script performed.
          // Do this, we protect the early changed state.
          while (hcom_cell_handler.state == (CELL_AT_CMD | CELL_PAUSED))
            {
              usleep(100);
            }
          hcom_cell_handler.state  = CELL_RESUMED;
        }
    }
  syslog(LOG_INFO, "%s-%d-Cell current state: %d\n", __FILE__, __LINE__, hcom_cell_handler.state);
}

void pppd_set_state(hcom_pppd_handler_t *handler, int state)
{
  handler->state = handler->state | state;
}

void pppd_clear_state(hcom_pppd_handler_t *handler, int state)
{
  handler->state = handler->state ^ state;
}

int meadow_get_cell_at_cmds_output(unsigned char *buf)
{
    // Nothing captured yet: the PPPD thread has not run.
    if (cell_at_cmds_output == NULL)
    {
        return 0;
    }

    size_t len = strlen(cell_at_cmds_output) + 1;
    memcpy(buf, cell_at_cmds_output, len);

    return len;
}

int meadow_get_cell_error (void)
{
  return (int)cell_err;
}

static void hcom_pppd_connected_event(const struct in_addr *local_ip,
                                      const struct in_addr *pri_dns,
                                      const struct in_addr *sec_dns)
{
    char address[INET_ADDRSTRLEN];
    char pri_dns_address[INET_ADDRSTRLEN];
    char sec_dns_address[INET_ADDRSTRLEN];
    char *network_dns_servers[2];
    const char *network_dns_server_names[2];
    uint32_t network_dns_server_count = 0;

    syslog(LOG_INFO, "%s-%d-Cell network has been successfully connected, IP address %s\n", __FILE__, __LINE__,
           inet_ntop(AF_INET, local_ip, address, sizeof(address)));

    // The DNS servers provided by the network go at the start of the dns.conf
    // file, followed by the user defined servers from the configuration.
    if ((pri_dns->s_addr != 0) && (inet_ntop(AF_INET, pri_dns, pri_dns_address, sizeof(pri_dns_address)) != NULL))
    {
        network_dns_server_names[network_dns_server_count] = "Primary";
        network_dns_servers[network_dns_server_count++] = pri_dns_address;
    }
    if ((sec_dns->s_addr != 0) && (inet_ntop(AF_INET, sec_dns, sec_dns_address, sizeof(sec_dns_address)) != NULL))
    {
        network_dns_server_names[network_dns_server_count] = "Secondary";
        network_dns_servers[network_dns_server_count++] = sec_dns_address;
    }
    hcom_common_utils_erase_dns_resolver_file();
    hcom_common_utils_add_servers_to_dns_resolver_file(network_dns_servers, network_dns_server_count);

#ifdef HCOM_CELL_DEBUG_LOGS
    char host_message[HCOM_MED_SHORT_HOST_STRING_BUFF_LENGTH];

    snprintf_chk(host_message, sizeof(host_message),
        "Connection established successfully! IP address '%s'.\n", address);
    hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
        host_message, __FILE__, __LINE__);

    for (uint32_t index = 0; index < network_dns_server_count; index++)
    {
        snprintf_chk(host_message, sizeof(host_message),
            "%s DNS server address: '%s'.\n", network_dns_server_names[index],
            network_dns_servers[index]);
        hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
            host_message, __FILE__, __LINE__);
    }
#endif

    cell_connected = true;

    meadow_configuration_t *config = meadow_os_deep_copy_config();
    bool get_time = (config != NULL) && (config->get_network_time_at_startup != 0);

    if (config != NULL)
    {
        hcom_common_utils_add_servers_to_dns_resolver_file(config->dns_servers, config->dns_servers_count);
    }
    meadow_os_config_free_resources(config);

    if (get_time)
    {
        ntpc_start();
    }

    int result = hcom_pppd_raise_event(ESPCP_CELL_CONNECTED_EVENT, ESPCP_COMPLETED_OK_STATUS_CODE);
    syslog(LOG_INFO, "%s-%d-Cell connected event message result: %d\n", __FILE__, __LINE__, result);
}

static void hcom_pppd_disconnected_event(int err_base) 
{
    syslog(LOG_INFO, "%s-%d-Cell network has been disconnected, error: %d\n", __FILE__, __LINE__, err_base);

    uint32_t function = ESPCP_CELL_ERROR_EVENT;
    if (err_base == CELL_PPPD_LOST_CONNECTION_ERR)
    {
      function = ESPCP_CELL_DISCONNECTED_EVENT;
    }

    int result = hcom_pppd_raise_event(function, ESPCP_FAILURE_STATUS_CODE);
    syslog(LOG_INFO, "%s-%d-Cell disconnected event message result: %d\n", __FILE__, __LINE__, result);

    cell_connected = false;
    cell_err = err_base;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/
//====================================================================
// This function is called by the startup manager to start the PPPD thread,
// which is responsible to establish cell connection, if Cell interface
// is enabled.
int hcom_pppd_start()
{
    int ret;
    pthread_t pppd_thread_id;
    meadow_configuration_t *config = meadow_os_deep_copy_config();

    if ((config != NULL) && (config->default_interface != NULL))
    {
        if (config->default_interface->interface_type != MEADOW_IFT_CELL)
        {
            meadow_os_config_free_resources(config);
            return OK;
        }

        syslog(LOG_INFO, "%s-%d-Attempting to start PPPD\n", __FILE__, __LINE__);
        if (config->default_cell_settings == NULL)
        {
            syslog(LOG_ERR, "%s-%d-Failed to get PPPD settings\n", __FILE__, __LINE__);

            // At this moment, the ESP32 is not ready for sending event messages
#ifdef HCOM_CELL_DEBUG_LOGS
            hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
                "Invalid cell settings. Please check your cell configuration file.", __FILE__, __LINE__);
#endif

            meadow_os_config_free_resources(config);
            return -ENOENT;
        }

        // The thread owns this copy; the configuration copy is freed below.
        cell_settings_t *cell_settings = hcom_pppd_copy_settings(config->default_cell_settings);
        if (cell_settings == NULL)
        {
            syslog(LOG_ERR, "%s-%d-Failed to copy the cell settings\n", __FILE__, __LINE__);
            meadow_os_config_free_resources(config);
            return -ENOMEM;
        }

        syslog(LOG_INFO, "%s-%d-cell module id: %" PRIu32 "\n", __FILE__, __LINE__, cell_settings->module_id);
        syslog(LOG_INFO, "%s-%d-cell module: %s\n", __FILE__, __LINE__, hcom_pppd_str(cell_settings->module));
        syslog(LOG_INFO, "%s-%d-cell apn: %s\n", __FILE__, __LINE__, hcom_pppd_str(cell_settings->apn));
        syslog(LOG_INFO, "%s-%d-cell operator: %s\n", __FILE__, __LINE__, hcom_pppd_str(cell_settings->operator));
        syslog(LOG_INFO, "%s-%d-cell ttyname: %s\n", __FILE__, __LINE__, hcom_pppd_str(cell_settings->ttyname));
        syslog(LOG_INFO, "%s-%d-cell timeout: %s\n", __FILE__, __LINE__, hcom_pppd_str(cell_settings->timeout));
        syslog(LOG_INFO, "%s-%d-cell user: %s\n", __FILE__, __LINE__, hcom_pppd_str(cell_settings->pap_user));
        syslog(LOG_INFO, "%s-%d-cell password: %s\n", __FILE__, __LINE__,
               ((cell_settings->pap_password != NULL) && (cell_settings->pap_password[0] != '\0')) ? "set" : "not set");
        syslog(LOG_INFO, "%s-%d-cell operation mode: %s\n", __FILE__, __LINE__, hcom_pppd_str(cell_settings->mode));
        syslog(LOG_INFO, "%s-%d-cell scan mode: %" PRIu32 "\n", __FILE__, __LINE__, cell_settings->scan_mode);

        if (cell_settings->scan_mode)
        {
#ifdef HCOM_CELL_DEBUG_LOGS
            hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
                "ScanMode config has been deprecated! Consult how to use the network scanner on Meadow cellular docs.", __FILE__, __LINE__);
#endif
        }
        pthread_attr_t attr;
        struct sched_param param;

        // Initialize thread attributes
        pthread_attr_init(&attr);

        // Set the stack size
        size_t stack_size = HCOM_THREAD_STACKSIZE_CELL_PPPD;
        pthread_attr_setstacksize(&attr, stack_size);

        // Set the scheduling policy to SCHED_FIFO (First-In, First-Out)
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

        // Set the priority of the thread
        param.sched_priority = HCOM_THREAD_PRIORITY_CELL_PPPD;
        pthread_attr_setschedparam(&attr, &param);

        // Nothing joins the thread, so let it release its resources when it
        // returns.
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        ret = pthread_create(&pppd_thread_id, &attr, pppd_thread, (void *) cell_settings);
        pthread_attr_destroy(&attr);
        if (ret == OK)
        {
            (void) pthread_setname_np(pppd_thread_id, HCOM_THREAD_NAME_CELL_PPPD);
            syslog(LOG_INFO, "%s@%d-PPPD thread launched\n", __FILE__, __LINE__);

            meadow_os_config_free_resources(config);
            return ret;
        }

        syslog(LOG_ERR, "%s@%d-The task to run PPPD failed in create\n",
                            __FILE__, __LINE__);
        hcom_pppd_free_settings(cell_settings);

        cell_err = CELL_PPPD_THREAD_ERR;
        hcom_pppd_disconnected_event(cell_err);

        meadow_os_config_free_resources(config);
        return -ret;
    }

  meadow_os_config_free_resources(config);
  return -ENODATA;
}