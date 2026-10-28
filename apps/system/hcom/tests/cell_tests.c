/****************************************************************************
 * apps/system/hcom/tests/cell_tests.c
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
//  Native cell tests (build.sh --unittests=cell, developer test 20).
//
//  These test the OS side of the cell network quickly, without Mono. The
//  managed side is tested by tests/managed/CellTests.
//
//  Without a modem (always run):
//    - the connect scripts for each module;
//    - the AT reply parser, with the test playing the modem on a pty pair;
//    - GGA fix detection;
//    - CMUX (apps/netutils/cmux), with the test playing a CMUX modem on a pty
//      pair: whole, split and back-to-back frames, a bad FCS, CR delivered
//      unchanged, the control channel answered, and channel to modem framing,
//      including data longer than one frame (split at N1, partial writes).
//
//  With the cell link running (developer test value 1, a few bytes of data):
//    - the link is up, a DNS lookup works and a signal quality request through
//      the native API returns +CSQ.
//
//  CMUX has no tear-down, so each run leaves a CMUX thread and three ptys
//  behind; restart the board after a few runs.
//

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <nuttx/config.h>

#if (defined(CONFIG_CELL_TESTS) || defined(CONFIG_ALL_MEADOW_TESTS)) && defined(CONFIG_MEADOW_CELL_INCLUDE_IN_BUILD)

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <pty.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/crc8.h>

#include "netutils/cmux.h"

#include "../hcom_common.h"
#include "../cell/hcom_cell.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CELL_TEST_PATH_SIZE 32
#define CELL_TEST_BUFFER_SIZE 512
#define CELL_TEST_CMUX_CHANNELS 3
#define CELL_TEST_UIH 0xef
#define CELL_TEST_LIVE_CONNECT_TIMEOUT_S 120
#define CELL_TEST_LIVE_REQUEST_TIMEOUT_S 30

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int cell_test_passes;
static int cell_test_failures;

static char cell_test_buffer[CELL_TEST_BUFFER_SIZE];
static char cell_test_connect[CELL_TEST_BUFFER_SIZE];
static char cell_test_disconnect[64];
static unsigned char cell_test_frame[CELL_TEST_BUFFER_SIZE];
static unsigned char cell_test_output[CELL_AT_CMDS_OUTPUT_MAX_SIZE];
static unsigned char cell_test_previous[CELL_AT_CMDS_OUTPUT_MAX_SIZE];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

//====================================================================
static void cell_test_check(const char *name, bool ok)
{
  if (ok)
  {
    cell_test_passes++;
  }
  else
  {
    cell_test_failures++;
  }
  syslog(LOG_MTEST, "    %s: %s\n", name, ok ? "PASS" : "FAIL");
}

//====================================================================
static int cell_test_elapsed_ms(const struct timespec *begin)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (int)(((now.tv_sec - begin->tv_sec) * 1000) + ((now.tv_nsec - begin->tv_nsec) / 1000000));
}

//====================================================================
static void cell_test_set_raw(int fd)
{
  struct termios tio;
  if (tcgetattr(fd, &tio) == 0)
  {
    cfmakeraw(&tio);
    tcsetattr(fd, TCSANOW, &tio);
  }
}

static void cell_test_set_nonblocking(int fd)
{
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0)
  {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

//====================================================================
// Read whatever arrives on fd for the given time (fd must be non-blocking).
static size_t cell_test_read_for(int fd, unsigned char *buffer, size_t size, int duration_ms)
{
  struct timespec begin;
  struct pollfd pfd;
  size_t used = 0;

  clock_gettime(CLOCK_MONOTONIC, &begin);
  pfd.fd = fd;
  pfd.events = POLLIN;
  while ((cell_test_elapsed_ms(&begin) < duration_ms) && (used < size))
  {
    pfd.revents = 0;
    poll(&pfd, 1, 50);
    if ((pfd.revents & POLLIN) != 0)
    {
      ssize_t got = read(fd, buffer + used, size - used);
      if (got > 0)
      {
        used += (size_t)got;
      }
    }
  }
  return used;
}

//====================================================================
// A pty pair, both sides raw and non-blocking.
static int cell_test_open_pty(int *master, int *slave, char *name)
{
  if (openpty(master, slave, name, NULL, NULL) < 0)
  {
    return -errno;
  }
  cell_test_set_raw(*master);
  cell_test_set_raw(*slave);
  cell_test_set_nonblocking(*master);
  cell_test_set_nonblocking(*slave);
  return OK;
}

//====================================================================
// A write made later by a helper thread, to deliver a reply in two parts.
struct cell_test_delayed_write_s
{
  int fd;
  const char *data;
  int delay_ms;
};

static void *cell_test_delayed_write_thread(void *arg)
{
  struct cell_test_delayed_write_s *job = (struct cell_test_delayed_write_s *)arg;
  usleep(job->delay_ms * 1000);
  write(job->fd, job->data, strlen(job->data));
  return NULL;
}

/****************************************************************************
 * Connect scripts
 ****************************************************************************/

