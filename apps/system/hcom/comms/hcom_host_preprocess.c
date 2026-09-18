/****************************************************************************
 * \apps\examples\hcom\comms\hcom_host_preprocess.c
 * 
 *   Copyright (C) 2019 - 2025 Wilderness Labs. All rights reserved.
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

// This file does processing of all request. For file downloads it allocates
// and populates a struct that contains file specific information.

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include "../hcom_common.h"
#include <meadow/hcom_dnld_shared.h>
#include <sys/mount.h>
#include <meadow/meadow_partition_share.h>
#include <meadow/meadow_extended_errno.h>
#if defined (CONFIG_DIR_MGMT_TESTS)
#pragma message "(--) hcom_host_preprocess.c"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool _shutting_down;

// The following are allocated at startup and continue for the life of Meadow
static hcom_dnld_shared_t *_dnldShared;
static uint8_t *_packet_dest_buf = NULL;
static uint8_t *_decode_dest_buf = NULL;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int hcom_host_handle_packet(hcom_dnld_shared_t *dnldShared,
          const uint8_t *packet, const size_t packetSize);
static int hcom_host_process_run_loop(void);

/****************************************************************************
 * Private Functions
 ****************************************************************************/
static int hcom_host_preprocess_unmount_sdcard(char *MountPointName)
{
  int ret;

  ret = umount(MountPointName);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-ERROR: umount failed. ret:%d, errno:%d\n",
              __FILE__, __LINE__, ret, errno);
  }
#if defined (CONFIG_DIR_MGMT_TESTS)
  else
  {
    syslog(2, "===> %s@%d-umount successful\n", __FILE__, __LINE__);
    usleep(20 * 1000);
  }
#endif
  return ret;
}

//=====================================================================
// This function attempts to mount the SD-Card. If it fails, it will
// unmount and try again.
static int hcom_host_preprocess_mount_sdcard(void)
{
  // Attempt to mount, if it fails, unmount and try again.
  int ret;
  int attempts = 2;
  do
  {
    // Attempt to mount the SD-Card
    // mount(source, target, fstype, mountflags, data)
    // e.g. mount("/dev/mmcsd0", "/sdcard", "vfat", 0, NULL);
    ret = mount(MEADOW_SDCARD_BLOCK_NAME, MEADOW_SDCARD_MOUNT_POINT_NAME,
              MEADOW_SDCARD_FILE_SYS_TYPE, 0, NULL);
    if(ret >= 0)
    {
#if defined (CONFIG_DIR_MGMT_TESTS)
      syslog(2, "===> %s@%d-Mounted SDCard, Success\n", __FILE__, __LINE__);
      usleep(20 * 1000);
#endif
      break; // Successfully mounted, done
    }
    else
    {
      // May have been left mounted, so unmount the SD-Card
      // and try to mount again.
      ret = hcom_host_preprocess_unmount_sdcard(MEADOW_SDCARD_MOUNT_POINT_NAME);

      if(ret < 0)
      {
        syslog(LOG_ERR, "%s@%d-ERROR: Mount failed. ret:%d, errno:%d\n", __FILE__, __LINE__, ret, errno);
        return ret;
      }
      continue; // Try to mount again
    }
  }
  while(attempts-- > 0);

  // Ran out of attempts to mount the SD-Card
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-ERROR: SDCard Mount failed. ret:%d, errno:%d\n", __FILE__, __LINE__, ret, errno);
    return ret;
  }
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// Note: this thread was created by Nuttx and initially was used to do the
// Meadow initialization. Once all initialization is completed, it is used
// here to processing the HCOM messages. Therefore, it never returns.
int hcom_host_process_setup()
{
  int ret;
  _shutting_down = false;

  // These allocation continue throughout the life of Meadow
  _packet_dest_buf = (uint8_t *)malloc(HCOM_PROTOCOL_SAFE_ENCODED_MSG_BUF_SIZE);
  if (_packet_dest_buf == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Packet buffer allocation failed\n",
              __FILE__, __LINE__);
    return -ENOMEM;
  }
  
  _decode_dest_buf = (uint8_t *)malloc(HCOM_PROTOCOL_SAFE_ENCODED_MSG_BUF_SIZE);
  if (_decode_dest_buf == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Decoded allocation failed\n",
              __FILE__, __LINE__);
    free(_packet_dest_buf);
    return -ENOMEM;
  }

  // One time allocation for the download shared structure
  _dnldShared = zalloc(sizeof(hcom_dnld_shared_t));
  if (_dnldShared == NULL)
  {
    syslog(LOG_ERR, "%s@%d-Download shared allocation failed\n",
              __FILE__, __LINE__);

    free(_packet_dest_buf);
    free(_decode_dest_buf);
    return -ENOMEM;
  }

  // Initialize download shared structure
  // Currently only 1 partition allowed to be file system
  _dnldShared->dnldFilePartId = MEADOW_FLASH_PART_OFFSET_FILE_SYS;
  _dnldShared->dnldRqstCat = pathnameNotUsed;
  _dnldShared->dnldFileFD = -1;
  _dnldShared->dnldCurrentState = HcomStm32F7DnldStateInvalid;

  // The watchdog needs access to the download shared structure
  ret = hcom_host_watchdog_dnld_starting(_dnldShared);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Watchdog init failed:%d\n",
              __FILE__, __LINE__, ret);

    free(_packet_dest_buf);
    free(_decode_dest_buf);
    free(_dnldShared);
    return -ENOEXEC;    // May be better error code....
  }

  // This threads priority and initial stack size are set via menuconfig
  // Priority: CONFIG_USERMAIN_PRIORITY with a default of 100. This priority
  // is adjusted in the following code.
  // Stacksize: CONFIG_USERMAIN_STACKSIZE with a default stack size of 2048.
  // The stack size was set to 65536, don't know by whom.
  // RTOS Features->Stack and heap information->Main thread stack size
  //
  // See /nuttx/sched/init/nx_bringup.c for user main's creation
  struct sched_param sparam;
  sparam.sched_priority = HCOM_THREAD_PRIORITY_HCOM_PROCESS;
  sched_setparam(0, &sparam);

  // This thread needs to respond to SIGALRM
  sigset_t set;
  (void)sigemptyset(&set);
  (void)sigaddset(&set, SIGALRM);
  ret = sigprocmask(SIG_UNBLOCK, &set, NULL);
  if (ret != OK)
  {
    
    syslog(LOG_ERR, "%s@%d-sigprocmask() failed:%d\n",
              __FILE__, __LINE__, ret);
              
    free(_packet_dest_buf);
    free(_decode_dest_buf);
    free(_dnldShared);
    return -EPERM;
  }

  // This thread now runs host processing of received messages. As such this
  // call will never return.
  ret = hcom_host_process_run_loop();
 
  // This return should only be reached on shutdown.
  free(_packet_dest_buf);
  free(_decode_dest_buf);
  free(_dnldShared);
  return ret;
}

