/****************************************************************************
 * Meadow.OS/hcom_nx/hcom_nx_common_utils.c
 * 
 *   Copyright (C) 2019 - 2020 Wilderness Labs. All rights reserved.
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
#include "syslog.h"
#include "arm_internal.h"

#include "hcom_nx_common.h"
#include <meadow/hcom_bbreg_defn.h>
#include <meadow/hcom_protocol.h>
#include <meadow/hcom_shared_common.h>

#include <assert.h>

#include <arch/board/board.h>
#include "stm32_gpio.h"
#include "stm32_uid.h" // stm32_get_uniqueid()

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Configuration ************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/
static char *thisFile = __FILE__;
static uint32_t _syslogMask;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void hcom_nx_common_utils_host_restart_meadow()
{
  // This tells hcom when it starts that a concluded messages needs to
  // be sent to the host
  hcom_nx_bbreg_set_bbr_bits(HCOM_BBREG_RESTART_INITIATED_BY_HOST_CMD_BIT);
 
  // Give time for reconnect message to arrive and be processed before restart
  usleep(500 * 1000);

  // This never returns
  up_systemreset();
}

//============================================================================
void hcom_nx_common_utils_only_restart_meadow()
{
  // Give time for message to arrive and be processed before restart
  usleep(500 * 1000);

  // This never returns
  up_systemreset();
}

//============================================================================
// This call must be before any trace handling, it detects power cycle.
//
// Called early (hcom_nx_setup_mgr(), well before hcom_nx_config_init()
// parses meadow.config.yaml), so the mask established/persisted here is
// necessarily BBR-only -- it can't yet reflect the yaml file's
// InternalDebug.TraceLevel setting, since that file hasn't been read yet.
// hcom_nx_utils_apply_config_trace_level() augments _syslogMask with that
// setting later, once config parsing has completed (called from
// hcom_nx_setup_mgr(), after hcom_nx_config_init()) -- mirroring what
// user-space's hcom_diag_trace_priority_level_init() (apps/system/hcom/
// diag/hcom_diag_trace_priority.c) already does for its own, separate
// copy of the mask. Kernel-space and user-space each have their own
// independent syslog mask in a protected build (see the REVISIT comment
// in lib_setlogmask.c), so each side augments its own via its own
// setlogmask() call; there's no single call that could cover both.
int hcom_nx_utils_startup_handling_of_trace_level()
{
#if defined(CONFIG_STM32F7_PWR)
  // Check if this is a reboot or a power-on restart. The MCU at
  // Power-on (unless there's a coin cell) clears all 32 battery
  // backed registers to 0.
  uint32_t bbrValue = getreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER);
  if(bbrValue == 0)
  {
    // #define LOG_EMERG     0  /* System is unusable */
    // #define LOG_ALERT     1  /* Action must be taken immediately */
    // #define LOG_CRIT      2  /* Critical conditions */
    // #define LOG_ERR       3  /* Error conditions */
    // #define LOG_WARNING   4  /* Warning conditions */
    // #define LOG_NOTICE    5  /* Normal, but significant, condition */
    // #define LOG_INFO      6  /* Informational message */
    // #define LOG_DEBUG     7  /* Debug-level message */
    // Power-on restart clears all 32 of the battery backed registers to 0
    // Set and save the syslog level to the default value
    _syslogMask = LOG_MASK(LOG_EMERG) | LOG_MASK(LOG_ALERT) | LOG_MASK(LOG_CRIT) |
              LOG_MASK(LOG_ERR) | LOG_MASK(LOG_WARNING) |
              LOG_MASK(LOG_NOTICE) /*| LOG_MASK(LOG_INFO) | LOG_MASK(LOG_DEBUG) */;

    // Even though there are bits defined for other purposes, this works
    // because we know that the entire register is 0.
    putreg32(_syslogMask, HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER);
  }
  else
  {
    // Rebooted - it's safe to use the battery backed registers values
    _syslogMask = getreg32(HCOM_NX_MEADOW_BATTERY_BACKED_REGISTER);
    _syslogMask &= 0x000000ff;   // LS 8 bits are syslog mask

    // Mark that this start is a reboot, not a power-on restart, so any
    // later code -- including user-space, which always runs after this --
    // can still tell the two apart even after the syslog mask write above
    // (or the one just above in the power-on branch) makes the whole
    // register read back as non-zero either way.
    hcom_nx_bbreg_set_bbr_bits(HCOM_BBREG_LAST_START_WAS_REBOOT_BIT);
  }