static bool cell_test_contains(const char *text, const char *part)
{
  return strstr(text, part) != NULL;
}

static int cell_test_scripts(uint32_t module_id, char *apn, char *user, char *password, char *operator, char *mode)
{
  cell_settings_t settings;
  memset(&settings, 0, sizeof(settings));
  settings.module_id = module_id;
  settings.apn = apn;
  settings.pap_user = user;
  settings.pap_password = password;
  settings.operator = operator;
  settings.mode = mode;
  return hcom_cell_test_create_scripts(&settings, cell_test_connect, sizeof(cell_test_connect),
                                       cell_test_disconnect, sizeof(cell_test_disconnect));
}

static void cell_test_connect_scripts(void)
{
  syslog(LOG_MTEST, "  Connect scripts\n");

  int ret = cell_test_scripts(CELL_BG95M3_MODULE, "internet", "user", "secret", NULL, NULL);
  cell_test_check("BG95 script created", ret == OK);
  cell_test_check("BG95 sends credentials with AT+QICSGP",
                  cell_test_contains(cell_test_connect, "AT+QICSGP=1,1,\\\"internet\\\",\\\"user\\\",\\\"secret\\\",3"));
  cell_test_check("BG95 does not send AT+CGAUTH", !cell_test_contains(cell_test_connect, "CGAUTH"));
  cell_test_check("BG95 does not reset the modem (CMUX)", !cell_test_contains(cell_test_connect, "CFUN"));
  cell_test_check("BG95 sets the APN", cell_test_contains(cell_test_connect, "AT+CGDCONT=1,\\\"IP\\\",\\\"internet\\\""));
  cell_test_check("Default timeout is 30 s", strncmp(cell_test_connect, "TIMEOUT 30 ", 11) == 0);
  cell_test_check("Automatic operator selection", cell_test_contains(cell_test_connect, "AT+COPS=0 "));
  cell_test_check("Script dials and expects CONNECT", cell_test_contains(cell_test_connect, "ATD*99# CONNECT"));
  cell_test_check("Disconnect script hangs up", strcmp(cell_test_disconnect, "\"\" ATH OK \\c") == 0);

  ret = cell_test_scripts(CELL_BG95M3_MODULE, "internet", NULL, NULL, NULL, NULL);
  cell_test_check("BG95 without credentials has no AT+QICSGP",
                  (ret == OK) && !cell_test_contains(cell_test_connect, "QICSGP"));

  ret = cell_test_scripts(CELL_BG770A_MODULE, "internet", "user", "secret", NULL, NULL);
  cell_test_check("BG770A sends credentials with AT+CGAUTH",
                  (ret == OK) && cell_test_contains(cell_test_connect, "AT+CGAUTH=1,1,\\\"user\\\",\\\"secret\\\"") &&
                  !cell_test_contains(cell_test_connect, "QICSGP"));

  ret = cell_test_scripts(CELL_BG95M3_MODULE, "internet", NULL, NULL, "23433", "8");
  cell_test_check("Manual operator selection",
                  (ret == OK) && cell_test_contains(cell_test_connect, "AT+COPS=1,2,\\\"23433\\\",8"));

  ret = cell_test_scripts(CELL_BG95M3_MODULE, NULL, NULL, NULL, NULL, NULL);
  cell_test_check("Missing settings are treated as empty",
                  (ret == OK) && cell_test_contains(cell_test_connect, "AT+CGDCONT=1,\\\"IP\\\",\\\"\\\""));

  ret = cell_test_scripts(CELL_M95_MODULE, "internet", NULL, NULL, NULL, NULL);
  cell_test_check("M95 script created", (ret == OK) && cell_test_contains(cell_test_connect, "AT+QACCM=0,0"));

  ret = cell_test_scripts(99, "internet", NULL, NULL, NULL, NULL);
  cell_test_check("Unknown module is rejected", ret == -EINVAL);
}

