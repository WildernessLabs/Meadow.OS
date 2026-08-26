/****************************************************************************
 * \apps\examples\hcom\comms\hcom_host_send.c
 * 
 *   Copyright (C) 2019 - 2026 Wilderness Labs. All rights reserved.
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

// Both high level functions that prepare a message to be successfully sent
// and low level functions that do the transmission to the host PC or Mac
// are in this module.

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "../hcom_common.h"
#include <meadow/hcom_protocol.h>
#include <meadow/meadow_extended_errno.h>
#include <meadow/hcom_bbreg_defn.h>
#include <poll.h>

// #pragma GCC optimize("O0")    // Prevent code optimization

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
// Build code to add diagnostic syslog messages in this module.
#define HCOM_HOST_SEND_ADD_SYSLOG_IN_BUILD (0)

#define HCOM_XMIT_MAX_BLOCKED_TIME_DELAY  (50 * 1000) // 50 millisec

// Have seen a stall last as long as 19.5 seconds in the wild (this statement
// was made with an early version of CLI and may no longer be the case).
// The long budget is for reading files from Meadow and sending to CLI.
#define HCOM_XMIT_MAX_BLOCKED_MSEC_LONG   30000   // 30.0 seconds
#define HCOM_XMIT_MAX_BLOCKED_MSEC_NORMAL  1500   // 1.5 seconds

// Floor on real elapsed time per blocked retry. Confirmed on hardware that
// poll(POLLOUT) can report the fd writable (or time out) in under 1ms total
// across 30 retries, with write() still failing every time. This floor
// guarantees each blocked retry costs real time, so the elapsed-time budget
// below actually elapses.
#define HCOM_XMIT_MIN_RETRY_DELAY_USEC (2 * 1000)     // 2 millisec

#define HCOM_COMMS_MAX_XMIT_OPEN_ATTEMPTS 4

/* Configuration ************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/
static char *thisFile = __FILE__;

static bool _shutting_down;
static pid_t _hcomTaskGroup;
static int _comms_write_fd;
static uint8_t *_encodedXmitBuff;

// Scratch buffer for hcom_host_send_buffered_msg(), lazily allocated on
// first use rather than at setup(). This comms path only matters while
// Meadow.CLI is connected -- almost always a brief window during initial
// site provisioning -- so most deployed devices never send a buffered
// message and never pay this cost at all. Once allocated it's kept for the
// life of the process (freed on shutdown; the memory is returned to the
// system on the next reboot regardless), avoiding malloc/free churn on every
// subsequent call within the same session. Access is already serialized by
// _hostXmitSem, so no new locking is needed for the lazy-init check either.
static uint8_t *_bufferedMsgXmitBuff;

static sem_t _hostXmitSem;    // Implements event waiting
static bool _lastXmitBlocked;
static bool _notInitialized = true;
static bool _lowPowerActive;

/****************************************************************************
 * Global Data
 ****************************************************************************/

// Store the current protocol number being used.  We will start off with the
// preferred protocol version but allow the system to downgrade the protocol
// dynamically if required in the future.
uint16_t g_current_hcom_protocol_version = HCOM_PROTOCOL_PREFERRED_VERSION_NUMBER;

// Store the maximum protocol packet size for the currently selected protocol.
uint16_t g_current_hcom_maximum_packet_size = HCOM_PROTOCOL_CURRENT_PACKET_MAX_SIZE;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void hcom_host_send_build_msg_header(uint16_t requestType, uint16_t extraData,
          uint32_t userData, uint8_t *xmitBuffer);
static int hcom_host_send_buffered_msg(uint16_t requestType, uint16_t extraData,
          uint32_t userData, uint8_t *msgBuffer, size_t msgLength);
static int hcom_host_send_standard_msg(HcomProtoHdrMsg_t *hdrMsg,
          size_t totalLength, uint32_t maxBlockedMs);
static int hcom_host_send_transmit_to_host(FAR uint8_t xmitBuffer[],
  size_t xmitLength, uint32_t maxBlockedMs);
static bool hcom_host_send_is_host_xmit_blocked(void);
static int hcom_host_send_low_power_notification(bool lpStart);
static int hcom_host_send_open_transmit_connection(void);
static void hcom_host_send_transmit_takesem(sem_t *semaphore);
static void syslog_if_safe(int priority, FAR const IPTR char *fmt, ...);