#if HCOM_FORCE_SYSLOG_MASK_AND_OUTPUT_TO_UART > 0
    _syslogMask = LOG_MASK(LOG_EMERG) | LOG_MASK(LOG_ALERT) | LOG_MASK(LOG_CRIT) |
              LOG_MASK(LOG_ERR) | LOG_MASK(LOG_WARNING) |
              LOG_MASK(LOG_NOTICE) | LOG_MASK(LOG_INFO) /* | LOG_MASK(LOG_DEBUG) */;
#endif

  // Save for emergency debugging :-)
  // _syslogMask = LOG_MASK(LOG_EMERG) | LOG_MASK(LOG_ALERT) | LOG_MASK(LOG_CRIT) |
  //             LOG_MASK(LOG_ERR) | LOG_MASK(LOG_WARNING) | LOG_MASK(LOG_NOTICE) | 
  //             LOG_MASK(LOG_INFO);  // | LOG_MASK(LOG_DEBUG);

  // Sets new mask and returns the previous syslog_mask
  int syslogMaskPrev = setlogmask(_syslogMask);
  if (syslogMaskPrev < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setlogmask err:0x%08x\n", thisFile, __LINE__, syslogMaskPrev);
    return syslogMaskPrev;    // not old mask but error
  }
#else
#warning "CONFIG_STM32F7_PWR not defined\n"
  // Without battery backed registers the best we can do is defaults
  _syslogMask = LOG_MASK(LOG_EMERG) | LOG_MASK(LOG_ALERT) | LOG_MASK(LOG_CRIT) |
              LOG_MASK(LOG_ERR) | LOG_MASK(LOG_WARNING);
  int syslogMaskPrev = setlogmask(_syslogMask);
  if (syslogMaskPrev < 0)
  {
    syslog(LOG_CRIT, "%s@%d-setlogmask err:0x%08x\n", thisFile, __LINE__, syslogMaskPrev);
    return syslogMaskPrev;    // not old mask but error
  }
#endif
  return OK;
}

//============================================================================
// Applies meadow.config.yaml's InternalDebug.TraceLevel setting to the
// kernel-space syslog mask -- the yaml-driven counterpart to the BBR-only
// default hcom_nx_utils_startup_handling_of_trace_level() establishes
// above. Must be called after that function (so _syslogMask already holds
// the BBR-derived base) and after hcom_nx_config_init() has parsed the
// config file (so configTraceLevel is meaningful). Mirrors the identical
// switch in user-space's hcom_diag_trace_priority_level_init()
// (apps/system/hcom/diag/hcom_diag_trace_priority.c) exactly, so kernel-
// space syslog() calls get the same config-driven verbosity user-space
// calls already do. Not persisted back to BBR -- like user-space, this is
// re-derived fresh from the config file every boot, so there's nothing to
// save.
void hcom_nx_utils_apply_config_trace_level(int configTraceLevel)
{
  switch(configTraceLevel)
  {
    case 0:
      break;
    case 1:
      _syslogMask |= LOG_MASK(LOG_NOTICE);
      break;
    case 2:
      _syslogMask |= LOG_MASK(LOG_NOTICE) | LOG_MASK(LOG_INFO);
      break;
    case 3:
      _syslogMask |= LOG_MASK(LOG_NOTICE) | LOG_MASK(LOG_INFO) |
          LOG_MASK(LOG_DEBUG);
      break;
    default:
      syslog(LOG_WARNING,
        "%s@%d-Unsupported config trace level of %d ignored\n",
         thisFile, __LINE__, configTraceLevel);
      break;
  }

  setlogmask(_syslogMask);
}

//============================================================================
// Immediately applies a fully-formed syslog mask to kernel-space, bypassing
// the config-file-driven augmentation above. Called (via the /dev/upd
// HCOM_NX_UPD_SET_SYSLOG_MASK ioctl) from user-space's
// hcom_diag_trace_priority_change_trace_level() so a live CLI trace-level
// change takes effect on kernel-space syslog() calls immediately, the same
// way it already does for user-space's own mask -- instead of only taking
// effect on kernel-space after the next reboot re-reads the BBR value that
// CLI command also persists.
void hcom_nx_utils_set_syslog_mask_now(uint32_t newMask)
{
  _syslogMask = newMask;
  setlogmask(_syslogMask);
}