/****************************************************************************
 * GGA fix detection
 ****************************************************************************/

static void cell_test_gga(void)
{
  syslog(LOG_MTEST, "  GGA fix detection\n");
  cell_test_check("Fix quality 1 is a fix",
      hcom_cell_test_gga_has_fix("\r\n+QGPSGNMEA: $GPGGA,141950.00,5000.000000,N,00100.000000,W,1,09,1.0,10.7,M,47.1,M,,*48\r\n"));
  cell_test_check("Fix quality 0 is not a fix",
      !hcom_cell_test_gga_has_fix("\r\n+QGPSGNMEA: $GPGGA,,,,,,0,,,,,,,,*66\r\n\r\nOK\r\n"));
  cell_test_check("No GGA sentence is not a fix", !hcom_cell_test_gga_has_fix("\r\nOK\r\n"));
  cell_test_check("Truncated GGA is not a fix", !hcom_cell_test_gga_has_fix("$GPGGA,1,2"));
}

/****************************************************************************
 * AT reply parser
 ****************************************************************************/

// Queue a reply on the modem side (and optionally a second part 200 ms later),
// then run one command.
static int cell_test_at_case(int master, int slave, const char *reply, const char *later, int timeout_ms)
{
  pthread_t thread;
  struct cell_test_delayed_write_s job;
  bool started = false;

  if (reply != NULL)
  {
    write(master, reply, strlen(reply));
  }
  if (later != NULL)
  {
    job.fd = master;
    job.data = later;
    job.delay_ms = 200;
    started = (pthread_create(&thread, NULL, cell_test_delayed_write_thread, &job) == 0);
  }

  int ret = hcom_cell_test_at(slave, "AT+CSQ", timeout_ms, cell_test_buffer, sizeof(cell_test_buffer));

  if (started)
  {
    pthread_join(thread, NULL);
  }
  // Discard the command the "modem" received.
  cell_test_read_for(master, cell_test_frame, sizeof(cell_test_frame), 50);
  return ret;
}