/****************************************************************************
 * Public Functions
 ****************************************************************************/
// Public
int hcom_host_send_setup()
{
  int ret;

  // We assume this function is ONLY called at startup up from
  // hcom_startup_manager.c
  _hcomTaskGroup = getpid();
  _comms_write_fd = -1;
  _lastXmitBlocked = true; // Assume blocked
  _lowPowerActive = false;

  _encodedXmitBuff = malloc(HCOM_PROTOCOL_SAFE_ENCODED_MSG_BUF_SIZE);
  if(_encodedXmitBuff == NULL)
  {
    syslog(LOG_ERR, "%s@%d-malloc returned NULL\n", thisFile, __LINE__);
    return -ENOMEM;
  }

  // _bufferedMsgXmitBuff is intentionally NOT allocated here -- see its
  // declaration and hcom_host_send_buffered_msg() for why.

  sem_init(&_hostXmitSem, 0, 1);

  // Register with power management so we can properly shutdown before entering
  // a low-power mode.
  ret = hcom_via_nx_register_pwr_mgmt_callback(hcom_host_send_low_power_notification);
  if(ret < 0)
  {
    syslog(LOG_ERR, "%s@%d-Registering for pwr mgmt:%d\n", thisFile, __LINE__, ret);
    return ret;
  }

  _notInitialized = false;
  return OK;
}

//--------------------------------------------------------------------
// Public
void hcom_host_send_shutdown()
{
  _shutting_down = true;

  close(_comms_write_fd);
  _comms_write_fd = -1;
  _lastXmitBlocked = true;

  free(_encodedXmitBuff);
  free(_bufferedMsgXmitBuff);

  // use sem_destroy
  sem_destroy(&_hostXmitSem);
}

//=======================================================================
// This will be called when entering and leaving low-power mode
int hcom_host_send_low_power_notification(bool lpStart)
{
  // After spending a lot of time attempting to fix the problems caused by
  // being in low-power mode, found that it wasn't possible to fix the problem
  // in this module. Added this flag so that a transmission failure caused by
  // being in low-power mode could be identified and the proper action taken
  // to allow the message to be resent.
  if(lpStart)
  {
    // Wait till nothing is being sent and prevent additional sending
    hcom_host_send_transmit_takesem(&_hostXmitSem);
    if(_comms_write_fd > 0)
    {
      // If there's a valid fd close and re-open on first msg after return to
      // non-sleep state.
      close(_comms_write_fd);
      _comms_write_fd = -1;
      _lastXmitBlocked = true;
    }

    // Only used to detect if errno == ENOTCONN
    _lowPowerActive = true;
  }
  else
  {
    // Prevent output till sleep ends. Probably not needed since everything
    // is sleeping.
    sem_post(&_hostXmitSem);
  }
  return OK;
}

//=====================================================================
// Is CLI to be receiving text messages?
static bool hcom_trace_is_sending_to_cli(void)
{
  if(hcom_bbreg_is_bbr_bit_set(HCOM_BBREG_ROUTE_TRACE_MSG_TO_HOST_BIT))
    return true;
  else
    return false;
}

//=====================================================================
// FUNCTION TO USE WHEN SENDING ALL STANDARD MESSAGE WITH ONLY HEADER
// Note: This function is a step towards standardizing the protocol.
//
// This function can be used after the caller has properly populated the
// message header and wants the Protocol Version, sequence added.
// The actual message type is any standard message type. The full length must
// be allocated (header + data). And this value reflected in totalMsgLen.
//
// The caller uses one of the structs defined in
// /nuttx/include/meadow/hcom_protocol.h. Any of those containing the
// HcomProtoStdHdr_t type (e.g. HcomProtoTextMsg_t, HcomProtoHdrMsg_t,
// HcomProtoBinMsg_t, etc.) can be used. The caller populates the proper struct
// fields and downcasts the type to a HcomProtoStdHdr_t and passes this as
// 'hdrMsg' to this function.