//============================================================================
// This function provides the serial number to the CDCACM via a Nuttx definition
FAR const char *board_usbdev_serialstr(void)
{
  static char strMcuSn[16];

  hcom_nx_common_utils_calc_f7_sn(NULL, strMcuSn);
  return strMcuSn;
}

//============================================================================
// Returns the MCU serial number as int and as a null terminated char, as requested
void hcom_nx_common_utils_calc_f7_sn(uint8_t mcu6ByteSerialNumb[], char mcu12CharSerialNumb[])
{
  uint8_t uniqueId[12];  // 96 bit unique chip id as 12 bytes
  
  stm32_get_uniqueid(uniqueId);

  // Convert chip Id to serial number
  uint8_t serialNumb[6];
  serialNumb[0] = uniqueId[11];                     // 95-88
  serialNumb[1] = uniqueId[10] + uniqueId[2];       // 87-80 + 23-16
  serialNumb[2] = uniqueId[9];                      // 79-72
  serialNumb[3] = uniqueId[8] + uniqueId[0] + 10;   // 71-64 + 7-0 + magic 10
  serialNumb[4] = uniqueId[7];                      // 63-56 
  serialNumb[5] = uniqueId[6];                      // 55-48

  if(mcu6ByteSerialNumb != NULL)
    memcpy(mcu6ByteSerialNumb, serialNumb, 6);
  
  if(mcu12CharSerialNumb == NULL)
    return;

  // Convert serial number to string. The result is 12 + NULL = 13 bytes
  snprintf_chk(mcu12CharSerialNumb, 16, "%02X%02X%02X%02X%02X%02X", 
          serialNumb[0], serialNumb[1], serialNumb[2],
          serialNumb[3], serialNumb[4], serialNumb[5]);
}

//============================================================================
// For diagnostic use only
#if HCOM_INCLUDE_DIAG_PRINT_BUFFER_CODE > 0

#define HCOM_UTIL_BYTES_PER_LINE 16
#define HCOM_UTIL_LEADING_SPACES 2
#define HCOM_UTIL_HEXADECIMAL_OFFSET (8 + HCOM_UTIL_LEADING_SPACES)
#define HCOM_UTIL_ASCII_OFFSET (57 + HCOM_UTIL_LEADING_SPACES)
#define HCOM_UTIL_DISPLAY_LENGTH (HCOM_UTIL_ASCII_OFFSET + HCOM_UTIL_BYTES_PER_LINE + 3)

void hcom_nx_diag_print_buffer(const uint8_t buffer[], const int bufLen, uint8_t msgPriority)
{
  // (--) THIS IS BROKEN - comment out so all priorities pass
  // if ((_syslogMask & LOG_MASK(msgPriority)) == 0)
  // {
  //   syslog(LOG_MDIAG, "diag print buffer exited, priority too low.\n");
  //   return;
  // }

  // Use the Nuttx standard 'syslog' for output
  hcom_nx_diag_print_buffer_x(buffer, bufLen, msgPriority, syslog);
}
#else
void hcom_nx_diag_print_buffer(const uint8_t buffer[], const int bufLen, uint8_t msgPriority)
{
  syslog(LOG_MDIAG, "HCOM_INCLUDE_DIAG_PRINT_BUFFER_CODE not defined.\n");
}
#endif