//===========================================================================
// Free any memory in struct hcom_dnld_shared_s.
// The intent is any function can call this and be assured that all the
// internally allocated memory is freed.
int hcom_dir_mgmt_free_file_info(hcom_dnld_shared_t *dnldShared)
{
  int ret;

#if defined (CONFIG_DIR_MGMT_TESTS)
  // To help insure all removed
  if(dnldShared->dnldFullPathName != NULL)
  {
    syslog(LOG_MTEST, "===> %s@%d-About to remove any existing dnldShared resources, cat:%s, file'%s'\n",
              __FILE__, __LINE__,
              hcom_file_dir_mgmt_find_category(dnldShared->dnldRqstCat),
              dnldShared->dnldFullPathName);
    usleep(20 * 1000);
  }
  else
  {
    syslog(LOG_MTEST, "===> %s@%d-About to remove any existing dnldShared resources, cat:%s\n",
              __FILE__, __LINE__,
              hcom_file_dir_mgmt_find_category(dnldShared->dnldRqstCat));
    usleep(20 * 1000);
  }
#endif

  // Insure all resources removed
  if(dnldShared->dnldFileFD > 0)
  {
    ret = close(dnldShared->dnldFileFD);
    if (ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-Close of nldShared->dnldFileFD, ret:%d, errno %d\n",
              __FILE__, __LINE__, ret, errno);
    }
  }

  // Free any string memory allocations
  if(dnldShared->dnldOrigPathName != NULL)
  {
    free(dnldShared->dnldOrigPathName);
    dnldShared->dnldOrigPathName = NULL;
  }

  if(dnldShared->dnldFullPathName != NULL)
  {
    free(dnldShared->dnldFullPathName);
    dnldShared->dnldFullPathName = NULL;
  }

  // Basic reinitialization
  memset(dnldShared, 0, sizeof(hcom_dnld_shared_t));
  dnldShared->dnldRqstCat = pathnameNotUsed;
  dnldShared->dnldFileFD = -1;
  dnldShared->dnldCurrentState = HcomStm32F7DnldStateInvalid;

  return OK;
}