// This function currenly only used in file read (aka upload) because CLI can
// stall for up to 20 seconds while processing received data.
// Public
int hcom_host_send_std_header_msg(HcomProtoHdrMsg_t *hdrMsg,
          size_t totalMsgLen, char *sourceFileName, int sourceLineNumber)
{
  int ret = OK;

  // Caller must sets for non-data packet
  // stdHeader.rqstType = HCOM_HOST_REQUEST_XXXXX_XXXX_XXXX;
  // stdHeader.userData = 0;
  // stdHeader.extraData = 0;

  // These are always the same values for non-data
  hdrMsg->stdHeader.seqNumber = HCOM_PROTOCOL_COMMAND_TYPE_SEQUENCE_NUMBER;
  hdrMsg->stdHeader.version = g_current_hcom_protocol_version;

  // EAGAIN is not an error it means the message was blocked
  ret = hcom_host_send_standard_msg(hdrMsg, totalMsgLen,
    HCOM_XMIT_MAX_BLOCKED_MSEC_NORMAL);
  if (ret < 0 && ret != -EAGAIN)
      syslog_if_safe(LOG_ERR, "%s@%d-Host xmit err:%d\n",
        sourceFileName, sourceLineNumber, ret);

  return ret;
}

//-------------------------------------------------------------------
// FUNCTION TO USE WHEN SENDING A STANDARD MESSAGE WITH MORE THEN JUST A HEADER
// Data messages need a non-zero sequence number
// Public
int hcom_host_send_std_data_msg_long_wait(HcomProtoHdrMsg_t *hdrMsg,
        size_t totalMsgLen, char *sourceFileName, int sourceLineNumber)
{
  int ret = OK;

  // Caller must set these for data packet
  // stdHeader.rqstType = HCOM_HOST_REQUEST_XXXXX_XXXX_XXXX;
  // stdHeader.userData = 0;
  // stdHeader.extraData = 0;
  // For file data the seqNumber is set 1 - n.
  // For non-file data it is set to 0.
  // hdrMsg->stdHeader.seqNumber = ?;

  hdrMsg->stdHeader.version = g_current_hcom_protocol_version;

  // EAGAIN is not an error it means the message was blocked
  ret = hcom_host_send_standard_msg(hdrMsg, totalMsgLen,
    HCOM_XMIT_MAX_BLOCKED_MSEC_LONG);
  if (ret < 0 && ret != -EAGAIN)
      syslog_if_safe(LOG_ERR, "%s@%d-Host xmit err:%d\n",
        sourceFileName, sourceLineNumber, ret);

  return ret;
}

//-------------------------------------------------------------------
// FUNCTION TO USE WHEN SENDING A STANDARD MESSAGE WITH MORE THEN JUST A HEADER
// Data messages need a non-zero sequence number
// Public
int hcom_host_send_std_data_msg(HcomProtoHdrMsg_t *hdrMsg,
          size_t totalMsgLen, char *sourceFileName, int sourceLineNumber)
{
  int ret = OK;

  // Caller must set these for data packet
  // stdHeader.rqstType = HCOM_HOST_REQUEST_XXXXX_XXXX_XXXX;
  // stdHeader.userData = 0;
  // stdHeader.extraData = 0;
  // For file data the seqNumber is set 1 - n.
  // For non-file data it is set to 0.
  // hdrMsg->stdHeader.seqNumber = ?;

  hdrMsg->stdHeader.version = g_current_hcom_protocol_version;

  // EAGAIN is not an error it means the message was blocked
  ret = hcom_host_send_standard_msg(hdrMsg, totalMsgLen,
    HCOM_XMIT_MAX_BLOCKED_MSEC_NORMAL);
  if (ret < 0 && ret != -EAGAIN)
      syslog_if_safe(LOG_ERR, "%s@%d-Host xmit err:%d\n",
        sourceFileName, sourceLineNumber, ret);

  return ret;
}

//=====================================================================
// This function is like the hcom_host_send_buffered_msg() function.
// The difference is, since the protocol is now simpler, because of using
// structs to define the message being sent. Therefore, this function
// eliminates the need for hcom_host_send_buffered_msg().
// The messy work eliminated by structures by the caller.
int hcom_host_send_standard_msg(HcomProtoHdrMsg_t *hdrMsg,
          size_t totalLength, uint32_t maxBlockedMs)
{
  int ret;

  if(_notInitialized)
    return -EAGAIN;

  // Only one thread / message at a time can be sent to host
  hcom_host_send_transmit_takesem(&_hostXmitSem);

  if(hcom_host_send_is_host_xmit_blocked())
  {
    sem_post(&_hostXmitSem);
    return OK;   // Throw the message away. What else can be done?
  }

  // Send the message which may include data
  ret = hcom_host_send_transmit_to_host((uint8_t *)hdrMsg, totalLength,
    maxBlockedMs);

  sem_post(&_hostXmitSem);
  return ret;
}

