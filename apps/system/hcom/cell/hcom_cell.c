/****************************************************************************
 * apps/system/hcom/cell/hcom_cell.c
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

//
//  Cell network support.
//
//  The modem UART is multiplexed with CMUX (3GPP 27.010) into virtual
//  channels, so that AT commands never have to take the data link down:
//
//    COM1 -> cmux_create() -> DLCI 1: PPP, run by the stock NuttX pppd()
//                             DLCI 2: AT commands, run by the CellAT thread
//
//  The CellPPPD thread waits for the modem, starts CMUX and then runs pppd()
//  on the data channel. The CellAT thread watches ppp0 for the link state,
//  raises the cell events for the managed API and runs the GNSS, signal
//  quality and network scan requests posted with meadow_cell_change_state().
//

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <meadow/hcom_protocol.h>
#include <meadow/meadow_os.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "netutils/cmux.h"
#include "netutils/netlib.h"
#include "netutils/pppd.h"

#include "hcom_cell.h"
#include "../hcom_common.h"
#include "../misc/espcp_utils.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

// CMUX channels: DLCI 0 is the control channel, then the PPP and AT channels.
#define CELL_CMUX_CHANNELS 3
#define CELL_DLCI_PPP 1
#define CELL_DLCI_AT 2
#define CELL_PTY_PATH_SIZE 16
#define CELL_MAX_PTYS 16

#define CONNECT_SCRIPT_MAX_SIZE 512
#define DISCONNECT_SCRIPT_MAX_SIZE 32
#define AUTHENTICATION_CMD_MAX_SIZE 128
#define OPERATOR_SELECTION_CMD_MAX_SIZE 128
#define QICSGP_CMD_MAX_SIZE 256

#define CELL_MODEM_READY_TIMEOUT_S 30
#define CELL_AT_TIMEOUT_MS 5000
#define CELL_SCAN_TIMEOUT_MS 180000
#define CELL_GNSS_FIX_TIMEOUT_S 120
#define CELL_GNSS_POLL_S 3
#define CELL_CSQ_ATTEMPTS 3
#define CELL_LINK_DOWN_REPORT_S 60

// Before CMUX starts: no echo, verbose errors, no hardware flow control (the
// RTS/CTS lines are not wired on the mikroBUS socket), then CMUX basic mode,
// UIH frames, 115200 baud, 127 byte frames.
#define CELL_CMUX_SCRIPT \
  "TIMEOUT 10 " \
  "\"\" ATE0 " \
  "OK AT+CMEE=2 " \
  "OK AT+IFC=0,0 " \
  "OK AT+CMUX=0,0,5,127,10,3,30,10,2 " \
  "OK \\c"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static volatile bool cell_connected = false;
static volatile int cell_err = CELL_INVALID_SETTING_ERR;

// The settings copy owned by the CellPPPD thread (see hcom_cell_start()).
static cell_settings_t *cell_settings;

static char cell_ppp_pty[CELL_PTY_PATH_SIZE];
static char cell_at_pty[CELL_PTY_PATH_SIZE];

// The pending managed request (CELL_AT_CMD_* bits), taken by the CellAT thread.
static volatile int cell_pending_request = 0;

// The modem output returned to the managed API, written by the CellAT thread.
static char cell_at_cmds_output[CELL_AT_CMDS_OUTPUT_MAX_SIZE];
static pthread_mutex_t cell_output_lock = PTHREAD_MUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

//====================================================================
// Settings the configuration leaves out are NULL: treat them as empty.
static const char *hcom_cell_str(const char *s)
{
  return (s == NULL) ? "" : s;
}

static char *hcom_cell_strdup(const char *s)
{
  return (s == NULL) ? NULL : strdup(s);
}

//====================================================================
// Free a settings copy made by hcom_cell_copy_settings().
static void hcom_cell_free_settings(cell_settings_t *settings)
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
// Deep copy of the cell settings for the cell threads. A struct copy is not
// enough: the strings belong to the configuration copy, which is freed as soon
// as the threads have been started. Only the members the configuration copy
// sets are taken; the others are zeroed (#917).
static cell_settings_t *hcom_cell_copy_settings(const cell_settings_t *source)
{
  cell_settings_t *copy = calloc(1, sizeof(cell_settings_t));
  if (copy == NULL)
  {
    return NULL;
  }

  copy->module_id = source->module_id;
  copy->scan_mode = source->scan_mode;
  copy->module = hcom_cell_strdup(source->module);
  copy->apn = hcom_cell_strdup(source->apn);
  copy->operator = hcom_cell_strdup(source->operator);
  copy->mode = hcom_cell_strdup(source->mode);
  copy->ttyname = hcom_cell_strdup(source->ttyname);
  copy->timeout = hcom_cell_strdup(source->timeout);
  copy->pap_user = hcom_cell_strdup(source->pap_user);
  copy->pap_password = hcom_cell_strdup(source->pap_password);

  if (((source->module != NULL) && (copy->module == NULL)) ||
      ((source->apn != NULL) && (copy->apn == NULL)) ||
      ((source->operator != NULL) && (copy->operator == NULL)) ||
      ((source->mode != NULL) && (copy->mode == NULL)) ||
      ((source->ttyname != NULL) && (copy->ttyname == NULL)) ||
      ((source->timeout != NULL) && (copy->timeout == NULL)) ||
      ((source->pap_user != NULL) && (copy->pap_user == NULL)) ||
      ((source->pap_password != NULL) && (copy->pap_password == NULL)))
  {
    hcom_cell_free_settings(copy);
    return NULL;
  }

  return copy;
}

//====================================================================
// Queue a cell event for the managed side. espcp_queue_event_messages()
// copies the encoded bytes into the message queue (mq_send), so both buffers
// can live on the stack.
static int hcom_cell_raise_event(uint32_t function, uint32_t status_code)
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

//====================================================================
// Send a message to the host when cell debug messages are enabled.
static void hcom_cell_host_message(const char *text)
{
#ifdef HCOM_CELL_DEBUG_LOGS
  hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
      (char *)text, __FILE__, __LINE__);
#else
  (void)text;
#endif
}

//====================================================================
// Make a copy of the modem output available to the managed API.
static void hcom_cell_publish_output(const char *output)
{
  pthread_mutex_lock(&cell_output_lock);
  strlcpy(cell_at_cmds_output, hcom_cell_str(output), sizeof(cell_at_cmds_output));
  pthread_mutex_unlock(&cell_output_lock);
}

//====================================================================
// Put a serial device or pty into raw mode. COM1 (/dev/ttyS0) is also the
// NuttX serial console, which defaults to echo, canonical (line) mode and
// CR/LF translation; echo would send everything the modem says straight back
// to it. The settings belong to the device, so they stay in force for later
// opens of it.
static int hcom_cell_set_raw(int fd)
{
  struct termios tio;

  if (tcgetattr(fd, &tio) < 0)
  {
    return -errno;
  }

  cfmakeraw(&tio);
  if (tcsetattr(fd, TCSANOW, &tio) < 0)
  {
    return -errno;
  }

  return OK;
}

//====================================================================
// Send one AT command and collect the reply, until the final result code or
// the timeout. The reply is appended to out (always NUL terminated), so a
// sequence of commands can share one buffer. Returns OK, -EIO for ERROR or
// +CME ERROR, or -ETIMEDOUT.
//
// The descriptor must be non-blocking. Reads are done in bulk, once for each
// poll(): a read of a pty that has just been emptied can block even with
// O_NONBLOCK.
static int hcom_cell_at(int fd, const char *command, int timeout_ms, char *out, size_t out_size)
{
  char line[160];
  size_t used = strlen(out);
  size_t start = used;
  struct timespec begin;
  struct timespec now;
  struct pollfd pfd;
  int elapsed_ms = 0;

  snprintf(line, sizeof(line), "%s\r", command);
  if (write(fd, line, strlen(line)) < 0)
  {
    return -errno;
  }

  pfd.fd = fd;
  pfd.events = POLLIN;
  clock_gettime(CLOCK_MONOTONIC, &begin);
  while (elapsed_ms < timeout_ms)
  {
    pfd.revents = 0;
    poll(&pfd, 1, 100);
    clock_gettime(CLOCK_MONOTONIC, &now);
    elapsed_ms = (int)(((now.tv_sec - begin.tv_sec) * 1000) + ((now.tv_nsec - begin.tv_nsec) / 1000000));
    if ((pfd.revents & POLLIN) == 0)
    {
      continue;
    }

    char chunk[64];
    ssize_t got = read(fd, chunk, sizeof(chunk));
    for (ssize_t index = 0; index < got; index++)
    {
      if ((used + 1) < out_size)
      {
        out[used++] = chunk[index];
        out[used] = '\0';
      }
    }

    // Only look at this command's reply, and only at complete lines.
    if ((used > start) && (out[used - 1] == '\n'))
    {
      if (strstr(out + start, "\r\nOK\r\n") != NULL)
      {
        return OK;
      }
      if (strstr(out + start, "ERROR") != NULL)
      {
        return -EIO;
      }
    }
  }

  return -ETIMEDOUT;
}

//====================================================================
// The connect script for the modem, run by pppd on the PPP channel. Unlike the
// scripts used before CMUX, it does not reset the modem (AT+CFUN=1,1): that
// would end CMUX. The disconnect script hangs up.
static int hcom_cell_create_scripts(const cell_settings_t *settings, char *connect_script, char *disconnect_script)
{
  const char *pap_user = hcom_cell_str(settings->pap_user);
  const char *pap_password = hcom_cell_str(settings->pap_password);
  const char *operator = hcom_cell_str(settings->operator);
  const char *mode = hcom_cell_str(settings->mode);
  const char *timeout = hcom_cell_str(settings->timeout);
  const char *apn = hcom_cell_str(settings->apn);
  char authentication_cmd[AUTHENTICATION_CMD_MAX_SIZE];
  char operator_selection_cmd[OPERATOR_SELECTION_CMD_MAX_SIZE];
  char qicsgp_cmd[QICSGP_CMD_MAX_SIZE];

  if (timeout[0] == '\0')
  {
    timeout = "30";
  }

  snprintf_chk(authentication_cmd, AUTHENTICATION_CMD_MAX_SIZE,
      ((pap_user[0] != '\0') && (pap_password[0] != '\0'))
          ? "AT+CGAUTH=1,1,\\\"%s\\\",\\\"%s\\\" PAUSE 3 OK "
          : "",
      pap_user,
      pap_password
  );

  // If the carrier operator code or the network operator mode is missing, the
  // automatic network selection will be used (AT+COPS=0)
  snprintf_chk(operator_selection_cmd, OPERATOR_SELECTION_CMD_MAX_SIZE,
      ((operator[0] != '\0') && (mode[0] != '\0'))
          ? "AT+COPS=1,2,\\\"%s\\\",%s PAUSE 3 OK "
          : "AT+COPS=0 PAUSE 3 OK ",
      operator,
      mode
  );

  switch (settings->module_id)
  {
    case CELL_BG770A_MODULE:
      snprintf_chk(connect_script, CONNECT_SCRIPT_MAX_SIZE,
        "TIMEOUT %s "
        "\"\" AT+CMEE=2 "
        "PAUSE 3 "
        "OK AT+CGDCONT=1,\\\"IP\\\",\\\"%s\\\" "
        "PAUSE 3 "
        "OK %s%s"
        "ATD*99# "
        "CONNECT \\c",
        timeout, apn, authentication_cmd, operator_selection_cmd);
      break;

    case CELL_M95_MODULE:
      snprintf_chk(connect_script, CONNECT_SCRIPT_MAX_SIZE,
        "TIMEOUT %s "
        "\"\" AT+QACCM=0,0 "
        "PAUSE 3 "
        "OK AT+CGDCONT=1,\\\"IP\\\",\\\"%s\\\" "
        "PAUSE 3 "
        "OK ATD*99# "
        "CONNECT \\c",
        timeout, apn);
      break;

    case CELL_BG95M3_MODULE:
      // The BG95-M3 answers ERROR to AT+CGAUTH. Its own command, AT+QICSGP,
      // sets the context's APN and credentials (context 1, IPv4, then the
      // authentication: 3 = PAP or CHAP).
      snprintf_chk(qicsgp_cmd, QICSGP_CMD_MAX_SIZE,
          ((pap_user[0] != '\0') && (pap_password[0] != '\0'))
              ? "AT+QICSGP=1,1,\\\"%s\\\",\\\"%s\\\",\\\"%s\\\",3 PAUSE 3 OK "
              : "",
          apn,
          pap_user,
          pap_password
      );
      snprintf_chk(connect_script, CONNECT_SCRIPT_MAX_SIZE,
        "TIMEOUT %s "
        "\"\" AT+CMEE=2 "
        "PAUSE 3 "
        "OK AT+CGDCONT=1,\\\"IP\\\",\\\"%s\\\" "
        "PAUSE 3 "
        "OK %s%s"
        "ATD*99# "
        "CONNECT \\c",
        timeout, apn, qicsgp_cmd, operator_selection_cmd);
      break;

    case CELL_EG21GL_MODULE:
      snprintf_chk(connect_script, CONNECT_SCRIPT_MAX_SIZE,
        "TIMEOUT %s "
        "\"\" AT+CMEE=2 "
        "PAUSE 3 "
        "OK AT+CGDCONT=1,\\\"IP\\\",\\\"%s\\\" "
        "PAUSE 3 "
        "OK %s"
        "ATD*99# "
        "CONNECT \\c",
        timeout, apn, operator_selection_cmd);
      break;

    default:
      return -EINVAL;
  }

  snprintf_chk(disconnect_script, DISCONNECT_SCRIPT_MAX_SIZE, "\"\" ATH OK \\c");
  return OK;
}

//====================================================================
// GNSS request: turn the receiver on, wait for a fix (or the timeout), read
// the NMEA sentences and turn it off again. The GPIO 64 commands switch the
// GNSS antenna supply, as in the scripts used before CMUX.
static int hcom_cell_gnss(int fd, char *out, size_t out_size)
{
  int ret;
  char setup[256];

  // Only the NMEA reads go into the output: the managed buffer is 1024 bytes.
  setup[0] = '\0';
  hcom_cell_at(fd, "AT+QGPS=1,2,180,1", CELL_AT_TIMEOUT_MS, setup, sizeof(setup));
  hcom_cell_at(fd, "AT+QCFG=\"gpio\",1,64,1,0,0,1", CELL_AT_TIMEOUT_MS, setup, sizeof(setup));
  hcom_cell_at(fd, "AT+QCFG=\"gpio\",3,64,1,1", CELL_AT_TIMEOUT_MS, setup, sizeof(setup));
  hcom_cell_at(fd, "AT+QGPSCFG=\"nmeasrc\",1", CELL_AT_TIMEOUT_MS, setup, sizeof(setup));

  // Wait for the fix by the clock: while the BG95 acquires a fix with the data
  // link up it can stop answering for minutes, and each unanswered poll takes
  // the full AT timeout.
  struct timespec begin;
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &begin);
  now = begin;
  for (int waited = 0; waited < CELL_GNSS_FIX_TIMEOUT_S; waited = (int)(now.tv_sec - begin.tv_sec))
  {
    char gga[192];
    gga[0] = '\0';
    hcom_cell_at(fd, "AT+QGPSGNMEA=\"GGA\"", CELL_AT_TIMEOUT_MS, gga, sizeof(gga));

    // The fix quality is field 6 of GGA; 0 (or empty) means no fix yet.
    const char *field = strstr(gga, "$GPGGA");
    for (int commas = 0; (field != NULL) && (*field != '\0') && (commas < 6); field++)
    {
      if (*field == ',')
      {
        commas++;
      }
    }
    if ((field != NULL) && (*field >= '1') && (*field <= '9'))
    {
      syslog(LOG_INFO, "%s-%d-Cell GNSS fix after %d s\n", __FILE__, __LINE__, waited);
      break;
    }
    sleep(CELL_GNSS_POLL_S);
    clock_gettime(CLOCK_MONOTONIC, &now);
  }

  hcom_cell_at(fd, "AT+QGPSGNMEA=\"GSV\"", CELL_AT_TIMEOUT_MS, out, out_size);
  hcom_cell_at(fd, "AT+QGPSGNMEA=\"GGA\"", CELL_AT_TIMEOUT_MS, out, out_size);
  ret = hcom_cell_at(fd, "AT+QGPSGNMEA=\"RMC\"", CELL_AT_TIMEOUT_MS, out, out_size);
  hcom_cell_at(fd, "AT+QGPSGNMEA=\"GSA\"", CELL_AT_TIMEOUT_MS, out, out_size);
  hcom_cell_at(fd, "AT+QGPSGNMEA=\"VTG\"", CELL_AT_TIMEOUT_MS, out, out_size);
  setup[0] = '\0';
  hcom_cell_at(fd, "AT+QGPSEND", CELL_AT_TIMEOUT_MS, setup, sizeof(setup));
  hcom_cell_at(fd, "AT+QCFG=\"gpio\",1,64,1,0,0,1", CELL_AT_TIMEOUT_MS, setup, sizeof(setup));
  hcom_cell_at(fd, "AT+QCFG=\"gpio\",3,64,0,1", CELL_AT_TIMEOUT_MS, setup, sizeof(setup));
  return ret;
}

//====================================================================
// Signal quality request. +CSQ: 99 means "not known", which the modem can
// report for a short time (for example straight after registering): retry.
static int hcom_cell_signal_quality(int fd, char *out, size_t out_size)
{
  int ret = -ETIMEDOUT;

  for (int attempt = 0; attempt < CELL_CSQ_ATTEMPTS; attempt++)
  {
    out[0] = '\0';
    ret = hcom_cell_at(fd, "AT+CSQ", CELL_AT_TIMEOUT_MS, out, out_size);
    if ((ret == OK) && (strstr(out, "+CSQ: 99,") == NULL))
    {
      break;
    }
    sleep(2);
  }
  return ret;
}

//====================================================================
// Run a request posted by the managed API and raise the AT command event
// with its output, whether it worked or not, so the caller stops waiting.
static void hcom_cell_run_request(int fd, int request)
{
  static char output[CELL_AT_CMDS_OUTPUT_MAX_SIZE];
  struct timespec begin;
  struct timespec end;
  int ret = -EINVAL;

  output[0] = '\0';
  clock_gettime(CLOCK_MONOTONIC, &begin);

  if ((request & CELL_AT_CMD_GPS) != 0)
  {
    ret = hcom_cell_gnss(fd, output, sizeof(output));
  }
  if ((ret == -EINVAL) && ((request & CELL_AT_CMD_SIGNAL_QUALITY) != 0))
  {
    ret = hcom_cell_signal_quality(fd, output, sizeof(output));
  }
  if ((ret == -EINVAL) && ((request & CELL_AT_CMD_SCAN) != 0))
  {
    ret = hcom_cell_at(fd, "AT+COPS=?", CELL_SCAN_TIMEOUT_MS, output, sizeof(output));
  }

  clock_gettime(CLOCK_MONOTONIC, &end);
  syslog(LOG_INFO, "%s-%d-Cell request %d done, ret=%d, %ld s, %zu characters\n", __FILE__, __LINE__,
         request, ret, (long)(end.tv_sec - begin.tv_sec), strlen(output));

  hcom_cell_publish_output(output);
  hcom_cell_raise_event(ESPCP_CELL_AT_CMD_EVENT,
                        (ret == OK) ? ESPCP_COMPLETED_OK_STATUS_CODE : ESPCP_FAILURE_STATUS_CODE);
}

//====================================================================
// The link has come up: point the resolver at the configured DNS servers
// (stock pppd does not ask the network for its servers), start the network
// time client, publish the modem identity and signal quality for the managed
// API and raise the connected event.
static void hcom_cell_link_up(int fd, const struct in_addr *address)
{
  char text[INET_ADDRSTRLEN];
  char message[HCOM_MED_SHORT_HOST_STRING_BUFF_LENGTH];
  static char info[CELL_AT_CMDS_OUTPUT_MAX_SIZE];

  inet_ntop(AF_INET, address, text, sizeof(text));
  syslog(LOG_INFO, "%s-%d-Cell network has been successfully connected, IP address %s\n", __FILE__, __LINE__, text);
  snprintf_chk(message, sizeof(message), "Connection established successfully! IP address '%s'.\n", text);
  hcom_cell_host_message(message);

  meadow_configuration_t *config = meadow_os_deep_copy_config();
  bool get_time = (config != NULL) && (config->get_network_time_at_startup != 0);
  hcom_common_utils_erase_dns_resolver_file();
  if (config != NULL)
  {
    hcom_common_utils_add_servers_to_dns_resolver_file(config->dns_servers, config->dns_servers_count);
  }
  meadow_os_config_free_resources(config);

  if (get_time)
  {
    ntpc_start();
  }

  // The managed side reads the IMEI ("+GSN\r\r\n<digits>", so the echo is
  // needed) and the signal quality from this output at the connected event.
  info[0] = '\0';
  hcom_cell_at(fd, "AT+GSN", CELL_AT_TIMEOUT_MS, info, sizeof(info));
  hcom_cell_at(fd, "AT+QCSQ", CELL_AT_TIMEOUT_MS, info, sizeof(info));
  hcom_cell_at(fd, "AT+CSQ", CELL_AT_TIMEOUT_MS, info, sizeof(info));
  hcom_cell_publish_output(info);

  cell_connected = true;
  int result = hcom_cell_raise_event(ESPCP_CELL_CONNECTED_EVENT, ESPCP_COMPLETED_OK_STATUS_CODE);
  syslog(LOG_INFO, "%s-%d-Cell connected event message result: %d\n", __FILE__, __LINE__, result);
}

//====================================================================
// While the link is down, report the modem's view to the host now and then:
// signal quality, registration and operator.
static void hcom_cell_report_link_down(int fd)
{
  static char report[CELL_AT_CMDS_OUTPUT_MAX_SIZE];

  report[0] = '\0';
  hcom_cell_at(fd, "AT+CSQ", CELL_AT_TIMEOUT_MS, report, sizeof(report));
  hcom_cell_at(fd, "AT+CEREG?", CELL_AT_TIMEOUT_MS, report, sizeof(report));
  hcom_cell_at(fd, "AT+COPS?", CELL_AT_TIMEOUT_MS, report, sizeof(report));
  syslog(LOG_INFO, "%s-%d-Cell link down, modem status: %s\n", __FILE__, __LINE__, report);
  hcom_cell_host_message("Cell link not up yet. Modem status:\n");
  hcom_cell_host_message(report);
}

//====================================================================
// The CellAT thread: owns the AT channel, follows the link state and runs the
// managed requests.
static void *hcom_cell_at_thread(void *arg)
{
  (void)arg;

#if HCOM_DIAG_OUTPUT_SYSLOG_PID_OF_NEW_THREADS > 0
  syslog(LOG_MDIAG, "New pthread [Thrd:%d][Task:%d] %s\n",
     gettid(), getpid(), "hcom_cell_at_thread");
#endif

  int fd = open(cell_at_pty, O_RDWR | O_NONBLOCK);
  if (fd < 0)
  {
    syslog(LOG_ERR, "%s-%d-Failed to open the AT channel %s, errno=%d\n", __FILE__, __LINE__, cell_at_pty, errno);
    return NULL;
  }
  hcom_cell_set_raw(fd);

  static char reply[128];
  reply[0] = '\0';
  int ret = hcom_cell_at(fd, "ATE1", CELL_AT_TIMEOUT_MS, reply, sizeof(reply));
  syslog(LOG_INFO, "%s-%d-Cell AT channel %s ready: %d\n", __FILE__, __LINE__, cell_at_pty, ret);

  int seconds_down = 0;
  while (true)
  {
    struct in_addr address;
    address.s_addr = 0;
    bool up = (netlib_get_ipv4addr("ppp0", &address) == OK) && (address.s_addr != 0);

    if (up && !cell_connected)
    {
      hcom_cell_link_up(fd, &address);
      seconds_down = 0;
    }
    if (!up && cell_connected)
    {
      cell_connected = false;
      cell_err = CELL_PPPD_LOST_CONNECTION_ERR;
      syslog(LOG_INFO, "%s-%d-Cell network has been disconnected\n", __FILE__, __LINE__);
      hcom_cell_raise_event(ESPCP_CELL_DISCONNECTED_EVENT, ESPCP_FAILURE_STATUS_CODE);
      hcom_cell_raise_event(ESPCP_CELL_CONNECTING_EVENT, ESPCP_COMPLETED_OK_STATUS_CODE);
    }
    if (!up)
    {
      seconds_down++;
      if ((seconds_down % CELL_LINK_DOWN_REPORT_S) == 0)
      {
        hcom_cell_report_link_down(fd);
      }
    }

    int request = __atomic_exchange_n(&cell_pending_request, 0, __ATOMIC_SEQ_CST);
    if (request != 0)
    {
      hcom_cell_run_request(fd, request);
    }

    sleep(1);
  }

  return NULL;
}

//====================================================================
// The pty that the next openpty() will create: CMUX creates one pty per
// channel, in order, starting from the lowest free one.
static int hcom_cell_next_free_pty(void)
{
  char path[CELL_PTY_PATH_SIZE];
  struct stat st;

  for (int index = 0; index < CELL_MAX_PTYS; index++)
  {
    snprintf(path, sizeof(path), "/dev/pts/%d", index);
    if (stat(path, &st) < 0)
    {
      return index;
    }
  }
  return -ENOENT;
}

//====================================================================
// Wait for the modem after power-on (about 10 s to APP RDY) on the raw UART.
static int hcom_cell_wait_for_modem(const char *ttyname)
{
  int fd = open(ttyname, O_RDWR | O_NONBLOCK);
  if (fd < 0)
  {
    return -errno;
  }

  int ret = hcom_cell_set_raw(fd);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s-%d-Failed to set %s to raw mode, ret=%d\n", __FILE__, __LINE__, ttyname, ret);
  }

  ret = -ETIMEDOUT;
  for (int attempt = 0; (attempt < CELL_MODEM_READY_TIMEOUT_S) && (ret != OK); attempt++)
  {
    char reply[64];
    reply[0] = '\0';
    ret = hcom_cell_at(fd, "AT", 1000, reply, sizeof(reply));
  }

  close(fd);
  return ret;
}

//====================================================================
static void hcom_cell_failed(hcom_cell_err_t err, const char *text)
{
  syslog(LOG_ERR, "%s-%d-Cell start failed: %s\n", __FILE__, __LINE__, text);
  hcom_cell_host_message(text);
  cell_err = err;
  hcom_cell_raise_event(ESPCP_CELL_ERROR_EVENT, ESPCP_FAILURE_STATUS_CODE);
}

//====================================================================
// The CellPPPD thread: brings up CMUX, starts the CellAT thread and runs pppd
// on the PPP channel. pppd() only returns if it cannot start.
static void *hcom_cell_pppd_thread(void *arg)
{
  (void)arg;
  static char connect_script[CONNECT_SCRIPT_MAX_SIZE];
  static char disconnect_script[DISCONNECT_SCRIPT_MAX_SIZE];
  const char *ttyname = hcom_cell_str(cell_settings->ttyname);

#if HCOM_DIAG_OUTPUT_SYSLOG_PID_OF_NEW_THREADS > 0
  syslog(LOG_MDIAG, "New pthread [Thrd:%d][Task:%d] %s\n",
     gettid(), getpid(), "hcom_cell_pppd_thread");
#endif

  if (hcom_cell_create_scripts(cell_settings, connect_script, disconnect_script) < 0)
  {
    hcom_cell_failed(CELL_INVALID_MODEM_ERR, "Cell: unsupported cell module, check the cell configuration file.");
    return NULL;
  }

  int ret = hcom_cell_wait_for_modem(ttyname);
  if (ret < 0)
  {
    hcom_cell_failed(CELL_PPPD_TIMEOUT_ERR, "Cell: the modem does not answer AT commands.");
    return NULL;
  }

  int pty = hcom_cell_next_free_pty();
  static char cmux_script[] = CELL_CMUX_SCRIPT;
  struct cmux_settings_s cmux =
  {
    .tty_name = ttyname,
    .script = cmux_script,
    .total_channels = CELL_CMUX_CHANNELS,
  };
  ret = (pty < 0) ? pty : cmux_create(&cmux);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s-%d-cmux_create failed: %d\n", __FILE__, __LINE__, ret);
    hcom_cell_failed(CELL_PPPD_THREAD_ERR, "Cell: the modem did not start CMUX.");
    return NULL;
  }

  snprintf(cell_ppp_pty, sizeof(cell_ppp_pty), "/dev/pts/%d", pty + CELL_DLCI_PPP);
  snprintf(cell_at_pty, sizeof(cell_at_pty), "/dev/pts/%d", pty + CELL_DLCI_AT);
  syslog(LOG_INFO, "%s-%d-Cell CMUX started: PPP on %s, AT commands on %s\n", __FILE__, __LINE__,
         cell_ppp_pty, cell_at_pty);

  pthread_t at_thread;
  pthread_attr_t attr;
  struct sched_param param;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, HCOM_THREAD_STACKSIZE_CELL_AT);
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
  param.sched_priority = HCOM_THREAD_PRIORITY_CELL_AT;
  pthread_attr_setschedparam(&attr, &param);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  ret = pthread_create(&at_thread, &attr, hcom_cell_at_thread, NULL);
  pthread_attr_destroy(&attr);
  if (ret == OK)
  {
    (void) pthread_setname_np(at_thread, HCOM_THREAD_NAME_CELL_AT);
  }
  else
  {
    syslog(LOG_ERR, "%s-%d-Failed to start the cell AT thread: %d\n", __FILE__, __LINE__, ret);
  }

  // pppd opens the PPP channel itself; make it raw first, as for the UART.
  int fd = open(cell_ppp_pty, O_RDWR | O_NONBLOCK);
  if (fd >= 0)
  {
    hcom_cell_set_raw(fd);
    close(fd);
  }

  struct pppd_settings_s pppd_settings;
  memset(&pppd_settings, 0, sizeof(pppd_settings));
  strlcpy(pppd_settings.ttyname, cell_ppp_pty, sizeof(pppd_settings.ttyname));
  pppd_settings.connect_script = connect_script;
  pppd_settings.disconnect_script = disconnect_script;

  syslog(LOG_INFO, "%s-%d-Starting PPPD, connect script: %s\n", __FILE__, __LINE__, connect_script);
  hcom_cell_raise_event(ESPCP_CELL_CONNECTING_EVENT, ESPCP_COMPLETED_OK_STATUS_CODE);
  ret = pppd(&pppd_settings);

  hcom_cell_failed(CELL_PPPD_THREAD_ERR, "Cell: PPPD could not start.");
  syslog(LOG_ERR, "%s-%d-pppd returned %d\n", __FILE__, __LINE__, ret);
  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

//====================================================================
// Called by the startup manager: if the cell interface is selected, start the
// CellPPPD thread, which brings the cell link up.
int hcom_cell_start(void)
{
  int ret;
  pthread_t thread;
  meadow_configuration_t *config = meadow_os_deep_copy_config();

  if ((config == NULL) || (config->default_interface == NULL) ||
      (config->default_interface->interface_type != MEADOW_IFT_CELL))
  {
    meadow_os_config_free_resources(config);
    return OK;
  }

  syslog(LOG_INFO, "%s-%d-Attempting to start the cell network\n", __FILE__, __LINE__);
  if (config->default_cell_settings == NULL)
  {
    syslog(LOG_ERR, "%s-%d-Failed to get the cell settings\n", __FILE__, __LINE__);
    hcom_cell_host_message("Invalid cell settings. Please check your cell configuration file.");
    meadow_os_config_free_resources(config);
    return -ENOENT;
  }

  // The threads use this copy for as long as they run.
  cell_settings = hcom_cell_copy_settings(config->default_cell_settings);
  meadow_os_config_free_resources(config);
  if (cell_settings == NULL)
  {
    syslog(LOG_ERR, "%s-%d-Failed to copy the cell settings\n", __FILE__, __LINE__);
    return -ENOMEM;
  }

  syslog(LOG_INFO, "%s-%d-cell module: %s (%" PRIu32 "), apn: %s, operator: %s, mode: %s, ttyname: %s, timeout: %s, user: %s, password: %s\n",
         __FILE__, __LINE__, hcom_cell_str(cell_settings->module), cell_settings->module_id,
         hcom_cell_str(cell_settings->apn), hcom_cell_str(cell_settings->operator),
         hcom_cell_str(cell_settings->mode), hcom_cell_str(cell_settings->ttyname),
         hcom_cell_str(cell_settings->timeout), hcom_cell_str(cell_settings->pap_user),
         ((cell_settings->pap_password != NULL) && (cell_settings->pap_password[0] != '\0')) ? "set" : "not set");

  if (cell_settings->scan_mode)
  {
    hcom_cell_host_message("ScanMode config has been deprecated! Consult how to use the network scanner on Meadow cellular docs.");
  }

  pthread_attr_t attr;
  struct sched_param param;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, HCOM_THREAD_STACKSIZE_CELL_PPPD);
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
  param.sched_priority = HCOM_THREAD_PRIORITY_CELL_PPPD;
  pthread_attr_setschedparam(&attr, &param);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  ret = pthread_create(&thread, &attr, hcom_cell_pppd_thread, NULL);
  pthread_attr_destroy(&attr);
  if (ret != OK)
  {
    syslog(LOG_ERR, "%s@%d-The task to run PPPD failed in create\n", __FILE__, __LINE__);
    hcom_cell_free_settings(cell_settings);
    cell_settings = NULL;
    cell_err = CELL_PPPD_THREAD_ERR;
    hcom_cell_raise_event(ESPCP_CELL_ERROR_EVENT, ESPCP_FAILURE_STATUS_CODE);
    return -ret;
  }

  (void) pthread_setname_np(thread, HCOM_THREAD_NAME_CELL_PPPD);
  syslog(LOG_INFO, "%s@%d-PPPD thread launched\n", __FILE__, __LINE__);
  return OK;
}

bool meadow_cell_is_connected(void)
{
  return cell_connected;
}

//====================================================================
// A managed request (CELL_AT_CMD_* bits) runs on the AT channel without
// touching the link. 0 means "resume", which needs nothing here. A request
// made while another is still pending replaces it; the managed API makes one
// request at a time.
void meadow_cell_change_state(int state)
{
  if ((state & (CELL_AT_CMD_GPS | CELL_AT_CMD_SIGNAL_QUALITY | CELL_AT_CMD_SCAN)) != 0)
  {
    __atomic_store_n(&cell_pending_request, state, __ATOMIC_SEQ_CST);
  }
  syslog(LOG_INFO, "%s-%d-Cell request: %d\n", __FILE__, __LINE__, state);
}

int meadow_get_cell_at_cmds_output(unsigned char *buf)
{
  pthread_mutex_lock(&cell_output_lock);
  size_t len = strlen(cell_at_cmds_output) + 1;
  memcpy(buf, cell_at_cmds_output, len);
  pthread_mutex_unlock(&cell_output_lock);

  return (int)len;
}

int meadow_get_cell_error(void)
{
  return (int)cell_err;
}
