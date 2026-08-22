/****************************************************************************
 * \include\meadow\hcom_upd_shared.h
 * 
 *   Copyright (C) 2020-2026 Wilderness Labs. All rights reserved.
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
#ifndef __INCLUDE_MEADOW_HCOM_NX_SHARED__H
#define __INCLUDE_MEADOW_HCOM_NX_SHARED__H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <meadow/hcom_shared_common.h>
#include <meadow/hcom_protocol.h>
#include <meadow/meadow_pwr_mgmt.h>

/****************************************************************************
 * Private defines
 ****************************************************************************/
// This header file contains those items that must be shared by apps/hcom
// #include <string.h>

// Defining buffer sizes
#define HCOM_NX_CMD_HOST_MSG_SIZE     128
#define HCOM_NX_CMD_LOG_MSG_SIZE      128

// Battery Backed Register
struct hcom_nx_upd_bbr_value
{
  uint32_t value;
};

struct hcom_nx_upd_bbr_update
{
  uint32_t clearBits;
  uint32_t setBits;
};

// Kernel-space syslog mask
struct hcom_nx_upd_syslog_mask
{
  uint32_t value;
};

struct hcom_nx_upd_update_flag
{
  uint8_t offset;
  uint8_t value;
};

struct hcom_nx_upd_is_part_mounted
{
  uint32_t partitionId;
  bool isMounted;
};

struct hcom_nx_cmd_data
{
  uint16_t hcomCmd;   // The orginal host command
  uint32_t userData;

  uint8_t logLevel;
  uint8_t logLen;
  char logMsg[HCOM_NX_CMD_LOG_MSG_SIZE + 1];
  void (* send_host_msg)(uint16_t, uint32_t, char *, char *, int);
};

struct hcom_nx_upd_gpio_config_s
{
  uint32_t gpioPinDefn;
  int result;
};

struct hcom_nx_upd_gpio_write_s
{
  uint32_t gpioPinDefn;
  bool cmdValue;   // 1 = high, 0 = low
};

struct hcom_nx_upd_uart_reconfig_s
{
  uint32_t uart_id;
};

struct hcom_nx_upd_diag_fd_inode_s
{
  uint32_t fileDescriptor;
  struct inode *inodeAddr;
};

struct hcom_nx_upd_mcu_ser_numb_s
{
  char *ser_numb;
};

struct hcom_nx_upd_ini_cfg_get_value_s
{
  char *file_name;
  char *section_name;
  char *key_name;
  char *return_value;
  int return_size;
};

struct hcom_nx_upd_ini_cfg_get_match_s
{
  char *file_name;
  char *section_name;
  char *key_name;
  char *return_error;
  char *match_value;
  bool return_bool;
};

struct hcom_nx_upd_ini_cfg_get_int_defval_s
{
  char *file_name;
  char *section_name;
  char *key_name;
  char *return_error;
  int default_value;
  int return_int;
};

typedef struct hcom_nx_upd_get_hw_ver_s
{
  uint32_t hwVer;

} hcom_nx_upd_get_hw_ver_t;

typedef struct hcom_nx_upd_diag_app_command_s
{
  const HcomProtoHdrMsg_t *hdrMsg;
  size_t msgLen;

} hcom_nx_upd_diag_app_command_t;

typedef struct hcom_nx_upd_rtc_set_time_s
{
  // Use ISO 8601 format
  const HcomProtoHdrMsg_t *hdrMsg;
  size_t msgLen;

} hcom_nx_upd_rtc_set_time_t;

typedef struct hcom_nx_upd_rtc_wakeup_time_s
{
  // Use ISO 8601 format
  const HcomProtoHdrMsg_t *hdrMsg;
  size_t msgLen;

} hcom_nx_upd_rtc_wakeup_time_t;

typedef struct hcom_nx_upd_pwr_mgmt_cb_s
{
  pwr_mgmt_notify_callback callback;
} hcom_nx_upd_pwr_mgmt_cb_t;

typedef struct hcom_nx_upd_host_send_cb_s
{
  send_host_std_msg_data hostCallback;
} hcom_nx_upd_host_send_cb_t;