static void cell_test_at_parser(void)
{
  int master;
  int slave;
  char name[CELL_TEST_PATH_SIZE];
  struct timespec begin;

  syslog(LOG_MTEST, "  AT reply parser\n");
  if (cell_test_open_pty(&master, &slave, name) < 0)
  {
    cell_test_check("Open a pty pair", false);
    return;
  }

  cell_test_buffer[0] = '\0';
  int ret = cell_test_at_case(master, slave, "\r\n+CSQ: 17,99\r\n\r\nOK\r\n", NULL, 2000);
  cell_test_check("OK in one chunk", ret == OK);
  cell_test_check("Reply kept with CR LF", cell_test_contains(cell_test_buffer, "\r\n+CSQ: 17,99\r\n"));

  cell_test_buffer[0] = '\0';
  ret = cell_test_at_case(master, slave, "\r\nO", "K\r\n", 2000);
  cell_test_check("OK split across two writes", ret == OK);

  cell_test_buffer[0] = '\0';
  ret = cell_test_at_case(master, slave, "\r\nERROR\r\n", NULL, 2000);
  cell_test_check("ERROR", ret == -EIO);

  cell_test_buffer[0] = '\0';
  ret = cell_test_at_case(master, slave, "\r\n+CME ERROR: 3\r\n", NULL, 2000);
  cell_test_check("+CME ERROR", ret == -EIO);

  cell_test_buffer[0] = '\0';
  clock_gettime(CLOCK_MONOTONIC, &begin);
  ret = cell_test_at_case(master, slave, NULL, NULL, 500);
  int elapsed = cell_test_elapsed_ms(&begin);
  cell_test_check("No reply times out", ret == -ETIMEDOUT);
  cell_test_check("Timeout is bounded", (elapsed >= 400) && (elapsed < 2000));

  // An OK left in the buffer by an earlier command must not count.
  strlcpy(cell_test_buffer, "AT\r\r\nOK\r\n", sizeof(cell_test_buffer));
  ret = cell_test_at_case(master, slave, NULL, NULL, 300);
  cell_test_check("Earlier OK is ignored", ret == -ETIMEDOUT);

  // The command reaches the modem side, terminated by CR.
  cell_test_buffer[0] = '\0';
  write(master, "\r\nOK\r\n", 6);
  hcom_cell_test_at(slave, "AT+CSQ", 1000, cell_test_buffer, sizeof(cell_test_buffer));
  size_t got = cell_test_read_for(master, cell_test_frame, sizeof(cell_test_frame) - 1, 100);
  cell_test_frame[got] = '\0';
  cell_test_check("Command sent with CR", strcmp((char *)cell_test_frame, "AT+CSQ\r") == 0);

  close(slave);
  close(master);
}

/****************************************************************************
 * CMUX
 ****************************************************************************/

// A CMUX basic mode frame, as the modem would send it: flag, address (DLCI, EA),
// control, length, payload, FCS (over address, control and length, computed
// as cmux.c does), flag.
static size_t cell_test_cmux_frame(unsigned char *out, int dlci, unsigned char control,
                                   const unsigned char *payload, size_t length)
{
  size_t used = 0;
  out[used++] = 0xf9;
  out[used++] = (unsigned char)((dlci << 2) | 0x01);
  out[used++] = control;
  out[used++] = (unsigned char)((length << 1) | 0x01);
  memcpy(out + used, payload, length);
  used += length;
  out[used++] = crc8rohcpart(out + 1, 3, 0x00);
  out[used++] = 0xf9;
  return used;
}

// Find the first UIH frame for a DLCI in data read from the modem side and
// return its payload length (or -1).
static int cell_test_find_uih(const unsigned char *data, size_t size, int dlci, unsigned char *payload,
                              size_t payload_size)
{
  for (size_t index = 0; (index + 5) < size; index++)
  {
    if ((data[index] == 0xf9) && (data[index + 1] != 0xf9) && ((data[index + 1] >> 2) == dlci) &&
        ((data[index + 2] & 0xef) == CELL_TEST_UIH))
    {
      size_t length = data[index + 3] >> 1;
      if (((index + 4 + length) <= size) && (length <= payload_size))
      {
        memcpy(payload, data + index + 4, length);
        return (int)length;
      }
    }
  }
  return -1;
}

// The modem's side of cmux_create(): answer each AT command line with OK until
// AT+CMUX has been answered.
struct cell_test_modem_s
{
  int master;
  volatile bool done;
};