//====================================================================
// Shutdown
void hcom_host_process_shutdown()
{
  _shutting_down = true;

  free(_decode_dest_buf);
  free(_packet_dest_buf);

  hcom_dir_mgmt_free_file_info(_dnldShared);
  free(_dnldShared);
}

//==========================================================================
// Are we involved in some external F7 flash download activity? There are the
// following valid states: HcomStm32F7DnldStateNone,
// HcomStm32F7DnldStateStarting and HcomStm32F7DnldStateFileXfer
static bool hcom_host_process_is_stm32f7_dnld_active(hcom_dnld_shared_t *dnldShared)
{
  // These are set during processing of HCOM_MDOW_REQUEST_START_FILE_TRANSFER
  // request message.
  return((dnldShared->dnldCurrentState == HcomStm32F7DnldStateStarting) ||
         (dnldShared->dnldCurrentState == HcomStm32F7DnldStateFileXfer));
}

//==========================================================================
// This function consolidates a lot of the needed processing for file download
// and file delete into a single function instead of being spread all over
// the code base.
static int hcom_host_process_init_write_or_del(hcom_dnld_shared_t *dnldShared,
          const HcomProtoHdrMsg_t *hdrMsg, const size_t packetSize,
          const uint16_t requestType)
{
  int ret;

  // Verify that mono has been disabled
  if(hcom_mono_ctrl_is_mono_enabled())
  {
    char *rqstTypeStr;
    char hostMsg[HCOM_TINY_HOST_STRING_BUFF_LENGTH];

    if(requestType == HCOM_MDOW_REQUEST_START_FILE_TRANSFER ||
        requestType == HCOM_MDOW_REQUEST_MONO_UPDATE_RUNTIME)
    {
      rqstTypeStr = "download";
    }
    else if(requestType == HCOM_MDOW_REQUEST_DELETE_FILE_BY_NAME)
    {
      rqstTypeStr = "delete";
    }
    else
    {
      rqstTypeStr = "access";   // Cover all other bases
    }

    snprintf_chk(hostMsg, HCOM_TINY_HOST_STRING_BUFF_LENGTH,
            "Mono must be disabled for file %s", rqstTypeStr);

    syslog(LOG_ERR, "%s@%d-%s\n",
            __FILE__, __LINE__, hostMsg);

    // Let CLI user know the problem
    hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR, 0,
            hostMsg, __FILE__, __LINE__);

    // Notify CLI that download/delete can't continue because of an error.
    hcom_host_send_header_msg(HCOM_HOST_REQUEST_INIT_DOWNLOAD_FAIL, 0,
            __FILE__, __LINE__);
    return -EPERM;    // Operation not permitted
  }

  // Initialize the shared download struct
#if defined (CONFIG_DIR_MGMT_TESTS)
  syslog(2, "===> %s@%d-Download/delete. Calling hcom_host_process_init_hcom_dnld_share() true,true\n",
            __FILE__, __LINE__);
  usleep(20 * 1000);
#endif

  ret = hcom_host_process_init_hcom_dnld_share(dnldShared, hdrMsg,
            packetSize, true, true);
  if(ret < 0)
  {
    // In case there's some half baked changes
    hcom_dir_mgmt_free_file_info(dnldShared);
    return ret;    // Error already reported via syslog
  }

  // SD-Card file activity must be mounted before it can be accessed
  if(dnldShared->dnldRqstCat == pathnameSdcard)
  {
#if defined (CONFIG_DIR_MGMT_TESTS)
    syslog(LOG_MTEST, "===> %s@%d-Must mount SDCard for file:%s\n",
              __FILE__, __LINE__,
              dnldShared->dnldFullPathName);
    usleep(20 * 1000);
#endif

    // mount(source, target, fstype, mountflags, data)
    // e.g. mount("/dev/mmcsd0", "/sdcard", "vfat", 0, NULL);
    ret = hcom_host_preprocess_mount_sdcard();
    if(ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-ERROR: Mount failed. ret:%d, errno:%d\n",
              __FILE__, __LINE__, ret, errno);
      hcom_dir_mgmt_free_file_info(dnldShared);
      return ret;
    }
  }

  // For the Meadow file system download start, need to do extra initialization
  if(requestType == HCOM_MDOW_REQUEST_START_FILE_TRANSFER ||
      requestType == HCOM_MDOW_REQUEST_MONO_UPDATE_RUNTIME)
  {
    // If there are subdirectories, the number of elements will be > 2 since
    // this is a count of the number of elements
    if(dnldShared->dnldPathNameEleCount > 2)
    {
      ret = hcom_dir_mgmt_check_and_add_subdir(dnldShared);
      if(ret < 0)
      {
        syslog(LOG_ERR, "%s@%d-Processing subdir, ret:%d, errno:%d\n",
                  __FILE__, __LINE__, ret, errno);
        hcom_dir_mgmt_free_file_info(dnldShared);
        return ret;
      }
    }
  }

  return ret;
}