//=====================================================================
// The following are first generation functions for sending data.
//=====================================================================
// They have been superseded by the above hcom_host_send_std_data_msg()
// function. However, the time to refactor the code they support has never
// been made available.
//=====================================================================
// Deprecated, best to use hcom_host_send_std_msg_header_only
// THIS IS THE FUNCTION THAT SHOULD BE USED WHEN JUST SENDING A HEADER
// Just sends a header message and report the error here
// Public
void hcom_host_send_header_msg(uint16_t requestType, uint32_t userData,
      char *sourceFileName, int sourceLineNumber)
{
  int ret = hcom_host_send_buffered_msg(requestType, 0, userData, NULL, 0);
  if (ret < 0 && ret != -EAGAIN) // EAGAIN is not an error it means the message was blocked
    syslog_if_safe(LOG_ERR, "%s@%d-Host xmit err:%d\n",
      sourceFileName, sourceLineNumber, ret);
}

//=====================================================================
// Deprecated, best to use hcom_host_send_std_data_msg
// FUNCTION TO USE WHEN SENDING BINARY DATA WITH HEADER
// Prepare a bytes for transmission
// Public
void hcom_host_send_binary_data_msg(uint16_t requestType, uint32_t userData,
        uint8_t *bytes, size_t msgLength, char *sourceFileName, int sourceLineNumber)
{
  int ret = hcom_host_send_buffered_msg(requestType, 0, userData, bytes, msgLength);
  if (ret < 0 && ret != -EAGAIN) // EAGAIN is not an error it means the message was blocked
      syslog_if_safe(LOG_ERR, "%s@%d-Host xmit err:%d\n",
                sourceFileName, sourceLineNumber, ret);
}

//=====================================================================
// Deprecated, best to use hcom_host_send_std_data_msg
// THIS IS THE FUNCTION THAT SHOULD BE USED FOR ALL SIMPLE TEXT MESSAGE
// Prepare a simple line of text for transmission and output the error message here
// Public
void hcom_host_send_simple_string_msg(uint16_t requestType, uint32_t userData,
           char *shortText, char *sourceFileName, int sourceLineNumber)
{
  // Need to remove any trailing cr/lf. If none found strcspn() finds terminating '\0'
  // returning its offset.
  size_t trueStrLen = strcspn(shortText, "\r\n");

  int ret = hcom_host_send_buffered_msg(requestType, 0, userData, (uint8_t*) shortText, trueStrLen);
  if (ret < 0 && ret != -EAGAIN) // EAGAIN is not an error it means the message was blocked
    syslog_if_safe(LOG_ERR, "%s@%d-Host xmit err:%d\n", sourceFileName, sourceLineNumber, ret);
}

//=====================================================================
// Deprecated, best to use hcom_host_send_std_data_msg
// THIS IS THE FUNCTION THAT SHOULD BE USED WHEN SPECIAL CIRCUMSTANCES EXIST
// Prepare a string for transmission, allowing any character
// This is called for various internal needs (e.g. mono redirect, diagnostic).
// Public
int hcom_host_send_raw_string_msg(uint16_t requestType, uint32_t userData,
          char *shortText, size_t msgLength,
          char *sourceFileName, int sourceLineNumber)
{
  int ret = hcom_host_send_buffered_msg(requestType, 0, userData, (uint8_t*) shortText, msgLength);
  if (ret < 0 && ret != -EAGAIN) // EAGAIN is not an error it means the message was blocked
  {
      syslog_if_safe(LOG_ERR, "%s@%d-Host xmit err:%d\n",
                sourceFileName, sourceLineNumber, ret);
  }

  return ret;
}

