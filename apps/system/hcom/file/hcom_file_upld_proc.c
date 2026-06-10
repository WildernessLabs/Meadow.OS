/****************************************************************************
 * \apps\system\hcom\file\hcom_file_upld_proc.c
 * 
 *   Copyright (C) 2021-2026 Wilderness Labs. All rights reserved.
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

// The functions in the file setup to upload a file or part of a file to the
// host PC.

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_common.h"
#include <meadow/hcom_protocol.h>
#include <meadow/hcom_shared_common.h>

#include <nuttx/arch.h>

#if defined (CONFIG_DIR_MGMT_TESTS)
#pragma message "(--) hcom_file_upld_proc.c"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HCOM_FILE_UPD_PROC_DIAG_OUTPUT_SYSLOG (0)
#define HCOM_FILE_INITIAL_BYTES_LENGTH (256)

/* Configuration ************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/
enum hcom_upload_data_packet_action
{
  HcomUpldActionNone = 0,
  HcomUpldActionInitialized = 1,
  HcomUpldActionUploading = 2
};

static char *thisFile = __FILE__;
static int _uploadAction;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int hcom_file_upld_proc_build_upload_packet(int fd, char *fileName);

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// Currently, no additional initialization is needed.
int hcom_file_upld_proc_setup()
{
  return OK;
}

//==========================================================================
// The hcom_file_upld_proc_initial_bytes_in_file() feature is rarely used, if
// ever. Originally, it was a way to identify if a file was already downloaded
// to the file system.
//
// This function receives a command from CLI and builds a single message to
// send back to the CLI. This message contains the first part of a file's
// data. The maximum number of bytes is set by HCOM_FILE_INITIAL_BYTES_LENGTH,
// which is defined here.
// This functionality is considered a type of upload so it's in this file.
//==========================================================================
void hcom_file_upld_proc_initial_bytes_in_file(const HcomProtoHdrMsg_t *hdrMsg,
          const size_t packetSize)
{
  int ret;
  int fd;
  char *fileNameBuffer;
  char *fileName;
  
  HcomProtoTextMsg_t *textMsg = (HcomProtoTextMsg_t *)hdrMsg;

  // How long must the file name be
  size_t fileNameLen = packetSize - HCOM_PROTOCOL_TEXT_MSG_START_OFF;

  // Last field in file info is the file name
  fileNameBuffer = malloc(fileNameLen + 1);
  if(fileNameBuffer == NULL)
  {
    syslog(LOG_ERR, "%s@%d-malloc returned NULL\n",
              thisFile, __LINE__);
    return;
  }

  memset(fileNameBuffer, 0, fileNameLen + 1);
  memcpy(fileNameBuffer, textMsg->textData, fileNameLen);

  // Create the name of the mount point part of the file name
  char *fullMountPtName = malloc(HCOM_MAX_PATH_AND_FILE_BUFF_LENGTH);
  if(fullMountPtName == NULL)
  {
    syslog(LOG_ERR, "%s@%d-malloc returned NULL\n", 
              thisFile, __LINE__);
    free(fileNameBuffer);
    return;
  }

  strncpy(fullMountPtName, MONO_MEADOW_FILE_SYSTEM_MOUNTED_NAME,
            HCOM_MAX_PATH_AND_FILE_BUFF_LENGTH);

  fileName = malloc(HCOM_MAX_PATH_AND_FILE_BUFF_LENGTH);
  if(fileName == NULL)
  {
    syslog(LOG_ERR, "%s@%d-malloc returned NULL\n",
              thisFile, __LINE__);
    free(fileNameBuffer);
    free(fullMountPtName);
    return;
  }
  
  snprintf_chk(fileName, HCOM_MAX_PATH_AND_FILE_BUFF_LENGTH, "%s/%s",
                fullMountPtName, fileNameBuffer);
  free(fullMountPtName);

  // Open the file
  fd = open(fileName, O_RDONLY);
  if (fd == -1)
  {
    char hostMsg[HCOM_SHORT_HOST_STRING_BUFF_LENGTH];
    snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH, 
          "File '%s' cannot be opened", fileName);
    hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR, 0, hostMsg,
            thisFile, __LINE__);

    syslog(LOG_ERR, "%s@%d-open '%s', errno:%d\n",
                thisFile, __LINE__, fileName, errno);                
    free(fileNameBuffer);
    free(fileName);
    return;
  }

  // Seek to beginning
  off_t offset = lseek(fd, 0, SEEK_SET);
  if (offset == (off_t)-1)
  {
    char hostMsg[HCOM_SHORT_HOST_STRING_BUFF_LENGTH];
    snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH, 
          "The file '%s' encountered a lseek error", fileName);
    hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR, 0, hostMsg,
            thisFile, __LINE__);

    syslog(LOG_ERR, "%s@%d-lseek failed %s, errno:%d\n",
              thisFile, __LINE__, fileNameBuffer, errno);
    
    close(fd);
    free(fileNameBuffer);
    free(fileName);
    return;
  }

  free(fileNameBuffer);

  // Read first data bytes
  uint8_t *returnBinData = malloc(HCOM_PROTOCOL_COMMAND_MAX_PAYLOAD_LEN);
  if(returnBinData == NULL)
  {
    syslog(LOG_ERR, "%s@%d-malloc returned NULL\n", thisFile, __LINE__);

    close(fd);
    free(fileName);
    return;
  }
  
  ssize_t nbytes;
  int bufOff = 0;
  do
  {
    nbytes = read(fd, returnBinData + bufOff, HCOM_PROTOCOL_COMMAND_MAX_PAYLOAD_LEN - bufOff);
    if (nbytes < 0)
    {
      syslog(LOG_ERR, "%s@%d-read %s, errno:%d\n",
                thisFile, __LINE__, fileName, errno); usleep(20 * 1000);

      close(fd);
      free(returnBinData);
      free(fileName);
      return;
    }

    bufOff += nbytes;
  } while (nbytes > 0);

  // Done with file
  ret = close(fd);
  if (ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-close %s, errno:%d\n",
             thisFile, __LINE__, fileName, errno);

    free(fileName);
    free(returnBinData);
    return;
  }

  // Send the command to host
  hcom_host_send_binary_data_msg(HCOM_HOST_REQUEST_SEND_INITIAL_FILE_BYTES,
            0, returnBinData, bufOff, __FILE__, __LINE__);

  free(fileName);
  free(returnBinData);
}

//=============================================================
// Sending a file to the host requires these steps:
// 1. CLI sends a request to upload a file, the request arrives here,
//    with the file name at  hcom_file_upld_proc_start_file_upload() having
//    the HCOM_MDOW_REQUEST_UPLOAD_FILE_INIT (0x0301) message type.
//
// 2. This module then sends a file information message, back to the CLI
//    with the file's size and CRC checksum, with the request type
//    HCOM_HOST_REQUEST_INIT_UPLOAD_OKAY (0x0310) if everything is good
//    or if for any reason the file cannot be set (e.g. no such file, CRC
//    checksum etc.) the HCOM_HOST_REQUEST_INIT_UPLOAD_FAIL (0x0311)
//    messages is sent instead.
//
// 3. We wait for CLI on the Host PC to respond that it is ready to receive
//    the file data (i.e., target file is opened for writing) by sending the
//    HCOM_MDOW_REQUEST_UPLOAD_START_DATA_SEND (0x0121) request.
//
// 4. The hcom_file_upld_proc_build_upload_packet() function (below) will
//    send 1 - n data packets, each containing a sequency number and binary
//    data for the file's contents. It uses the
//    HCOM_HOST_REQUEST_UPLOADING_FILE_DATA (0x0312) request type
//    to do this.
//
// 5. When all the file data has been sent, the same
//    hcom_file_upld_proc_build_upload_packet() function will send the
//    HCOM_HOST_REQUEST_UPLOAD_FILE_COMPLETED (0x0101) message to the CLI
//    to indicate that the file read (upload) is complete.
//

//=============================================================
// This call is from hcom_host_route.c to start the file read operation
int hcom_file_upld_proc_start_file_upload(hcom_dnld_shared_t *dnldShared)
{
  int detectError;
  uint32_t blockSizeKB;   // Required for call but not used
  uint32_t crc32Checksum = 0;
  off_t fileSize;
  size_t totalMsgLength;
  size_t activeFileNameLen;  
  HcomProtoFileMsg_t *fileMsg;
  char hostMsg[HCOM_SHORT_HOST_STRING_BUFF_LENGTH];   // 128 bytes

#if HCOM_FILE_UPD_PROC_DIAG_OUTPUT_SYSLOG > 0
  syslog(LOG_MDIAG, "%s@%d-CLI request to read '%s'.\n",
    __FILE__, __LINE__, dnldShared->dnldFullPathName);
#endif

  _uploadAction = HcomUpldActionNone;

  // Open the file to upload
  set_errno(0);

  dnldShared->dnldFileFD = open(dnldShared->dnldFullPathName, O_RDONLY);
  if (dnldShared->dnldFileFD == -1)
  {
    if(errno == ENOENT)
    {
      snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH, 
            "File '%s' could not be found in Meadow file system.",
            dnldShared->dnldFullPathName);
    }
    else
    {
      snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH, 
            "File '%s' could not be opened, error:%d",
            dnldShared->dnldFullPathName, errno);
    }

    syslog(LOG_ERR, "%s@%d-Failed to open '%s', errno:%d\n",
                thisFile, __LINE__, dnldShared->dnldFullPathName, errno);

    // This message will notify the user
    hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR,
              0, hostMsg, thisFile, __LINE__);
    
    // This message will stop upload
    hcom_host_send_header_msg(HCOM_HOST_REQUEST_INIT_UPLOAD_FAIL, 0,
              thisFile, __LINE__);

    hcom_dir_mgmt_free_file_info(dnldShared);

    return -errno;
  }

  // Calculate the CRC checksum
  crc32Checksum = hcom_file_misc_calc_crc_for_file_fd(dnldShared->dnldFileFD,
          dnldShared->dnldFullPathName,
          &fileSize, &blockSizeKB, &detectError);
  if(detectError < 0)
  {
    snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH, 
          "%s@%d-Read CRC calculation error:%d\n", thisFile, __LINE__, detectError);

    syslog(LOG_ERR, "%s@%d-CRC calculation error:%d\n",
              thisFile, __LINE__, detectError);

    // This message will notify the user
    hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR,
              0, hostMsg, thisFile, __LINE__);
    
#if HCOM_FILE_UPD_PROC_DIAG_OUTPUT_SYSLOG > 0
    syslog(LOG_MDIAG, "Up--> %s()%s@%d-Calc of crc error. Stopping CLI.\n",
      __func__, __FILE__, __LINE__);
#endif

    // This message will stop upload
    hcom_host_send_header_msg(HCOM_HOST_REQUEST_INIT_UPLOAD_FAIL, 0,
              thisFile, __LINE__);

    hcom_dir_mgmt_free_file_info(dnldShared);

    return -errno;
  }

#if HCOM_FILE_UPD_PROC_DIAG_OUTPUT_SYSLOG > 0
  syslog(LOG_MDIAG, "Up-->%s@%d-start_file_upload, Checksum calc ok\n",
        thisFile, __LINE__); usleep(20 * 1000);
#endif

  // Report to the host upload success thus far and wait for a response
  activeFileNameLen = strlen(dnldShared->dnldFullPathName) + 1;
  totalMsgLength = activeFileNameLen + HCOM_PROTOCOL_FILE_MSG_LENGTH;
  
  fileMsg = (HcomProtoFileMsg_t *)malloc(totalMsgLength);
  if(fileMsg == NULL)
  {
    snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH,
              "%s@%d-malloc returned NULL\n", thisFile, __LINE__);
    syslog(LOG_ERR, hostMsg);

    // This message will notify the user
    hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR,
              0, hostMsg, thisFile, __LINE__);
    
    // This message will stop upload
    hcom_host_send_header_msg(HCOM_HOST_REQUEST_INIT_UPLOAD_FAIL, 0,
              thisFile, __LINE__);

    hcom_dir_mgmt_free_file_info(dnldShared);

    return -errno;
  }

  fileMsg->fileInfo.fileSize = fileSize;
  fileMsg->fileInfo.fileCheckSum = crc32Checksum;
  fileMsg->stdHeader.extraData = 0;
  fileMsg->stdHeader.userData = 0;
  fileMsg->stdHeader.rqstType = HCOM_HOST_REQUEST_INIT_UPLOAD_OKAY;

  // Copy the file name to the end of the message structure
  memcpy(fileMsg->fileInfo.fileName, dnldShared->dnldFullPathName,
            activeFileNameLen);

#if HCOM_FILE_UPD_PROC_DIAG_OUTPUT_SYSLOG > 0
  syslog(LOG_MDIAG,
    "Up-->File CRC is:0x%08lx, filesize:%lu, filename:'%s'. Sending 'Init upload OK' to HOST\n",
    crc32Checksum, fileSize, fileMsg->fileInfo.fileName);
#endif

  // This message contains what the host needs to start receiving a file
  hcom_host_send_std_header_msg((HcomProtoHdrMsg_t *)fileMsg,
            totalMsgLength, thisFile, __LINE__);

  free(fileMsg);
  _uploadAction = HcomUpldActionInitialized;

  return OK;
}

//==================================================================
// This function receives a command from the HOST CLI to upload file data
// This function is called after the CLI has a chance to process the above
// successfully. This will upload all the files data and send an end message.
//==================================================================
int hcom_file_upld_proc_begin_file_uploading(hcom_dnld_shared_t *dnldShared)
{
  int ret;
  char hostMsg[HCOM_SHORT_HOST_STRING_BUFF_LENGTH];   // 128 bytes

#if HCOM_FILE_UPD_PROC_DIAG_OUTPUT_SYSLOG > 0
  syslog(LOG_MDIAG, "%s@%d-CLI is Ready to start file read. Filename:'%s'\n",
    thisFile, __LINE__, dnldShared->dnldFullPathName);
#endif

  if(_uploadAction != HcomUpldActionInitialized)
  {
    snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH,
              "%s@%d-Must initialize upload before each upload.\n",
              thisFile, __LINE__);
    syslog(LOG_ERR, hostMsg, thisFile, __LINE__);

    // This message will notify the user
    hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR,
              0, hostMsg, thisFile, __LINE__);
    
    // This message will stop CLI uploading
    hcom_host_send_header_msg(HCOM_HOST_REQUEST_INIT_UPLOAD_FAIL, 0,
              thisFile, __LINE__);
    return -ENODATA;    // No data available
  }

  _uploadAction = HcomUpldActionUploading;

  // Use the information from start initialize and begin uploading
  ret = hcom_file_upld_proc_build_upload_packet(dnldShared->dnldFileFD,
            dnldShared->dnldFullPathName);
  if(ret < 0)
  {
    snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH, 
          "File '%s' data upload failed", dnldShared->dnldFullPathName);
    hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR, 0, hostMsg,
            thisFile, __LINE__);

    _uploadAction = HcomUpldActionNone;

    hcom_dir_mgmt_free_file_info(dnldShared);
    return ret;
  }

  // Finished with file upload, cleanup
  _uploadAction = HcomUpldActionNone;
  hcom_dir_mgmt_free_file_info(dnldShared);

  return OK;
}

//==================================================================
// This function builds and uploads the files contents.
int hcom_file_upld_proc_build_upload_packet(int fd, char *fileName)
{
  uint16_t sequenceNumb = 1;
  HcomProtoBinMsg_t *binMsg;    // Type has std header + bin data
  char hostMsg[HCOM_SHORT_HOST_STRING_BUFF_LENGTH];   // 128 bytes

#if HCOM_FILE_UPD_PROC_DIAG_OUTPUT_SYSLOG > 0
  syslog(LOG_MDIAG, "Up-->%s@%d-Start building and uploading packets\n", thisFile, __LINE__);
#endif

  // Seek to beginning
  off_t offset = lseek(fd, 0, SEEK_SET);
  if (offset == (off_t)-1)
  {
    snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH, 
          "The file '%s' encountered a lseek error", fileName);

    syslog(LOG_ERR, "%s@%d-lseek failed %s, fd:%d, errno:%d\n",
              thisFile, __LINE__, fileName, fd, errno);

    hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_ERROR,
              0, hostMsg, thisFile, __LINE__);
    return -errno;
  }

  // Buffer to hold header + binary file data
  binMsg = (HcomProtoBinMsg_t *)malloc(HCOM_PROTOCOL_CURRENT_PACKET_MAX_SIZE);
  if(binMsg == NULL)
  {
    syslog(LOG_ERR, "%s@%d-malloc returned NULL\n",
              thisFile, __LINE__);
    return -ENOMEM;
  }

  // Send all the file data
  ssize_t nbytes;
#if HCOM_FILE_UPD_PROC_DIAG_OUTPUT_SYSLOG > 0
  ssize_t totalSent = 0;    // diag
  int sentCount = 1;        // diag
#endif

  // Loop through entire file. Too simple? It reads the file until the read
  // indicates no more file data by returning '0'.
  do
  {
#if HCOM_FILE_UPD_PROC_DIAG_OUTPUT_SYSLOG > 0
    syslog(LOG_MDIAG, "Up-->%s@%d-uploading packet:%d\n",
      thisFile, __LINE__, sentCount);
#endif

    // Read data into the buffer after the header for maximum payload bytes
    nbytes = read(fd, binMsg->binData, HCOM_PROTOCOL_COMMAND_MAX_PAYLOAD_LEN);
    if (nbytes < 0)
    {
      syslog(LOG_ERR, "%s@%d-read %s, errno:%d\n",
                thisFile, __LINE__, fileName, errno);
      free(binMsg);
      return -errno;
    }

    binMsg->stdHeader.rqstType = HCOM_HOST_REQUEST_UPLOADING_FILE_DATA;
    if(nbytes > 0)
    {
      // Send the data to the host
#if HCOM_FILE_UPD_PROC_DIAG_OUTPUT_SYSLOG > 0
      sentCount++;
#endif
      binMsg->stdHeader.seqNumber = sequenceNumb++;
      binMsg->stdHeader.userData = 0;
      binMsg->stdHeader.extraData = 0;

      // This call will build the message and send it to the host.
      // Length must include header + data and be <= to max payload size.
      // Note: This call will wait longer than normal for the data to be
      // sent
      hcom_host_send_std_data_msg_long_wait((HcomProtoHdrMsg_t *)binMsg,
                HCOM_PROTOCOL_HEADER_MSG_LENGTH + nbytes,
                thisFile, __LINE__);

#if HCOM_FILE_UPD_PROC_DIAG_OUTPUT_SYSLOG > 0
      totalSent += nbytes;
#endif
    }
  } while (nbytes > 0);   // nbytes == 0 stop

  free(binMsg);

  // ---------------------------------------------------------------
  // Send the end message to CLI
  uint8_t msgBuf[HCOM_PROTOCOL_HEADER_MSG_LENGTH];
  HcomProtoHdrMsg_t *endHdrMsg = (HcomProtoHdrMsg_t *)msgBuf;
  endHdrMsg->stdHeader.rqstType = HCOM_HOST_REQUEST_UPLOAD_FILE_COMPLETED;
  endHdrMsg->stdHeader.userData = 0;
  endHdrMsg->stdHeader.extraData = 0;

  // Report to hosts that the entire file has been sent
  hcom_host_send_std_header_msg(endHdrMsg,
    HCOM_PROTOCOL_HEADER_MSG_LENGTH, thisFile, __LINE__);

  snprintf_chk(hostMsg, HCOM_SHORT_HOST_STRING_BUFF_LENGTH, 
        "File '%s' successfully sent to CLI", fileName);
  hcom_host_send_simple_string_msg(HCOM_HOST_REQUEST_TEXT_INFORMATION,
            0, hostMsg, thisFile, __LINE__);

#if HCOM_FILE_UPD_PROC_DIAG_OUTPUT_SYSLOG > 0
  syslog(LOG_MDIAG, "%s@%d-'%s'\n", thisFile, __LINE__, hostMsg);
#endif

  return OK;
}

//==========================================================================
// NOT IMPLEMENTED
void hcom_file_upld_proc_abort_file_upload(const HcomProtoHdrMsg_t *hdrMsg,
          const size_t packetSize, uint32_t partitionId)
{
  
}