static void *cell_test_modem_thread(void *arg)
{
  struct cell_test_modem_s *modem = (struct cell_test_modem_s *)arg;
  char line[64];
  size_t used = 0;
  struct timespec begin;

  clock_gettime(CLOCK_MONOTONIC, &begin);
  while (!modem->done && (cell_test_elapsed_ms(&begin) < 15000))
  {
    char c;
    if (read(modem->master, &c, 1) != 1)
    {
      usleep(10 * 1000);
      continue;
    }
    if ((c == '\r') || (c == '\n'))
    {
      line[used] = '\0';
      if (used > 0)
      {
        write(modem->master, "\r\nOK\r\n", 6);
        if (strncmp(line, "AT+CMUX", 7) == 0)
        {
          modem->done = true;
        }
      }
      used = 0;
    }
    else if (used < (sizeof(line) - 1))
    {
      line[used++] = c;
    }
  }
  return NULL;
}

static int cell_test_next_free_pty(void)
{
  char path[CELL_TEST_PATH_SIZE];
  struct stat st;

  for (int index = 0; index < 32; index++)
  {
    snprintf(path, sizeof(path), "/dev/pts/%d", index);
    if (stat(path, &st) < 0)
    {
      return index;
    }
  }
  return -ENOENT;
}

static void cell_test_cmux(void)
{
  int master;
  int slave;
  char name[CELL_TEST_PATH_SIZE];
  char channel_name[CELL_TEST_PATH_SIZE];
  int channel[CELL_TEST_CMUX_CHANNELS] = { -1, -1, -1 };
  unsigned char payload[64];

  syslog(LOG_MTEST, "  CMUX (simulated modem)\n");
  if (cell_test_open_pty(&master, &slave, name) < 0)
  {
    cell_test_check("Open the modem pty pair", false);
    return;
  }
  // cmux_create() opens the slave side by name itself.
  close(slave);

  int base = cell_test_next_free_pty();

  struct cell_test_modem_s modem;
  modem.master = master;
  modem.done = false;
  pthread_t thread;
  pthread_create(&thread, NULL, cell_test_modem_thread, &modem);

  static char script[] = "TIMEOUT 5 \"\" AT+CMUX=0 OK \\c";
  struct cmux_settings_s settings =
  {
    .tty_name = name,
    .script = script,
    .total_channels = CELL_TEST_CMUX_CHANNELS,
  };
  int ret = cmux_create(&settings);
  modem.done = true;
  pthread_join(thread, NULL);
  cell_test_check("cmux_create", (ret == OK) && (base >= 0));
  if ((ret != OK) || (base < 0))
  {
    close(master);
    return;
  }

  for (int index = 1; index < CELL_TEST_CMUX_CHANNELS; index++)
  {
    snprintf(channel_name, sizeof(channel_name), "/dev/pts/%d", base + index);
    channel[index] = open(channel_name, O_RDWR | O_NONBLOCK);
    if (channel[index] >= 0)
    {
      cell_test_set_raw(channel[index]);
    }
  }
  cell_test_check("Channel ptys exist", (channel[1] >= 0) && (channel[2] >= 0));
  if ((channel[1] < 0) || (channel[2] < 0))
  {
    close(master);
    return;
  }

  // Drop the SABM frames that opened the channels.
  cell_test_read_for(master, cell_test_frame, sizeof(cell_test_frame), 300);

  // A whole frame, with CR LF in the payload: delivered unchanged.
  size_t length = cell_test_cmux_frame(cell_test_frame, 1, CELL_TEST_UIH, (const unsigned char *)"hello\r\n", 7);
  write(master, cell_test_frame, length);
  size_t got = cell_test_read_for(channel[1], payload, sizeof(payload), 300);
  cell_test_check("Whole frame delivered", (got == 7) && (memcmp(payload, "hello\r\n", 7) == 0));
  cell_test_check("CR delivered unchanged (pty master raw)", (got == 7) && (payload[5] == '\r'));

  // A frame split across two writes.
  length = cell_test_cmux_frame(cell_test_frame, 1, CELL_TEST_UIH, (const unsigned char *)"split\r", 6);
  write(master, cell_test_frame, 3);
  usleep(300 * 1000);
  write(master, cell_test_frame + 3, length - 3);
  got = cell_test_read_for(channel[1], payload, sizeof(payload), 300);
  cell_test_check("Frame split across writes delivered", (got == 6) && (memcmp(payload, "split\r", 6) == 0));

  // Two frames for different channels in one write.
  length = cell_test_cmux_frame(cell_test_frame, 1, CELL_TEST_UIH, (const unsigned char *)"one\r", 4);
  length += cell_test_cmux_frame(cell_test_frame + length, 2, CELL_TEST_UIH, (const unsigned char *)"two\r", 4);
  write(master, cell_test_frame, length);
  got = cell_test_read_for(channel[1], payload, sizeof(payload), 300);
  bool first = (got == 4) && (memcmp(payload, "one\r", 4) == 0);
  got = cell_test_read_for(channel[2], payload, sizeof(payload), 300);
  cell_test_check("Two frames in one write delivered", first && (got == 4) && (memcmp(payload, "two\r", 4) == 0));

  // A bad FCS is dropped; the next good frame still gets through.
  length = cell_test_cmux_frame(cell_test_frame, 1, CELL_TEST_UIH, (const unsigned char *)"bad\r", 4);
  cell_test_frame[length - 2] ^= 0xff;
  write(master, cell_test_frame, length);
  got = cell_test_read_for(channel[1], payload, sizeof(payload), 300);
  cell_test_check("Frame with a bad FCS dropped", got == 0);
  length = cell_test_cmux_frame(cell_test_frame, 1, CELL_TEST_UIH, (const unsigned char *)"good\r", 5);
  write(master, cell_test_frame, length);
  got = cell_test_read_for(channel[1], payload, sizeof(payload), 300);
  cell_test_check("Good frame after a bad one delivered", (got == 5) && (memcmp(payload, "good\r", 5) == 0));

  // An MSC command on the control channel is answered: same message, C/R clear.
  static const unsigned char msc[] = { 0xe3, 0x05, 0x07, 0x8b };
  length = cell_test_cmux_frame(cell_test_frame, 0, CELL_TEST_UIH, msc, sizeof(msc));
  write(master, cell_test_frame, length);
  got = cell_test_read_for(master, cell_test_frame, sizeof(cell_test_frame), 500);
  int found = cell_test_find_uih(cell_test_frame, got, 0, payload, sizeof(payload));
  cell_test_check("Control channel MSC answered",
                  (found == 4) && (payload[0] == 0xe1) && (payload[1] == 0x05) && (payload[2] == 0x07) &&
                  (payload[3] == 0x8b));

  // Data written to a channel reaches the modem as a UIH frame for its DLCI.
  write(channel[2], "AT\r", 3);
  got = cell_test_read_for(master, cell_test_frame, sizeof(cell_test_frame), 500);
  found = cell_test_find_uih(cell_test_frame, got, 2, payload, sizeof(payload));
  cell_test_check("Channel data framed for the modem", (found == 3) && (memcmp(payload, "AT\r", 3) == 0));

  // 400 bytes in two writes, with the modem side not read until both are in:
  // each read from the channel must be split into frames of at most N1 (127)
  // bytes, and the second batch overfills the modem pty, so the frame writer
  // must finish partial writes instead of dropping the rest.
  static unsigned char sent[400];
  static unsigned char wire[1024];
  for (size_t index = 0; index < sizeof(sent); index++)
  {
    sent[index] = (unsigned char)('A' + (index % 26));
  }
  write(channel[1], sent, 200);
  usleep(200 * 1000);
  write(channel[1], sent + 200, 200);
  usleep(200 * 1000);
  got = cell_test_read_for(master, wire, sizeof(wire), 1500);
  size_t received = 0;
  size_t longest = 0;
  bool framed = true;
  for (size_t index = 0; (index + 5) < got;)
  {
    if ((wire[index] != 0xf9) || (wire[index + 1] == 0xf9))
    {
      index++;
      continue;
    }
    if (((wire[index + 3] & 0x01) == 0) || ((wire[index + 1] >> 2) != 1))
    {
      // A two-byte length field (a frame over 127 bytes) or another DLCI.
      framed = framed && ((wire[index + 1] >> 2) != 1);
      index++;
      continue;
    }
    size_t frame_length = wire[index + 3] >> 1;
    if ((index + 4 + frame_length) > got)
    {
      break;
    }
    if ((received + frame_length) <= sizeof(sent))
    {
      framed = framed && (memcmp(wire + index + 4, sent + received, frame_length) == 0);
    }
    received += frame_length;
    longest = (frame_length > longest) ? frame_length : longest;
    index += 4 + frame_length + 2;
  }
  syslog(LOG_MTEST, "    %zu of %zu bytes framed, longest frame %zu\n", received, sizeof(sent), longest);
  cell_test_check("Long channel data split into frames of at most 127 bytes",
                  framed && (received == sizeof(sent)) && (longest <= 127));

  // Leave the modem pty and the channels open. CMUX has no tear-down, so its
  // thread stays; once these are closed, its select() keeps reporting end of
  // file and the thread spins at CMUX priority, starving the real cell link.
}