//=====================================================================
// Deprecated, best to use hcom_host_send_std_data_msg
//
// This function is intended to be the sole and final entry point for
// messages that needed to be sent to Meadow.CLI. Use one of the above
// to access this function.
// Requirements: needs to efficiently handle message transmission in 3
// situations: 1) actively communicating with the CLI, 2) connected to
// host PC and CLI is not communicating and 3) Meadow is not connected
// to a host PC, this is the most normal situation.
int hcom_host_send_buffered_msg(uint16_t requestType, uint16_t extraData,
        uint32_t userData, uint8_t *origMsg, size_t msgLength)
{
  int ret;

  if(_notInitialized)
    return -EAGAIN;

  // Only one thread / message at a time can be sent to host
  hcom_host_send_transmit_takesem(&_hostXmitSem);

  // hcom_host_send_is_host_xmit_blocked() MUST be called before calling
  // hcom_host_send_transmit_to_host() to send a message to the host.
  // hcom_host_send_is_host_xmit_blocked() verifies that transmission is
  // possible. That is, the host PC can be connected to and that messages
  // are being received (not blocked). If it returns true (blocked)
  // then transmission is not possible at this time.
  if(hcom_host_send_is_host_xmit_blocked())
  {
    // This is a normal occurrence since the host is usually not connected
    sem_post(&_hostXmitSem);
    return OK;   // Throw the message away. What else can be done?
  }

  size_t fullMsgLen = msgLength + HCOM_PROTOCOL_HEADER_MSG_LENGTH;
  if(fullMsgLen > HCOM_PROTOCOL_CURRENT_PACKET_MAX_SIZE)
  {
    // Truncate to fit
    fullMsgLen = HCOM_PROTOCOL_CURRENT_PACKET_MAX_SIZE;
  }

  // Is this a header only message or a message with a body
  if(msgLength > 0)
  {
    // Lazily allocate _bufferedMsgXmitBuff on first use (see its
    // declaration for why), then keep reusing it instead of malloc/free per
    // call. Safe without new locking: this function already holds
    // _hostXmitSem for its whole duration.
    if(_bufferedMsgXmitBuff == NULL)
    {
      _bufferedMsgXmitBuff = malloc(HCOM_PROTOCOL_CURRENT_PACKET_MAX_SIZE);
      if(_bufferedMsgXmitBuff == NULL)
      {
        syslog(LOG_ERR, "%s@%d-malloc returned NULL\n", thisFile, __LINE__);
        sem_post(&_hostXmitSem);
        return -ENOMEM;
      }
    }

    // Uses the first part of message buffer for header
    hcom_host_send_build_msg_header(requestType, extraData, userData, _bufferedMsgXmitBuff);

    // Copy the body of the message
    memcpy(_bufferedMsgXmitBuff + HCOM_PROTOCOL_HEADER_MSG_LENGTH, origMsg, msgLength);

    // Send the header and the body
    ret = hcom_host_send_transmit_to_host(_bufferedMsgXmitBuff, fullMsgLen,
      HCOM_XMIT_MAX_BLOCKED_MSEC_NORMAL);
  }
  else
  {
    // Small so use the stack for space
    uint8_t headerOnlyMsg[HCOM_PROTOCOL_HEADER_MSG_LENGTH];

    // Uses the first part of message buffer for header
    hcom_host_send_build_msg_header(requestType, extraData, userData, headerOnlyMsg);

    // Send the message without a body, just the header
    ret = hcom_host_send_transmit_to_host(headerOnlyMsg, fullMsgLen,
      HCOM_XMIT_MAX_BLOCKED_MSEC_NORMAL);
  }

  sem_post(&_hostXmitSem);
  return ret;
}

//=====================================================================
// Build the xmit header in the provided transmit buffer
void hcom_host_send_build_msg_header(uint16_t requestType,
        uint16_t extraData, uint32_t userData, uint8_t *xmitBuffer)
{
  HcomProtoHdrMsg_t *hdrMsg = (HcomProtoHdrMsg_t *)xmitBuffer;
  hdrMsg->stdHeader.seqNumber = HCOM_PROTOCOL_COMMAND_TYPE_SEQUENCE_NUMBER;
  hdrMsg->stdHeader.version = g_current_hcom_protocol_version;
  hdrMsg->stdHeader.rqstType = requestType;
  hdrMsg->stdHeader.extraData = extraData;
  hdrMsg->stdHeader.userData = userData;
}
//=====================================================================
// End of generation one send functions
//=====================================================================

