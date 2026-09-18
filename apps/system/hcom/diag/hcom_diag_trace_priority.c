/****************************************************************************
 * /Meadow.OS/apps/system/hcom/diag/hcom_diag_trace_priority.c
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

// This module deals with the syslog/trace level setting and reporting

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <ctype.h>
#include "../hcom_common.h"
#include <meadow/hcom_protocol.h>
#include <meadow/hcom_bbreg_defn.h>

#include <nuttx/config.h>
#include "syslog.h"
#include <meadow/meadow_os.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Configuration ************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int _traceLevel;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// Make syslogMask available
int hcom_diag_trace_priority_get_trace_level()
{
  return _traceLevel;
}

//============================================================
// This function is called by startup_manager and gets the
// syslog mask set by hcom_nx
int hcom_diag_trace_priority_level_init()
{
  bool isPowerOnRestart;

  // Needed unconditionally for the startup info banner below (os_version,
  // hardware_version_text, mono_version), not just the CONFIG_STM32F7_PWR
  // path -- fetched here so it's always in scope, regardless of that config.
  meadow_configuration_t *config = meadow_os_deep_copy_config();

#if defined(CONFIG_STM32F7_PWR)
  // This BBR was set by hcom nx since it starts first -- hcom_nx already
  // establishes a sane default mask here on a true power-on restart (and
  // persists it), so this can always just read back whatever is there.
  _traceLevel = hcom_bbreg_read_bbr_and_right_justify(
    HCOM_BBREG_RESTART_SYSLOG_CONFIG_VALUE_MASK);

  // Check the configuration file value stored in the config structure.
  int iniValue = 0;
  if (config != NULL)
  {
    // Read any configuration trace level
    iniValue = config->trace_level;
  }
  switch(iniValue)
  {
    case 0:
      break;
    case 1:
      _traceLevel |= LOG_MASK(LOG_NOTICE);
      break;
    case 2:
      _traceLevel |= LOG_MASK(LOG_NOTICE) | LOG_MASK(LOG_INFO);
      break;
    case 3:
      _traceLevel |= LOG_MASK(LOG_NOTICE) | LOG_MASK(LOG_INFO) |
          LOG_MASK(LOG_DEBUG);
      break;
    default:
      syslog(LOG_WARNING,
        "%s@%d-Unsupported config trace level of %d ignored\n",
         __FILE__, __LINE__, iniValue);
      break;
  }

  // This only sets user-space's own syslog mask -- kernel-space has an
  // entirely separate g_syslog_mask instance in a protected build (see the
  // REVISIT comment in nuttx/libs/libc/syslog/lib_setlogmask.c), set once
  // by hcom_nx from the BBR alone, before this config-driven iniValue
  // augmentation above was even available (hcom_nx runs before
  // meadow.config.yaml is parsed). So a config-file trace_level override
  // only ever affects syslog() calls made from user-space code; kernel-
  // space syslog() calls stay gated by the BBR-only default regardless.
  setlogmask(_traceLevel);

  // Check if this is a reboot or a power-on restart. hcom_nx already OR's
  // the syslog mask bits above into this same register on every boot
  // (power-on or not), so by the time this runs the whole register can
  // never read back as 0 even on a true power-on -- read the dedicated
  // marker bit instead. hcom_nx sets it only once it has determined, from
  // the register's pristine pre-write state, that this is a reboot; it is
  // left at its power-on-cleared value of 0 on a true power-on, and stays
  // 1 across any number of further warm reboots since nothing but a true
  // power-on ever clears it.
  if(hcom_bbreg_read_bbr_and_right_justify(
       HCOM_BBREG_LAST_START_WAS_REBOOT_BIT) == 0)
  {
    // Power-on restart
    isPowerOnRestart = true;
  }
  else
  {
    // Rebooted - it's safe to use the battery backed registers values
    isPowerOnRestart = false;
  }
#else
  // Without battery backed registers the best we can do is guess
  isPowerOnRestart = true;
#endif

#if defined(CONFIG_STM32F7_PWR)
  // Check the host and uart1 bits in battery backed register
  char *traceCombo[] = {
    "none",
    "Host",
    "UART",
    "Host+UART"};
  uint32_t destValue = hcom_bbreg_read_bbr_and_right_justify(
    HCOM_BBREG_TRACE_MSG_TO_HOST_AND_UART_BIT_MASK);
  char *traceDest = traceCombo[destValue];
#else
  char *traceDest = "unknown";
#endif

  // Provide some information that may be useful on startup
  syslog(LOG_NOTICE, "Meadow %s %s, H/W:%s, Mono:%s, Trace level:0x%02x, to:%s (%s)\n",
        config == NULL ? "" : config->os_version.long_string,
        isPowerOnRestart ? "powered-on" :"rebooted",
        config != NULL ? config->hardware_version_text : "Unknown",
        hcom_mono_ctrl_is_mono_enabled() ? "Enabled" : "Disabled",
        _traceLevel, traceDest,
#if defined CONFIG_RAMLOG_SYSLOG
        "ramlog");
#else
        "syslog");
#endif

  syslog(LOG_NOTICE, "Mono version: %s\n",
    config == NULL ? "" : config->mono_version.long_string);

  if (config != NULL)
  {
    meadow_os_config_free_resources(config);
  }

  return OK;
}

//=======================================================================================
// Called from CLI command to changed the trace level
void hcom_diag_trace_priority_change_trace_level(uint32_t userData)
{
  char hostMsg[HCOM_SHORT_HOST_STRING_BUFF_LENGTH];
  char *traceNew;
  char *traceOld;
  
  // Minimum default
  int newTraceLevelMask = LOG_MASK(LOG_EMERG) | LOG_MASK(LOG_ALERT) |
        LOG_MASK(LOG_CRIT) | LOG_MASK(LOG_ERR) | LOG_MASK(LOG_WARNING);

  // Add additional trace levels
  switch (userData)
  {
    case HCOM_TRACE_LEVEL_NOTICE:
      newTraceLevelMask |= LOG_MASK(LOG_NOTICE);
      traceNew = "Notice";
      break;

    case HCOM_TRACE_LEVEL_NOTICE_INFO:
      newTraceLevelMask |= LOG_MASK(LOG_NOTICE) | LOG_MASK(LOG_INFO);
      traceNew = "Notice and Information";
      break;

    case HCOM_TRACE_LEVEL_NOTICE_INFO_DEBUG:
      newTraceLevelMask |= LOG_MASK(LOG_NOTICE) | LOG_MASK(LOG_INFO) |
          LOG_MASK(LOG_DEBUG);
      traceNew = "Notice, Information and Debug";
      break;

    case HCOM_TRACE_LEVEL_DEFAULT:
      traceNew = "Normal";
      break;

    default:    // minimum newTraceLevelMask
      traceNew = "Default";
      break;
  }

#if HCOM_FORCE_SYSLOG_MASK_AND_OUTPUT_TO_UART > 0
  newTraceLevelMask |= (LOG_MASK(LOG_NOTICE) | LOG_MASK(LOG_INFO));
#endif

  // Set the new syslog mask
  hcom_bbreg_clear_then_set_bbr_bits(
    HCOM_BBREG_RESTART_SYSLOG_CONFIG_VALUE_MASK, newTraceLevelMask);

  // Set new log mask and get the only mask
  int oldSyslogMask = setlogmask(newTraceLevelMask);
  _traceLevel = newTraceLevelMask;

  // Give kernel-space syslog() calls the same immediate effect -- without
  // this, kernel-space would only pick up the new mask on the next reboot,
  // when it re-reads the BBR bits just persisted above.
  hcom_via_nx_set_syslog_mask(newTraceLevelMask);

  switch (oldSyslogMask)
  {
    case HCOM_TRACE_MASK_NOTICE:
      traceOld = "Notice";
      break;

    case HCOM_TRACE_MASK_NOTICE_INFO:
      traceOld = "Notice and Information";
      break;

    case HCOM_TRACE_MASK_NOTICE_INFO_DEBUG:
      traceOld = "Notice, Information and Debug";
      break;
    
    case HCOM_TRACE_MASK_DEFAULT:
    default:    // minumum newTraceLevelMask
      traceOld = "Normal";
      break;
  }

  if(oldSyslogMask != newTraceLevelMask)
  {
    snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH,
            "Trace level changed from '%s' (0x%02x) to '%s' (0x%02x)",
             traceOld, oldSyslogMask, traceNew, newTraceLevelMask);
  }
  else
  {
    snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH,
            "Trace level remained at '%s' (0x%02x)",
            traceNew, newTraceLevelMask);
  }

  hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION, 0,
    hostMsg, __FILE__, __LINE__);

  syslog(LOG_NOTICE, "%s@%d-%s\n\n", __FILE__, __LINE__, hostMsg);
}