/****************************************************************************
 * Live checks, with the cell link running
 ****************************************************************************/

static void cell_test_live(void)
{
  struct timespec begin;

  syslog(LOG_MTEST, "  Live checks (cell link)\n");

  clock_gettime(CLOCK_MONOTONIC, &begin);
  while (!meadow_cell_is_connected() && (cell_test_elapsed_ms(&begin) < (CELL_TEST_LIVE_CONNECT_TIMEOUT_S * 1000)))
  {
    sleep(1);
  }
  bool connected = meadow_cell_is_connected();
  cell_test_check("Cell link is up", connected);
  if (!connected)
  {
    return;
  }

  struct addrinfo *result = NULL;
  int ret = getaddrinfo("example.com", NULL, NULL, &result);
  cell_test_check("DNS lookup over the cell link", (ret == 0) && (result != NULL));
  if (result != NULL)
  {
    freeaddrinfo(result);
  }

  // A signal quality request through the native API: wait for new output.
  memset(cell_test_previous, 0, sizeof(cell_test_previous));
  meadow_get_cell_at_cmds_output(cell_test_previous);
  meadow_cell_change_state(CELL_AT_CMD_SIGNAL_QUALITY);
  clock_gettime(CLOCK_MONOTONIC, &begin);
  do
  {
    sleep(1);
    memset(cell_test_output, 0, sizeof(cell_test_output));
    meadow_get_cell_at_cmds_output(cell_test_output);
  }
  while ((memcmp(cell_test_output, cell_test_previous, sizeof(cell_test_output)) == 0) &&
         (cell_test_elapsed_ms(&begin) < (CELL_TEST_LIVE_REQUEST_TIMEOUT_S * 1000)));
  cell_test_check("Signal quality request returns +CSQ", strstr((char *)cell_test_output, "+CSQ: ") != NULL);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

//====================================================================
// Developer test 20. A value of 1 also runs the live checks, which need the
// cell link to be running.
void meadow_cell_tests(uint32_t value)
{
  cell_test_passes = 0;
  cell_test_failures = 0;

  syslog(LOG_MTEST, "Executing cell tests\n");
  cell_test_connect_scripts();
  cell_test_gga();
  cell_test_at_parser();
  cell_test_cmux();
  if ((value & 1) != 0)
  {
    cell_test_live();
  }

  syslog(LOG_MTEST, "Cell tests: %d passed, %d failed - %s.\n", cell_test_passes, cell_test_failures,
         (cell_test_failures == 0) ? "PASS" : "FAIL");
}

#endif // CONFIG_CELL_TESTS