// Attempt to open the connection to the host PC
int hcom_host_send_open_transmit_connection()
{
  if(_comms_write_fd > 1)
    return OK;

  int openAttempts;

  // This will attempt to open the USB/ACM Serial port on the Meadow end
  for(openAttempts = 0; openAttempts < HCOM_COMMS_MAX_XMIT_OPEN_ATTEMPTS; openAttempts++)
  {
    _comms_write_fd = open(hcom_host_recv_get_device_name(), O_WRONLY | O_NONBLOCK);
    if(_comms_write_fd >= 0)
    {
      // If there's no CLI or equal running open will still be successful,
      // as long as the Host PC opens the correct USB Serial port.
      return OK;
    }

    // Wait and try again
    usleep(250 * 1000);
  }
  
  _lastXmitBlocked = true;
  return _comms_write_fd;   // This indicates error
}

//=====================================================================
// Usually, no host PC is running and/or connected. When this is the case these
// messages eventually will be blocked (after filling nuttx internal buffers).
// To workaround this, once we get a -EAGAIN error (i.e. blocked) we'll attempt
// to send 0x00 before every future message. This way, when the host PC begins to
// consume messages our 0x00 will be the first thing to arrive after whatever nuttx
// has internally buffered (which could be a partial message). The CLI ignores a
// single 0x00 byte message. Therefore, the first message sent, after the host
// connects, will be sent successfully and be properly parsed.
// The partially sent (corrupted) messages will be thrown away by the Meadow.CLI
// (or at least should be) after it reports an error.
//
bool hcom_host_send_is_host_xmit_blocked()
{
  int ret;

  // Last attempt was NOT blocked. Caller should attempt to send.
  if(!_lastXmitBlocked)
  {
    return false;
  }

  // Is the connection opened?
  if(_comms_write_fd == -1)
  {
    ret = hcom_host_send_open_transmit_connection();
    if(ret < 0)
    {
      // This is where message are ignored if there's no host 
      // PC connected. Can't connect.
      return true;    // Report blocked
    }
  }

  // Send a '0' as it's a message that the host knows to ignore. '0' is
  // the cots protocol framing character, so it does no harm.
  uint8_t oneZero[1];
  oneZero[0] = '\0';
  ssize_t writeRet = write(_comms_write_fd, &oneZero, 1);
  if(writeRet == 1)
  {
    // Write successful, no longer blocked
    _lastXmitBlocked = false;
    return false;   // Not blocked
  }
  
  // This is where we exit if the host PC exists but CLI (or equal)
  // is not running (i.e. not consuming characters).
  _lastXmitBlocked = true;
  return true;    // blocked or some error
}