#if HCOM_INCLUDE_DIAG_PRINT_BUFFER_CODE > 0
//============================================================================
// For diagnostic use only
// This version makes no assumptions about the function used for output, but
// it must be provided and it's signature must be 'void logger(int priority,
// const char *fmt, ...)' which is syslog's signature.
// Note: the priority field is ignored.
void hcom_nx_diag_print_buffer_x(const uint8_t buffer[], const int bufLen, uint8_t msgPriority,
        void (*logger)(int priority, const char *string, ...))
{
  int rowStartOffset, rowByteOffset;
  char lineBuff[HCOM_UTIL_DISPLAY_LENGTH];
  int hexOffset;
  int asciiOffset;

  if(bufLen <= 0)
  {
    logger(msgPriority, "%s@%d-%s() but 'bufLen:%d'\n",
              thisFile, __LINE__, __func__, bufLen);
    return;
  }

  if(buffer == NULL)
  {
    logger(msgPriority, "%s@%d-%s() but 'buffer == NULL'\n",
              thisFile, __LINE__, __func__);
    return;
  }

  // One line at a time
  for (rowStartOffset = 0; rowStartOffset < bufLen; rowStartOffset += HCOM_UTIL_BYTES_PER_LINE)
  {
    memset(lineBuff, 0x20, HCOM_UTIL_DISPLAY_LENGTH);

    // Buffer offset address
    snprintf(&lineBuff[HCOM_UTIL_LEADING_SPACES],
      HCOM_UTIL_DISPLAY_LENGTH, "%08lx ", (uint32_t)(buffer + rowStartOffset));

    hexOffset = HCOM_UTIL_HEXADECIMAL_OFFSET;
    asciiOffset = HCOM_UTIL_ASCII_OFFSET;

    // Loop for each character
    for (rowByteOffset = 0; rowByteOffset < HCOM_UTIL_BYTES_PER_LINE; rowByteOffset++)
    {
      off_t buffOffset = rowStartOffset + rowByteOffset;
      if (buffOffset >= bufLen)
        break;        // Reached the end of the buffer's data

      // Get the next byte to output
      uint8_t nextByte = buffer[buffOffset];

      // Save the hex value (add '.' half way)
      if(rowByteOffset == HCOM_UTIL_BYTES_PER_LINE / 2)
        snprintf(&lineBuff[hexOffset], HCOM_UTIL_DISPLAY_LENGTH - hexOffset, ".%02x", nextByte);
      else
        snprintf(&lineBuff[hexOffset], HCOM_UTIL_DISPLAY_LENGTH - hexOffset, " %02x", nextByte);
      hexOffset += 3;

      // Save the ascii value
      if (nextByte == 0) // Make it easy to spot '0'
        snprintf(&lineBuff[asciiOffset], HCOM_UTIL_DISPLAY_LENGTH - hexOffset, "-");
      else if (nextByte == 0xff)
        snprintf(&lineBuff[asciiOffset], HCOM_UTIL_DISPLAY_LENGTH - hexOffset, "*");
      else if (nextByte < 0x20 || nextByte > 0x7e)  //isprint()
        snprintf(&lineBuff[asciiOffset], HCOM_UTIL_DISPLAY_LENGTH - hexOffset, ".");
      else
        snprintf(&lineBuff[asciiOffset], HCOM_UTIL_DISPLAY_LENGTH - hexOffset, "%c", nextByte);

      asciiOffset++;
      if(asciiOffset >= HCOM_UTIL_DISPLAY_LENGTH)
        break;    // Just in case
    }

    // This row is ready
    lineBuff[hexOffset]     = 0x20;   // Replace last hex null with a space
    lineBuff[asciiOffset++] = 0x0a;   // line feed
    lineBuff[asciiOffset]   = 0x00;   // null terminator

    // Output one line
    logger(msgPriority, lineBuff);
    // On long dumps without this delay, overrun some Nuttx buffer and crash
    if(bufLen > 4096)
      usleep(10 * 1000);    // Give time to write line
  }
}
#else
void hcom_nx_diag_print_buffer_x(const uint8_t buffer[], const int bufLen, uint8_t msgPriority,
        void (*logger)(int priority, const char *string, ...))
{
}
#endif

//===================================================================
// Due to the number of places snprintf is called and the code required
// to determine success or failure. This function is designed so that
// users can generate less code and be confident that truncated messages
// are noted. A macro that adds file name and line number calls this
int hcom_nx_common_utils_snprintf_chk(FAR char *buf, size_t size, char *fileName, int lineNumb,
          FAR const IPTR char *fmt, ...)
{
  int bufChk;
  va_list ap;

  va_start(ap, fmt);

  // Process the string
  bufChk = vsnprintf(buf, size, fmt, ap);
  va_end(ap);

  // Handle buffer overflow here
  if(bufChk >= size)
  {
    syslog(LOG_WARNING, "%s@%d Host msg truncated, need:%d\n", fileName, lineNumb, bufChk + 1);
    // This modifies the standard Nuttx snprintf behavior which would normally
    // return the size of needed buffer.
    return -ENAMETOOLONG;
  }
  else if(bufChk < 0)
  {
    syslog(LOG_ERR, "%s@%d snprintf returned an error, ret:%d\n", fileName, lineNumb, bufChk);
  }

  // Must be operations as usual
  return bufChk;
}