//==========================================================================
// This thread processes all the messages the receive thread has written to
// the circular buffer in this loop
int hcom_host_process_run_loop()
{
  int ret;
  size_t dataLength;

  while (!_shutting_down)
  {
    // Dequeue the next packet received. Also, checks watchdog timer
    ret = hcom_host_enq_deq_dequeue_packet(_packet_dest_buf, &dataLength);
    if(ret < 0)
    {
      if(ret == -ETIME)
      {
        // Watchdog timeout waiting for data
        syslog(LOG_ERR, "%s@%d-Watchdog timeout during download\n",
                  __FILE__, __LINE__);

        // If any download/delete state information, delete it
        hcom_dir_mgmt_free_file_info(_dnldShared);
      }
      else
      {
        syslog(LOG_ERR, "%s@%d-Failed to dequeue message\n",
                  __FILE__, __LINE__);
      }
      continue;
    }

    // We have data. Drop trailing delimiter (via --dataLength), then decode
    // the packet, if it's a full packet. If not a full packet continue
    // saving bytes. Once a full message detected route it for processing.
    size_t decodedPacketSize = hcom_host_cobs_decoder(_packet_dest_buf,
              --dataLength, _decode_dest_buf);
    if(decodedPacketSize == 0)
      continue;

    // Make sure the packet isn't too large
    if(decodedPacketSize > HCOM_PROTOCOL_CURRENT_PACKET_MAX_SIZE)
    {
      char hostMsg[HCOM_SHORT_HOST_STRING_BUFF_LENGTH];

      // Let CLI user know the problem
      snprintf_chk(hostMsg, HCOM_TINY_HOST_STRING_BUFF_LENGTH,
          "Received oversized packet of %lu bytes, ignoring it.",
          decodedPacketSize);
      hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR, 0,
              hostMsg, __FILE__, __LINE__);

      syslog(LOG_ERR, "%s@%d-Oversized packet received (%u bytes)\n",
                __FILE__, __LINE__, decodedPacketSize);
      usleep(20 * 1000);    // Make sure syslog finishes
        
      // Don't return because this will kill our processing loop. Just
      // continue
      continue;
    }

    // Do packet processing
    // This function will completely handle the packet processing
    ret = hcom_host_handle_packet(_dnldShared, _decode_dest_buf,
              decodedPacketSize);
    if (ret < 0)
    {
      // Error message already logged for -EOWNERDEAD errors
      if(ret != -EOWNERDEAD)
      {
        // Let CLI user know the problem
        hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR, 0,
                "Error during download or delete command processing",
                __FILE__, __LINE__);

        syslog(LOG_ERR, "%s@%d-Processing data packet, ret:%d\n",
                  __FILE__, __LINE__, ret);
        usleep(20 * 1000);    // Make sure syslog finishes
      }

      // On error, if any download/delete state information, delete it
      hcom_dir_mgmt_free_file_info(_dnldShared);
    }
  }

  // Thread is exiting, this is not good
  return OK;
}

