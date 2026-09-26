/****************************************************************************
 * Meadow.OS/hcom_nx/diag/hcom_nx_trace_usb.c
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

/* Syslog to the second USB CDC/ACM port (/dev/ttyACM1), only in a
 * 'build.sh --dualcdc' developer build. See HCOM_USB_PORT2_USAGE in
 * hcom_shared_common.h and Documentation/DualCdcAcm.md.
 *
 * This thread has its own RAMLOG reader, separate from the RamlogRead
 * thread in hcom_nx_trace_msg_proc.c that feeds the UART and the CLI. A
 * RAMLOG reader starts at the oldest data in the buffer and keeps its own
 * position, so both writes here can block: while no host program is
 * reading the port only this thread waits, RAMLOG holds the backlog, and
 * nothing is lost unless the backlog passes the RAMLOG buffer size.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_nx_common.h"

#include <nuttx/kthread.h>

#include <termios.h>

#if HCOM_TRACE_USB_ENABLED > 0

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HCOM_TRACE_USB_PORT_NAME        ("/dev/ttyACM1")
#define HCOM_TRACE_USB_READ_BUF_LEN     (256)
#define HCOM_TRACE_USB_RETRY_DELAY_SEC  (1)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static char _usbReadBuf[HCOM_TRACE_USB_READ_BUF_LEN];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

//==========================================================================
// Open the second USB port. This fails until the host has configured the
// USB device, so the caller retries.
static int hcom_nx_trace_usb_open_port(void)
{
  struct termios tio;
  int fd;

  fd = open(HCOM_TRACE_USB_PORT_NAME, O_WRONLY);
  if(fd < 0)
    return -1;

  // RAMLOG lines end in LF only, send CR-LF as the UART console does
  if(tcgetattr(fd, &tio) == 0)
  {
    tio.c_oflag |= OPOST | ONLCR;
    tcsetattr(fd, TCSANOW, &tio);
  }

  return fd;
}

//==========================================================================
// Copy RAMLOG to the second USB port, forever
static int hcom_nx_trace_usb_kthread(int argc, char *argv[])
{
  int usbFd = -1;
  int ramlogFd = -1;
  size_t pending = 0;
  size_t offset = 0;
  ssize_t nbytes;

  while(true)
  {
    if(usbFd < 0)
    {
      usbFd = hcom_nx_trace_usb_open_port();
      if(usbFd < 0)
      {
        sleep(HCOM_TRACE_USB_RETRY_DELAY_SEC);
        continue;
      }
    }

    // Opened once and kept, so the read position survives a USB disconnect
    if(ramlogFd < 0)
    {
      ramlogFd = open(HCOM_TRACE_RAMLOG_DEVICE_NAME, O_RDONLY);
      if(ramlogFd < 0)
      {
        sleep(HCOM_TRACE_USB_RETRY_DELAY_SEC);
        continue;
      }
    }

    if(pending == 0)
    {
      nbytes = read(ramlogFd, _usbReadBuf, sizeof(_usbReadBuf));
      if(nbytes < 0)
      {
        if(errno != EINTR)
        {
          close(ramlogFd);
          ramlogFd = -1;
          sleep(HCOM_TRACE_USB_RETRY_DELAY_SEC);
        }
        continue;
      }

      pending = (size_t)nbytes;
      offset = 0;
      continue;
    }

    // Blocks while no host program is reading the port
    nbytes = write(usbFd, _usbReadBuf + offset, pending);
    if(nbytes < 0)
    {
      if(errno == EINTR)
        continue;

      // Disconnected (ENOTCONN) or another error. The rest of this chunk is
      // dropped, then the port is reopened once the host has configured
      // the USB device again.
      close(usbFd);
      usbFd = -1;
      pending = 0;
      sleep(HCOM_TRACE_USB_RETRY_DELAY_SEC);
      continue;
    }

    offset += (size_t)nbytes;
    pending -= (size_t)nbytes;
  }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

//==========================================================================
// Called at startup to start copying syslog to the second USB port
int hcom_nx_trace_usb_setup(void)
{
  int pid;

  pid = kthread_create(HCOM_THREAD_NAME_TRACE_USB,
                       HCOM_THREAD_PRIORITY_TRACE_USB,
                       HCOM_THREAD_STACKSIZE_TRACE_USB,
                       hcom_nx_trace_usb_kthread,
                       (char *const *) NULL);
  if(pid <= 0)
    return -ENOEXEC;

  return OK;
}

#endif