/****************************************************************************
 * ioctl command number allocation for /dev/upd
 *
 *   0x0000 - 0x00ff   MUPD_*          Meadow UPD commands (meadow-upd.h).
 *                                     Frozen - external .NET dependency.
 *   0x7000 - 0x70ff   HCOM_NX_UPD_*   HCOM kernel-side commands (below).
 *
 * Both command sets are handled by the single switch in upd_ioctl()
 * (meadow_os/meadow-upd.c), so their numbers must not overlap.
 *
 * 0x7000 is chosen to sit clear of the ioctl command blocks NuttX reserves
 * in nuttx/include/nuttx/fs/ioctl.h (currently 0x0100 - 0x4600, plus 0x8b00
 * and 0xff00). Some of those - FIONBIO, FIOC_*, BIOC_* - are handled by the
 * VFS in fs_ioctl.c and never reach this driver, so overlapping them would
 * fail in a very confusing way. Re-check this constant on a NuttX rebase.
 ****************************************************************************/

#define HCOM_NX_UPD_BASE                        0x7000

// Battery-backed register access
#define HCOM_NX_UPD_SET_BBR_VALUE               (HCOM_NX_UPD_BASE + 0x00)
#define HCOM_NX_UPD_GET_BBR_VALUE               (HCOM_NX_UPD_BASE + 0x01)
#define HCOM_NX_UPD_UPDATE_BBR_VALUE            (HCOM_NX_UPD_BASE + 0x02)

// Device identity and hardware information
#define HCOM_NX_UPD_GET_MCU_ID                  (HCOM_NX_UPD_BASE + 0x10)
#define HCOM_NX_UPD_GET_MCU_SER_NUMB            (HCOM_NX_UPD_BASE + 0x11)
#define HCOM_NX_UPD_GET_HW_VERSION              (HCOM_NX_UPD_BASE + 0x12)

// File system
#define HCOM_NX_UPD_IS_PART_MOUNTED             (HCOM_NX_UPD_BASE + 0x20)

// Is a host computer currently using the file system exposed over USB
// mass storage? Narrower than storage mode, which is only the runtime
// being disabled; this is true once a host has actually read the volume.
#define HCOM_NX_UPD_IS_HOST_USING_FS            (HCOM_NX_UPD_BASE + 0x21)

// Host / CLI command routing
#define HCOM_NX_UPD_CLI_COMMAND                 (HCOM_NX_UPD_BASE + 0x30)
#define HCOM_NX_UPD_DIAG_APP_CMD                (HCOM_NX_UPD_BASE + 0x31)

// Restart, DFU and power management
#define HCOM_NX_UPD_HOST_RESTART_MEADOW_MCU     (HCOM_NX_UPD_BASE + 0x40)
#define HCOM_NX_UPD_ONLY_RESTART_MEADOW_MCU     (HCOM_NX_UPD_BASE + 0x41)
#define HCOM_NX_UPD_ENTER_INTO_DFU_MODE         (HCOM_NX_UPD_BASE + 0x42)
#define HCOM_NX_UPD_REG_PWR_MGMT_CB             (HCOM_NX_UPD_BASE + 0x43)

// Real-time clock
#define HCOM_NX_UPD_RTC_SET_TIME                (HCOM_NX_UPD_BASE + 0x50)
#define HCOM_NX_UPD_RTC_WAKEUP_TIME             (HCOM_NX_UPD_BASE + 0x51)

// OS update / OTA
#define HCOM_NX_UPD_UPDATE_OS1                  (HCOM_NX_UPD_BASE + 0x60)
#define HCOM_NX_UPD_UPDATE_OS2                  (HCOM_NX_UPD_BASE + 0x61)
#define HCOM_NX_UPD_GET_UPDATE_FLAG             (HCOM_NX_UPD_BASE + 0x62)
#define HCOM_NX_UPD_SET_UPDATE_FLAG             (HCOM_NX_UPD_BASE + 0x63)
#define HCOM_NX_UPD_COPY_RUNTIME_TO_RAM         (HCOM_NX_UPD_BASE + 0x64)

// Mono runtime and UART restoration
#define HCOM_NX_UPD_MONO_HAS_STARTED            (HCOM_NX_UPD_BASE + 0x70)
#define HCOM_NX_UPD_RESTORE_UART_CONFIG         (HCOM_NX_UPD_BASE + 0x71)

// Kernel-side test hooks
#define HCOM_NX_UPD_EXECUTE_ESPCP_TESTS         (HCOM_NX_UPD_BASE + 0x80)

// Kernel-space syslog mask
#define HCOM_NX_UPD_SET_SYSLOG_MASK             (HCOM_NX_UPD_BASE + 0x90)

#endif  // __INCLUDE_MEADOW_HCOM_NX_SHARED__H