//============================================================================
// Parse and process received decoded packets as sent by host (CLI).
int hcom_host_handle_packet(hcom_dnld_shared_t *dnldShared,
          const uint8_t *decodedPacket, const size_t decodedSize)
{
  int ret;
  uint16_t requestType;
  uint32_t userData;

  // syslog(LOG_DEBUG, "%s@%d-Data seq:%d, len:%d\n",
  //           __FILE__, __LINE__, hcomDataMsg->seqNumber, decodedSize);

  // decodedPacket points into a long-lived, reused buffer that's never
  // cleared between packets. A short/garbled packet must not be cast to a
  // struct larger than decodedSize -- doing so would read stale bytes left
  // over from the previous real packet instead of failing cleanly.
  if (decodedSize < HCOM_PROTOCOL_DATA_MSG_DATA_INFO_OFF)
  {
    syslog(LOG_ERR, "%s@%d-Received undersized packet of %u bytes, ignoring it.\n",
              __FILE__, __LINE__, decodedSize);
    return -EBADMSG;
  }

  // All messages contain a sequence number field. The sequence number
  // determines if the packet is a command (0) or data (1-n).
  // Assume data type for determination.
  HcomProtoDataMsg_t *hcomDataMsg = (HcomProtoDataMsg_t *) decodedPacket;

  // Test to determine if this is a data packet or a command
  // If Command packet sequence number will be 0
  if (hcomDataMsg->seqNumber != HCOM_PROTOCOL_COMMAND_TYPE_SEQUENCE_NUMBER)
  {
    // This is a Data packet, not a command request
#if HCOM_DIAG_INCLUDE_MESSAGE_DECODING_IN_BUILD > 0
      hcom_diag_decode_data_packet_type(decodedSize);
      usleep(100 * 1000);
#endif
    // What is the current download state? What is active, external flash or
    // ESP32?
    if(hcom_host_process_is_stm32f7_dnld_active(dnldShared))
    {
      // We have data to write to F7 file system
      ret = hcom_file_dnld_stm32f7_recvd_file_data(hcomDataMsg, decodedSize,
                dnldShared);
      if(ret < 0)
      {
        syslog(LOG_ERR, "%s@%d-Receiving data, ret:%d, errno:%d\n",
                  __FILE__, __LINE__, ret, errno);
        return ret;
      }
    }
    else if(hcom_file_dnld_esp32_is_active())
    {
      hcom_file_dnld_esp32_recvd_file_data(hcomDataMsg, decodedSize);
    }
    else
    {
      // CLI is confused or something else is wrong. We should never receive a
      // packet with a sequence number when no download is active.
      syslog(LOG_DEBUG, "%s@%d-Data received but no active download\n",
                __FILE__, __LINE__);
      return -EBADMSG;
    }

    // Finished with preprocessing for this data packet
    return OK;    // Exit as this is a data packet
  }

  // Process Command packet
  //----------------------------------------------------------------------
  // Since sequence number is '0', must be a command (non-data packet)
  // message. And these always have the full HCOM header -- verify enough
  // bytes were actually decoded before reading the rest of it, for the same
  // reason as the length check above.
  if (decodedSize < HCOM_PROTOCOL_HEADER_MSG_LENGTH)
  {
    syslog(LOG_ERR, "%s@%d-Received undersized command packet of %u bytes, ignoring it.\n",
              __FILE__, __LINE__, decodedSize);
    return -EBADMSG;
  }

  const HcomProtoHdrMsg_t *hdrMsg = (HcomProtoHdrMsg_t *) decodedPacket;

#if HCOM_DIAG_INCLUDE_MESSAGE_DECODING_IN_BUILD > 0
  hcom_diag_decode_recvd_message_type(hdrMsg, decodedSize);
  usleep(100 * 1000);
#endif

  // Pull out useful command related values
  userData = hdrMsg->stdHeader.userData;
  requestType = hdrMsg->stdHeader.rqstType;

  // Verify protocol version
  if(hdrMsg->stdHeader.version < HCOM_PROTOCOL_PREFERRED_VERSION_NUMBER)
  {
    char hostMsg[HCOM_SHORT_HOST_STRING_BUFF_LENGTH];
    snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH, 
          "Meadow is expecting a different CLI Protocol version. Please "\
          "update Meadow.CLI. (version received: %04x expected: %04x).",
          hdrMsg->stdHeader.version, HCOM_PROTOCOL_PREFERRED_VERSION_NUMBER);

    syslog(LOG_WARNING, "%s\n", hostMsg);

    uint16_t textRqstType = HCOM_HOST_REQUEST_TEXT_INFORMATION;
    if (hdrMsg->stdHeader.version < HCOM_PROTOCOL_MINIMUM_PROTOCOL_NUMBER)
    {
      textRqstType = HCOM_HOST_REQUEST_TEXT_ERROR;
    }
    else
    {
      // Save the version so we can send the same version back to CLI
      g_current_hcom_protocol_version = hdrMsg->stdHeader.version;
    }

    hcom_host_send_simple_string_msg(textRqstType, 0, hostMsg, __FILE__, __LINE__);
    // If illegal version, exit
    if (textRqstType == HCOM_HOST_REQUEST_TEXT_ERROR)
    {
      // Caller expects a Concluded message for all messages, even for
      // a wrong CLI protocol version.
      hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_CONCLUDED, 0,
              "", __FILE__, __LINE__);
      
      // Exit if below HCOM_PROTOCOL_MINIMUM_PROTOCOL_NUMBER
      return -ENOTSUP;
    }
  }

  //------------------------------------------------------------------
  // Ensure if doing download only a valid command is processed
  if(hcom_host_process_is_stm32f7_dnld_active(dnldShared))
  {
    if(requestType != HCOM_MDOW_REQUEST_END_FILE_TRANSFER &&
       requestType != HCOM_MDOW_REQUEST_MONO_UPDATE_FILE_END)
    {
      syslog(LOG_ERR, "%s@%d-ERROR:F7 Dnld active, unexpected rqst type:0x%04x, dnld state:%d\n",
        __FILE__, __LINE__, requestType, dnldShared->dnldCurrentState);
      return -EBADRQC;   // Invalid request code
    }
  }

  // Allow the file list processing to include a subdirectories if the
  // message type supports it.
  if( requestType == HCOM_MDOW_REQUEST_LIST_FILES_SUBDIR ||
      requestType == HCOM_MDOW_REQUEST_LIST_FILES_SUBDIR_CRC)
  {
    // Need to get the information associated with these list requests. It
    // could be empty or include 1 or more subdirectories, from which a file
    // list may be generated.
    #if defined (CONFIG_DIR_MGMT_TESTS)
    syslog(2, "===> %s@%d-File list. Calling hcom_host_process_init_hcom_dnld_share() false,false\n",
              __FILE__, __LINE__);
    usleep(20 * 1000);
#endif

    ret = hcom_host_process_init_hcom_dnld_share(dnldShared, hdrMsg,
            decodedSize, false, false);
    if(ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-Init file list, ret:%d, errno:%d\n",
                __FILE__, __LINE__, ret, errno);

      return ret;   // On error exit
    }

    // SD-Card file activity must be mounted before it can be accessed
    // Note: if the file list command contains a single '\' this will
    // not mount the SD-Card.
    if(dnldShared->dnldRqstCat == pathnameSdcard)
    {
#if defined (CONFIG_DIR_MGMT_TESTS)
      syslog(LOG_MTEST, "===> %s@%d-Must mount SDCard for file:%s\n",
                __FILE__, __LINE__, dnldShared->dnldFullPathName);
      usleep(20 * 1000);
#endif
      // Use the shared helper (also used by the file write/delete and file
      // read paths below) rather than a raw mount() call: it retries via
      // unmount-then-remount if the SD-Card is already mounted (e.g. from
      // the boot-time auto-mount in meadow_sdmmc.c), which a bare mount()
      // call here would otherwise fail with -ENOTDIR on every subsequent
      // file-list request.
      ret = hcom_host_preprocess_mount_sdcard();
      if(ret < 0)
      {
        syslog(LOG_ERR, "%s@%d-ERROR: Mount failed. ret:%d, errno:%d\n",
                __FILE__, __LINE__, ret, errno);
        return ret;
      }
    }
  }
  // File Read initialization or initial?
  else if(requestType == HCOM_MDOW_REQUEST_UPLOAD_FILE_INIT ||
          requestType == HCOM_MDOW_REQUEST_GET_INITIAL_FILE_BYTES)
  {
#if defined (CONFIG_DIR_MGMT_TESTS)
    syslog(LOG_MTEST,
      "===> %s@%d-Upload. Calling hcom_host_process_init_hcom_dnld_share() false,true\n",
      __FILE__, __LINE__);
    usleep(20 * 1000);
#endif
    
    ret = hcom_host_process_init_hcom_dnld_share(dnldShared, hdrMsg,
            decodedSize, false, true);
    if(ret < 0)
    {
      syslog(LOG_ERR, "%s@%d-Init file list, ret:%d, errno:%d\n",
                __FILE__, __LINE__, ret, errno);

      return ret;   // On error exit
    }

    // If SD-Card file read must be mounted before it can be used
    if(dnldShared->dnldRqstCat == pathnameSdcard)
    {
#if defined (CONFIG_DIR_MGMT_TESTS)
      syslog(LOG_MTEST, "===> %s@%d-Must mount SDCard for file:%s\n",
                __FILE__, __LINE__, dnldShared->dnldFullPathName);
      usleep(20 * 1000);
#endif

      ret = hcom_host_preprocess_mount_sdcard();
      if(ret < 0)
      {
        syslog(LOG_ERR, "%s@%d-ERROR: Mount failed. ret:%d, errno:%d\n",
                __FILE__, __LINE__, ret, errno);
        return ret;
      }
    }
  }
  else if(requestType == HCOM_MDOW_REQUEST_START_FILE_TRANSFER ||
          requestType == HCOM_MDOW_REQUEST_MONO_UPDATE_RUNTIME ||
          requestType == HCOM_MDOW_REQUEST_DELETE_FILE_BY_NAME)
  {
    // Starting a file transfer or delete needs special pre-processing
    // before being routed to the various write and delete functions.
    // This function calls hcom_host_process_init_hcom_dnld_share() to
    // initialize the dnldShared information and if an SD-Card mounts it.
    ret = hcom_host_process_init_write_or_del(dnldShared, hdrMsg,
          decodedSize, requestType);
    if(ret < 0)
    {
      // Don't send another error message if mono not disabled
      if(ret != -EPERM)   // Not Permitted
      {
        syslog(LOG_ERR, "%s@%d-File write/delete, ret:%d, errno:%d\n",
                  __FILE__, __LINE__, ret, errno);
      }


      // These request types need a Concluded message. Why? Because in the
      // normal case the End message will do this. But, on an error the End
      // message cannot be expected.
      hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_CONCLUDED, 0,
        "", __FILE__, __LINE__);

      return ret;   // On error exit
    }
  }
  else if(requestType == HCOM_MDOW_REQUEST_END_FILE_TRANSFER ||
          requestType == HCOM_MDOW_REQUEST_MONO_UPDATE_FILE_END)
  {
    // End of file download
  }

  //-------------------------------------------------------------------
  // All command request types are routed to various functions here
  //-------------------------------------------------------------------

  // Route the message
  ret = hcom_host_route_request_by_cmd_type(hdrMsg, decodedSize, userData,
            requestType, dnldShared);
  if(ret < 0)
  {
    // Handle the command types that report errors
    syslog(LOG_ERR, "%s@%d-Request Type:%u, ret:%d, errno:%d\n",
              __FILE__, __LINE__, requestType, ret, errno);
    // No point exiting now, just do the cleanup first
  }

  //-------------------------------------------------------------------
  // After command routing and execution we may have more work to do
  //-------------------------------------------------------------------

  // These commands indicate that it's time to cleanup from some file
  // related activity.
  if( requestType == HCOM_MDOW_REQUEST_END_FILE_TRANSFER ||
      requestType == HCOM_MDOW_REQUEST_MONO_UPDATE_FILE_END ||
      requestType == HCOM_MDOW_REQUEST_UPLOAD_START_DATA_SEND ||
      requestType == HCOM_MDOW_REQUEST_DELETE_FILE_BY_NAME ||
      requestType == HCOM_MDOW_REQUEST_LIST_FILES_SUBDIR ||
      requestType == HCOM_MDOW_REQUEST_LIST_FILES_SUBDIR_CRC)
  {
    // SD Card's need to be unmounted when a command has mounted it
    if(dnldShared->dnldRqstCat == pathnameSdcard)
    {
#if defined (CONFIG_DIR_MGMT_TESTS)
      syslog(LOG_MTEST, "===> %s@%d-umount successful, cat:%d', file:%s\n",
              __FILE__, __LINE__,
              hcom_file_dir_mgmt_find_category(dnldShared->dnldRqstCat),
              dnldShared->dnldFullPathName);
      usleep(20 * 1000);
#endif
      ret = hcom_host_preprocess_unmount_sdcard(MEADOW_SDCARD_MOUNT_POINT_NAME);
      if(ret < 0)
      {
        // Insure that all allocated memory in dnldShared is freed.
        hcom_dir_mgmt_free_file_info(dnldShared);
        return ret;
      }
    }

    // All finished with the shared download data, time to free the info
    hcom_dir_mgmt_free_file_info(dnldShared);
  }

  return OK;
}