//===================================================================================
// All messages sent to host use this function.
int hcom_host_send_transmit_to_host(FAR uint8_t xmitBuffer[],
  size_t xmitLength, uint32_t maxBlockedMs)
{
  size_t remainingBytes;
  size_t toWriteOffset = 0;
  size_t blockedCount = 0;
  struct timespec blockedSince;   // When the CURRENT contiguous stall began
  bool isBlocked = false;         // Tracks whether blockedSince is live

#if (HCOM_HOST_SEND_ADD_SYSLOG_IN_BUILD > 0)
  // Diagnostic only -- lets the success path below report how much real
  // time a send that had to retry actually cost.
  struct timespec sendStart;
  clock_gettime(CLOCK_MONOTONIC, &sendStart);
#endif

  // Only a thread, from the same task group as hcom, can send messages
  if(getpid() != _hcomTaskGroup)
  {
    syslog(LOG_ERR, "%s@%d-Caller's Task Group %d is wrong, Task Group %d required.\n",
      __FILE__, __LINE__, getpid(), _hcomTaskGroup);

    return -EMEADOW_TASK_GRP_MISMATCH;
  }

  if(_shutting_down)
  {
    return OK;
  }

#if HCOM_DIAG_INCLUDE_MESSAGE_DECODING_IN_BUILD > 0
  hcom_diag_decode_sending_message_type((const uint8_t *)xmitBuffer,
              ((HcomProtoHdrMsg_t *)xmitBuffer)->stdHeader.rqstType,
               xmitLength);
#endif

  // Encode but reserve the first byte for a packet delimiter
  size_t encodedLength = hcom_host_cobs_encoder(xmitBuffer, 0, xmitLength, _encodedXmitBuff + 1);

  // Need room for 2 delimiters for the message
  if(encodedLength + 2 > HCOM_PROTOCOL_SAFE_ENCODED_MSG_BUF_SIZE)
  {
    syslog(LOG_ERR, "%s@%d-Buffer overrun. Need:%d\n", __FILE__, __LINE__,
              encodedLength + 2);
    usleep(20 * 1000);  // Ensure syslog is seen
    PANIC();
  }

  // To improve the ability of the CLI to detect packet boundaries
  // add an initial delimiter so we can insure there is always at
  // least one delimiter between messages, even partial messages.
  _encodedXmitBuff[0] = HCOM_PROTOCOL_COBS_DELIMITER;
  encodedLength++;    // Account for leading zero

  // Encoded messages needs a terminating delimiter for COBS
  _encodedXmitBuff[encodedLength] = HCOM_PROTOCOL_COBS_DELIMITER;
  encodedLength++;
  remainingBytes = encodedLength;

  // Loop until message 100% written to serial port
  while (remainingBytes > 0)
  {
    // Based on observation - If O_NONBLOCK is not specified in the file_open
    // call, the file_write call blocks after writing some number of bytes.
    // It's as if some internal buffer(s) fills causing the file_write call
    // to begin blocking. This cannot be allowed, since it would block the
    // calling thread, preventing it from doing any work.
    // set_errno(0);

    // Write some part of the buffer, usually < 256 characters (determined by testing)
    ssize_t writeRet = write(_comms_write_fd,
                             &_encodedXmitBuff[toWriteOffset],
                             remainingBytes);
    if(writeRet > 0)
    {
      remainingBytes -= writeRet;   // Note: if remainingBytes == 0 will exit while loop
      toWriteOffset += writeRet;
      isBlocked = false;   // Made real progress -- any future stall starts fresh

#if (HCOM_HOST_SEND_ADD_SYSLOG_IN_BUILD > 0)
      syslog_if_safe(LOG_MDIAG, "-->%s@%d-Write success, wrote:%d, remaining:%d\n",
          thisFile, __LINE__, writeRet, remainingBytes);
#endif
      continue;   // Loop and check if remainingBytes > 0, else done
    }

    // Examine error
    // EINTR is not an error... it simply means that this write was interrupted
    // by a signal before it wrote the data.
    if (errno == EINTR)
    {
      continue;
    }

    // Was the write attempt blocked? 
    // A zero return on write on a non-blocking fd is treated as a non-error
    // some identifty it as a "transient stall" whatever that means.
    if(errno == EAGAIN || writeRet == 0)
    {
      // In this case either host PC was disconnected from Meadow, CLI stopped running
      // or Meadow.CLI just can't keep up. We'll give it a chance to catchup.
      struct timespec blockedNow;
      clock_gettime(CLOCK_MONOTONIC, &blockedNow);

      if(!isBlocked)
      {
        // Start the clock on this contiguous stall
        blockedSince = blockedNow;
        isBlocked = true;
      }

      long blockedMs = (blockedNow.tv_sec - blockedSince.tv_sec) * 1000L +
                (blockedNow.tv_nsec - blockedSince.tv_nsec) / 1000000L;

      if(blockedMs < (long)maxBlockedMs)
      {
        blockedCount++;   // Kept for diagnostics only; no longer gates the retry

        // Wait for the fd to become writable instead of unconditionally
        // sleeping the full interval -- reacts immediately once the driver
        // has room again. A poll() timeout (nothing became writable in time)
        // is treated the same as a plain sleep: just another blocked
        // iteration. Any poll() outcome -- ready, timeout, or error -- just
        // falls through to retry the write above, which is the real source
        // of truth.
        struct pollfd pfd = { .fd = _comms_write_fd, .events = POLLOUT };
        poll(&pfd, 1, HCOM_XMIT_MAX_BLOCKED_TIME_DELAY / 1000);

        // Floor: confirmed on hardware that poll() can report ready (or
        // time out) in a fraction of a millisecond while write() keeps
        // failing, which would otherwise let blockedMs above spin through
        // its whole budget without any real time elapsing -- i.e. the CLI
        // never actually gets a chance to catch up. This guarantees every
        // blocked retry costs real wall-clock time toward that budget.
        usleep(HCOM_XMIT_MIN_RETRY_DELAY_USEC);

#if (HCOM_DIAG_INCLUDE_LOG_DEBUG_IN_BUILD > 0)
        hcom_logging_syslog_x(LOG_DEBUG, "%s@%d-Resend #%d\n", thisFile, __LINE__, blockedCount);
#endif
        continue;
      }

      // Exhausted the real-time budget
      _lastXmitBlocked = true; // Seems host isn't connected or CLI not running

      syslog_if_safe(LOG_ERR,
        "%s@%d-Error:Exceeded blockedCount:%d, stalled %ld ms (wrote %d of %d bytes)\n",
                thisFile, __LINE__, blockedCount, blockedMs,
                encodedLength - remainingBytes, encodedLength);

      // EAGAIN exit. No reason to close fd. The caller can sort out what to do
      // with partial data sent.
      return -EAGAIN;
    }
    
    //-------------------------------------------------------------
    // writeRet must be negative and indicating an error other than
    // EAGAIN or EINTR
#if (HCOM_HOST_SEND_ADD_SYSLOG_IN_BUILD > 0)
    syslog_if_safe(LOG_MDIAG, "ERROR: Unexpected errno:%d, exiting\n", errno);
#endif

    // Some unexpected error
    close(_comms_write_fd);
    _comms_write_fd = -1;
    _lastXmitBlocked = true;

    if(_lowPowerActive && errno == ENOTCONN)
    {
      _lowPowerActive = false;

      int ret = hcom_host_send_open_transmit_connection();
      if(ret >= 0)
      {
        continue;   // Attempt to resend this message
      }

      return -errno;
    }

    // Under rare conditions (e.g. Meadow powered with +5 and connected to USB
    // and while sending, then USB cable is removed) an ENOTCONN can be
    // returned. Under these circumstances we'll return to the caller as if
    // the message was sent. The next message a caller sends will be handled
    // like any other message.
    if(errno == ENOTCONN)
      return OK;    // Return with _lastXmitBlocked = true

    return -errno;
  } // while (remainingBytes > 0)

  // Success exit
  _lastXmitBlocked = false;

#if (HCOM_HOST_SEND_ADD_SYSLOG_IN_BUILD > 0)
  if(blockedCount > 0)
  {
    // Only fires when this send actually had to retry -- diagnostic for
    // seeing how much real time a recovered (not given-up) send costs.
    struct timespec sendEnd;
    clock_gettime(CLOCK_MONOTONIC, &sendEnd);
    long totalMs = (sendEnd.tv_sec - sendStart.tv_sec) * 1000L +
              (sendEnd.tv_nsec - sendStart.tv_nsec) / 1000000L;

    syslog_if_safe(LOG_NOTICE,
      "%s@%d-Send recovered after blockedCount:%d, total send time %ld ms\n",
      thisFile, __LINE__, blockedCount, totalMs);
  }
#endif

  return OK;
}

//===================================================================
// This function is for those places where hcom is processing text to
// send to the host PC via syslog. The problem is if we processing a syslog
// and generate another syslog, this syslog will cause another in an endless
// cycle.
void syslog_if_safe(int priority, FAR const IPTR char *fmt, ...)
{
#if defined CONFIG_RAMLOG_SYSLOG
  // If not sending to CLI we can send via syslog
  if(!hcom_trace_is_sending_to_cli())
  {
    // Send via syslog
    va_list args;
    va_start(args, fmt);
    vsyslog(priority, fmt, args);
    va_end(args);
    return;
  }

  // Can't send directly to the host because this would be recursive.
  return;
#else
  // If Nuttx is configured for outputting syslogs to the console via UART
  // then this works. But, obviously, these cannot be routed to the host PC.
  va_list args;
  va_start(args, fmt);
  vsyslog(priority, fmt, args);
  va_end(args);
#endif
}

//=====================================================================
// Wait for the thread writing to exit
void hcom_host_send_transmit_takesem(sem_t *semaphore)
{
  int ret;

  do
  {
    /* Take the semaphore (perhaps waiting) */
    ret = sem_wait(semaphore);
  }
  while (ret == -EINTR);
}
