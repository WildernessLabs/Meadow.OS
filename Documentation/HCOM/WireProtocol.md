# The HCOM Serial Protocol

## Overview

HCOM is the protocol spoken between a host (Meadow.CLI, HcomDiagUi) and the `hcom` application on
the device. It runs over a single USB CDC/ACM serial port (`/dev/ttyACM0` on the device; see
[HostLinkUsb.md](HostLinkUsb.md)). It is a request/reply protocol with asynchronous extras: the host
sends **requests**, the device answers each one with a short conversation of **messages**, and the
device may also send messages that nobody asked for (trace output, Mono's stdout and stderr,
debugger traffic, a low-power notice).

This document defines the protocol: how bytes are framed, what a packet looks like, every request
and message type, and the rules for how a request is answered. How the device implements it is
covered in [ReceivePipeline.md](ReceivePipeline.md), [SendPipeline.md](SendPipeline.md) and
[RequestRouting.md](RequestRouting.md).

Conventions used below:

- Multi-byte fields are **little-endian**, and all protocol structures are packed (no padding).
- Names follow the source: `HCOM_MDOW_REQUEST_*` is a request sent **host to Meadow** and
  `HCOM_HOST_REQUEST_*` is a message sent **Meadow to host**. Despite the name, both directions use
  the same packet format. In the tables the prefix is dropped.
- "The device" and "Meadow" mean the same thing.
- The definitions are in `F7MeadowOS/include/meadow/hcom_protocol.h`.

## Layers

```
   request / reply conversation       Accepted ... Concluded         (this document, last sections)
   ------------------------------------------------------------
   message types                      header-only, file, text, binary
   ------------------------------------------------------------
   packet                             12-byte standard header + body (up to 8192 bytes in all)
   ------------------------------------------------------------
   frame                              00 <COBS-encoded packet> 00
   ------------------------------------------------------------
   byte stream                        USB CDC/ACM serial port
```

There is no checksum, acknowledgement or retransmission at the packet or frame level. The
transport relies on USB for integrity. The only end-to-end check is the CRC32 that accompanies a
file transfer.

## Framing

### Delimiters and COBS

Packets are delimited with **Consistent Overhead Byte Stuffing (COBS)** using `0x00` as the
delimiter (`HCOM_PROTOCOL_COBS_DELIMITER`). COBS rewrites a packet so that it contains no `0x00`
bytes, which lets `0x00` mark packet boundaries unambiguously, so a receiver that joins the stream
mid-packet resynchronises at the next delimiter.

The implementation is `hcom_host_cobs_encoder()` and `hcom_host_cobs_decoder()` in
`apps/system/hcom/comms/hcom_host_encode.c`, a port of the standard algorithm (Cheshire and Baker, SIGCOMM 1997).
In outline: the encoded data is a series of blocks, each starting with a code byte `N`. The `N - 1`
bytes that follow are data bytes; if `N` is less than `0xff`, the block ends with an implied
`0x00`. Code `0xff` means 254 data bytes follow and there is no implied zero, which is how packets
longer than 254 bytes are handled.

A small packet, `11 22 00 33`, encodes as:

```
   packet   : 11 22 00 33
   encoded  : 03 11 22 02 33         (03 = "two data bytes then a zero", 02 = "one data byte")
```

### What goes on the wire

Every message Meadow sends is framed as

```
   00  <COBS-encoded packet>  00
```

The leading delimiter is deliberate (a Meadow requirement, per the source comments): it guarantees
there is always at least one delimiter between two messages, even if the previous message was cut
short. Consecutive delimiters produce empty frames, and **empty frames are ignored by the device**
(and, per the source comments, by the CLI). The device uses this deliberately: when it has found no
host reading its output, it writes a single `0x00` before each send as a probe, because that byte
is harmless to a host that picks it up late.

The host sends the same framing. The device does not require the leading delimiter; it cuts
packets at the trailing `0x00` and ignores empty ones.

A worked example: a `GET_DEVICE_INFORMATION` request (sequence 0, version 8, request type
`0x0112`, no extra data, user data 0) has the 12-byte packet

```
   00 00  08 00  12 01  00 00  00 00 00 00
   seq    ver    rqst   extra  userData
```

which COBS encodes to 13 bytes and goes on the wire as

```
   00  01 01 02 08 03 12 01 01 01 01 01 01 01  00
```

### Sizes and limits

| Constant | Value | Meaning |
|----------|-------|---------|
| `HCOM_PROTOCOL_CURRENT_PACKET_MAX_SIZE` | 8192 | Largest decoded packet (header included), both directions |
| `HCOM_PROTOCOL_HEADER_MSG_LENGTH` | 12 | Size of the standard header |
| `HCOM_PROTOCOL_COMMAND_MAX_PAYLOAD_LEN` | 8180 | Largest body that fits behind a standard header |
| `HCOM_PROTOCOL_SAFE_ENCODED_MSG_BUF_SIZE` | 8232 | Buffer size for one encoded packet: 8192 plus 1 byte per 254 plus 8 |
| `HCOM_CIR_BUFFER_MAX_PACKETS` | 4 | The receive buffer holds 4 maximum-size encoded packets (32,928 bytes) |

COBS adds at least one byte, and one more for every 254 bytes of non-zero data. A packet of 254
non-zero bytes encodes to 256 bytes, and 8192 non-zero bytes encode to 8225. A packet of all
zeros encodes to one byte more than its length.

What the device does with out-of-range input:

| Condition | Result |
|-----------|--------|
| Encoded packet larger than 8232 bytes | Discarded; "Message too big, deleted" in syslog; no reply |
| Decoded packet larger than 8192 bytes | `TEXT_ERROR` "Received oversized packet of N bytes, ignoring it." |
| Packet shorter than 2 bytes (cannot hold a sequence number) | Ignored with an error; `TEXT_ERROR` |
| Command packet shorter than 12 bytes | Dropped; `TEXT_ERROR` "Request 0x... failed", then `TEXT_CONCLUDED` |
| Encoding that does not decode (bad block length) | Dropped silently (the decoder returns 0) |

## Packet Layout

### The standard header

Every packet starts with the 12-byte `HcomProtoStdHdr_t`:

| Offset | Size | Field | Meaning |
|--------|------|-------|---------|
| 0 | 2 | `seqNumber` | `0` for a command or any non-data message; `1` and up for file-data packets |
| 2 | 2 | `version` | Protocol version (see [Protocol Versions](#protocol-versions)) |
| 4 | 2 | `rqstType` | The request or message type. The high byte is the message class |
| 6 | 2 | `extraData` | Unused ("future"), except that the developer request (`0xf8`) carries its test selector here |
| 8 | 4 | `userData` | Meaning depends on the request type; often unused |

The body, if any, follows immediately.

### Message classes

The high byte of `rqstType` (`HCOM_PROTOCOL_REQUEST_TYPE_MASK` = `0xff00`) is the message class. It
says how the body is laid out. Requests and messages share the class numbering.

| Class | Value | Body | Struct |
|-------|-------|------|--------|
| Header only | `0x0100` | none | `HcomProtoHdrMsg_t` |
| File start | `0x0200` | file information, then a file name | `HcomProtoFileMsg_t` |
| Simple text | `0x0300` | text | `HcomProtoTextMsg_t` |
| Simple binary | `0x0400` | binary data | `HcomProtoBinMsg_t` |

The low byte is the specific request or message; the full 16-bit value is `class | code`, for
example `HCOM_MDOW_REQUEST_GET_DEVICE_INFORMATION` is `0x0100 | 0x12` = `0x0112`.

### File information (file-start class)

A file-start packet is the standard header followed by an `HcomProtoFileInfoHdr_t` (44 bytes, so
the file name starts at offset 56, `HCOM_PROTOCOL_FILE_INFO_NAME_HDR_OFF` from the info):

| Offset in body | Size | Field | Used by |
|----------------|------|-------|---------|
| 0 | 4 | `fileSize` | File size in bytes |
| 4 | 4 | `fileCheckSum` | CRC32 of the whole file, calculated by the sender |
| 8 | 4 | `fileFlashAddr` | ESP32 firmware downloads only |
| 12 | 32 | `fileMD5Hash` | ESP32 firmware downloads only (32 hex characters) |
| 44 | variable | `fileName` | File name or path |

The name is not NUL-terminated on the wire. The device takes everything after the 56-byte prefix
as the name, and adds its own terminator.

### Text bodies

For text messages in either direction:

- The text is **not NUL-terminated**; its length is the packet length minus the header.
- Meadow's simple string sender, which almost all text uses, **stops at the first carriage return
  or line feed**, so such a message is at most one line. (`hcom_host_send_simple_string_msg()`
  measures the string with `strcspn(text, "\r\n")`.) The raw-string variant, used for Mono's output
  and trace, sends the bytes as given.
- Text from Mono and trace is forwarded in chunks that may start or end in the middle of a line.

### File data packets

File data is the one place the two directions differ:

| Direction | Layout |
|-----------|--------|
| **Host to Meadow** (download) | `HcomProtoDataMsg_t`: a **2-byte** header holding only the sequence number (1 or more), then the data. The version and request-type fields are not present. |
| **Meadow to host** (upload) | A full 12-byte standard header with `rqstType` = `UPLOADING_FILE_DATA`, `seqNumber` 1 to n, then the data. |

The device tells command packets and data packets apart by the first two bytes alone: `0` means
command, anything else means data. So a download data packet is recognised by its sequence number
being non-zero. The maximum data per packet is 8190 bytes (download) and 8180 bytes (upload).

## Request Types, Host to Meadow

Values are `class | code`. "Standard" means the usual conversation (Accepted, work, Concluded; see
[The Request Conversation](#the-request-conversation)). Requests marked (ESP32) or (OTA) belong to
topics that are not covered in these documents; they are listed so the number space is complete.

### Header-only class (`0x0100`)

| Value | Name | Notes |
|-------|------|-------|
| `0x0102` | `CHANGE_TRACE_LEVEL` | `userData` is the level: 0 default (errors and warnings), 1 adds notice, 2 adds notice and info, 3 adds notice, info and debug. Saved in the battery-backed registers. |
| `0x0103` | `FORMAT_FLASH_FILE_SYS` | Defined, but **not handled**: the router replies `TEXT_REJECTED` then `TEXT_CONCLUDED`. |
| `0x0104` | `END_FILE_TRANSFER` | Ends a file download. No `TEXT_ACCEPTED`; concludes the transfer started by `START_FILE_TRANSFER`. |
| `0x0105` | `RESTART_PRIMARY_MCU` | Restart. Accepted, `TEXT_RECONNECT`, reset, `TEXT_CONCLUDED` after the restart. |
| `0x0106` | `VERIFY_ERASED_FLASH` | Kernel-side check of the file-system flash. Standard. |
| `0x010a` | `BULK_FLASH_ERASE` | Erases the file-system flash, then restarts (as `RESTART_PRIMARY_MCU`). |
| `0x010b` | `ENTER_DFU_MODE` | Accepted, then the device resets into the STM32 DFU bootloader. No `TEXT_RECONNECT` and no `TEXT_CONCLUDED`. |
| `0x010c` | `ENABLE_DISABLE_NSH` | Starts the NSH shell on a spare serial port; `userData` must be 1. In a build without NSH (`CONFIG_SYSTEM_NSH`, which the default `meadow_os` configuration does not set) it replies `TEXT_INFORMATION` "NuttShell not available". See [DeveloperCommandsNsh.md](DeveloperCommandsNsh.md). |
| `0x010f` | `MONO_DISABLE` | Disable Mono at the next start; restarts the device. |
| `0x0110` | `MONO_ENABLE` | Enable Mono; restarts the device. |
| `0x0111` | `MONO_RUN_STATE` | Replies `TEXT_INFORMATION` "Mono is enabled" or "Mono is disabled". Standard. |
| `0x0112` | `GET_DEVICE_INFORMATION` | Replies `TEXT_DEVICE_INFO` (format below). Standard. |
| `0x0113` | `PART_RENEW_FILE_SYS` | Renew (reformat on next boot) the file system; restarts. Not currently sent by the CLI. |
| `0x0114` | `NO_TRACE_TO_HOST` | Stop sending trace to the host. Standard. |
| `0x0115` | `SEND_TRACE_TO_HOST` | Send kernel trace to the host as `TEXT_TRACE_MSG`. Standard. |
| `0x0116` | `END_ESP_FILE_TRANSFER` | (ESP32) |
| `0x0117` | `READ_ESP_MAC_ADDRESS` | (ESP32) |
| `0x0118` | `RESTART_ESP32` | (ESP32) |
| `0x0119` | `MONO_FLASH` | Copies the Mono Runtime image to flash (kernel side). Standard. |
| `0x011a` | `SEND_TRACE_TO_UART` | Route trace output to a UART. Standard. |
| `0x011b` | `NO_TRACE_TO_UART` | Stop routing trace to a UART. Standard. |
| `0x011c` | `MONO_UPDATE_RUNTIME` | Starts a download of the Mono Runtime image; takes a file-start body despite its class. Behaves like `START_FILE_TRANSFER`. |
| `0x011d` | `MONO_UPDATE_FILE_END` | Ends that download, then flashes the Runtime (`MONO_FLASH`). No `TEXT_ACCEPTED`. |
| `0x011e` | `MONO_START_DBG_SESSION` | Arms the remote debugger and restarts the device. Concluded after the restart. |
| `0x011f` | `GET_DEVICE_NAME` | Replies `TEXT_DEVICE_INFO` containing only the name. Standard. |
| `0x0120` | `GET_INITIAL_FILE_BYTES` | Returns the first bytes of a file. Takes a path as text despite its class. |
| `0x0121` | `UPLOAD_START_DATA_SEND` | Starts sending the file prepared by `UPLOAD_FILE_INIT`. No `TEXT_ACCEPTED`. |
| `0x0122` | `UPLOAD_ABORT_DATA_SEND` | Not implemented: replies `TEXT_ERROR` "File Read Abort is not implemented". |
| `0x0123` | `SEND_PROFILER_TO_UART` | Route profiler output to a UART. Standard. |
| `0x0124` | `NO_PROFILER_TO_UART` | Standard. |
| `0x01f3` | `GET_FILES_AND_FOLDERS` | Nested listing of every device, directory and file from `/`; non-zero `userData` includes `/proc`. Used by HcomDiagUi. |
| `0x01f7` | `OTA_REGISTER_DEVICE` | (OTA) When OTA is not built: `TEXT_REJECTED` "OTA not available in this build". |
| `0x01f8` | `DEVELOPER` | Developer test commands: `extraData` selects the test, `userData` is its value. Standard. |

### File-start class (`0x0200`)

| Value | Name | Body |
|-------|------|------|
| `0x0201` | `START_FILE_TRANSFER` | File information and a path: begins a download to the device. Answered `TEXT_ACCEPTED`, then `INIT_DOWNLOAD_OKAY` or `INIT_DOWNLOAD_FAIL`. |
| `0x0202` | `DELETE_FILE_BY_NAME` | A path. Deletes the file. Standard. |
| `0x0203` | `START_ESP_FILE_TRANSFER` | (ESP32) |

### Simple text class (`0x0300`)

| Value | Name | Body |
|-------|------|------|
| `0x0301` | `UPLOAD_FILE_INIT` | The path of the file to read from the device |
| `0x0302` | (removed) | Was `EXEC_DIAG_APP_CMD`; never ported to Meadow.OS. **Do not reuse the number.** |
| `0x0303` | `RTC_SET_TIME_CMD` | Date and time to set (format in [DeviceControl.md](DeviceControl.md)) |
| `0x0304` | `RTC_READ_TIME_CMD` | none; the time comes back as text |
| `0x0305` | `RTC_WAKEUP_TIME_CMD` | Wake-up time (format in [DeviceControl.md](DeviceControl.md)) |
| `0x0306` | `LIST_FILES_SUBDIR` | A directory path ending in `/` |
| `0x0307` | `LIST_FILES_SUBDIR_CRC` | As above; each file is listed with its CRC |

### Simple binary class (`0x0400`)

| Value | Name | Body |
|-------|------|------|
| `0x0401` | `DEBUGGING_DEBUGGER_DATA` | Bytes from the Visual Studio debugger for Mono. No Accepted or Concluded. |

## Message Types, Meadow to Host

| Value | Name | Meaning and body |
|-------|------|------------------|
| `0x0101` | `UPLOAD_FILE_COMPLETED` | Header only. The last message of an upload. |
| `0x0301` | `TEXT_REJECTED` | The request is unknown or unavailable. Text explains. |
| `0x0302` | `TEXT_ACCEPTED` | The request was recognised and is being processed. |
| `0x0303` | `TEXT_CONCLUDED` | Processing of the request is finished. |
| `0x0304` | `TEXT_ERROR` | Human-readable error. Does not end the request; `TEXT_CONCLUDED` still follows. |
| `0x0305` | `TEXT_INFORMATION` | Human-readable progress or result. |
| `0x0306` | `TEXT_LIST_HEADER` | Start of a file list (no body). |
| `0x0307` | `TEXT_LIST_MEMBER` | One line of a file list. |
| `0x0308` | `TEXT_CRC_MEMBER` | One line of a file list with CRC: `name [0xCRC] N KB (bytes)`. |
| `0x0309` | `TEXT_MONO_STDOUT` | A chunk of Mono's standard output. |
| `0x030a` | `TEXT_DEVICE_INFO` | Reply to the device information or name requests. |
| `0x030b` | `TEXT_TRACE_MSG` | A kernel syslog line, when trace to host is enabled. |
| `0x030c` | `TEXT_RECONNECT` | The device is about to restart; the host should reconnect afterwards. `userData` echoes the request's `userData`. |
| `0x030d` | `TEXT_MONO_STDERR` | A chunk of Mono's standard error. |
| `0x030e` | `INIT_DOWNLOAD_OKAY` | The device is ready for file data. |
| `0x030f` | `INIT_DOWNLOAD_FAIL` | The download cannot start (often preceded by a `TEXT_ERROR` with the reason). |
| `0x0310` | `INIT_UPLOAD_OKAY` | Upload is ready. The body is a file-information structure, not text (see below). |
| `0x0311` | `INIT_UPLOAD_FAIL` | The upload cannot start. |
| `0x0312` | `DNLD_FAIL_RESEND` | A download stalled and was abandoned; the host should start it again. The text names the file. |
| `0x0313` | `DEVICE_PUBLIC_KEY` | (OTA) |
| `0x0314` | `TEXT_NEXT_LOW_PWR` | "Entering low-power mode". Sent by the kernel just before Stop mode. |
| `0x0401` | `DEBUGGING_MONO_DATA` | Bytes from Mono's debugger agent for Visual Studio. |
| `0x0402` | `SEND_INITIAL_FILE_BYTES` | Reply to `GET_INITIAL_FILE_BYTES`. |
| `0x0403` | `UPLOADING_FILE_DATA` | A chunk of file data during an upload; `seqNumber` 1 to n. |

### Body formats worth knowing

**`INIT_UPLOAD_OKAY`** is classed as text but carries an `HcomProtoFileMsg_t`: `fileSize` and
`fileCheckSum` (CRC32) are set, and `fileName` holds the full device path **including its
terminating NUL**. The ESP32-only fields (`fileFlashAddr`, `fileMD5Hash`) are not initialised.

**`TEXT_DEVICE_INFO`** from `GET_DEVICE_INFORMATION` is a list of `Key|Value~` pairs on one line:
`Product`, `Model`, `ProcessorType`, `CoprocessorType`, `OSVersion`, `CoprocessorVersion` (when
known), `MonoVersion` (when non-zero), `ProcessorId` (12 hex bytes separated by `-`), `Hardware`,
`DeviceName`, `SerialNo`, `WiFiMAC` and `SoftAPMac`. The version strings are the long form at
protocol version 8 and the short form at version 7. The reply to `GET_DEVICE_NAME` is the bare
name.

**`TEXT_MONO_STDOUT`, `TEXT_MONO_STDERR` and `TEXT_TRACE_MSG`** carry raw chunks of text. See
[MonoLifecycle.md](MonoLifecycle.md) and [TraceSyslog.md](TraceSyslog.md).

## The Request Conversation

### The rule

**Every request is answered with exactly one `TEXT_CONCLUDED`**, however it ends: success, error,
rejection or an unknown request type. The host can therefore always tell when a request is
finished. The usual shape is

```
   Host                                   Meadow
    |-- request ------------------------------>|
    |<------------------------ TEXT_ACCEPTED ---|   request recognised
    |<------------- (work: information, data) --|   zero or more TEXT_INFORMATION, TEXT_ERROR, ...
    |<----------------------- TEXT_CONCLUDED ---|   finished
```

`TEXT_ERROR` messages inside the conversation do not end it; only `TEXT_CONCLUDED` does.

### How the rule is enforced

When a command packet arrives, the device marks the request as "Concluded pending". Sending any
`TEXT_CONCLUDED` clears the mark. After every packet has been handled, the receive loop checks the
mark and, if it is still set, sends the `TEXT_CONCLUDED` itself. This safety net means early returns
and error paths cannot leave the host waiting. Handlers therefore send `TEXT_CONCLUDED` themselves
only where the timing matters.

The exceptions, where the conversation intentionally does not end at once:

| Request | When `TEXT_CONCLUDED` is sent |
|---------|-------------------------------|
| `START_FILE_TRANSFER`, `MONO_UPDATE_RUNTIME` | After the matching `END_FILE_TRANSFER` or `MONO_UPDATE_FILE_END`; if the transfer did not start, immediately |
| `START_ESP_FILE_TRANSFER` | After its end request (ESP32) |
| Requests that restart the device (`RESTART_PRIMARY_MCU`, `BULK_FLASH_ERASE`, `PART_RENEW_FILE_SYS`, `MONO_DISABLE`, `MONO_ENABLE`, `MONO_START_DBG_SESSION`) | By the device after it restarts and the host reconnects (see below) |
| `DEBUGGING_DEBUGGER_DATA` | Never; it is a data stream, not a request |
| `ENTER_DFU_MODE` | Never; the device leaves HCOM |

### Restart requests

```
   Host                                   Meadow
    |-- RESTART_PRIMARY_MCU ------------------>|
    |<------------------------ TEXT_ACCEPTED ---|
    |<----------------------- TEXT_RECONNECT ---|   userData echoed
    |                 (device resets, USB drops and re-enumerates)
    |<----------------------- TEXT_CONCLUDED ---|   first thing sent after reconnecting
```

The kernel sets a battery-backed flag (`HCOM_BBREG_RESTART_INITIATED_BY_HOST_CMD_BIT`) just before
it resets the device. When the receive thread first opens the serial port after the restart, it
sees the flag, clears it and sends the `TEXT_CONCLUDED`. A restart that was not asked for by the
host sends nothing. `MONO_START_DBG_SESSION` restarts the same way but sends no `TEXT_RECONNECT`.

### File download

```
   Host                                          Meadow
    |-- START_FILE_TRANSFER (size, CRC32, name) ---->|
    |<------------------------------ TEXT_ACCEPTED ---|
    |<------------------ INIT_DOWNLOAD_OKAY | _FAIL ---|   _FAIL: nothing more is accepted
    |-- data packet, seq 1 ------------------------->|   no per-packet reply
    |-- data packet, seq 2 ------------------------->|
    |       ...                                       |
    |<------------ TEXT_INFORMATION "File 10% ..." ---|   about every 10 %
    |-- END_FILE_TRANSFER ------------------------->|
    |<------- TEXT_INFORMATION (result) | TEXT_ERROR -|   CRC and size checked
    |<---------------------------- TEXT_CONCLUDED ---|
```

While a download to the device's own file system is active, the device accepts only the matching
end request; any other command is refused. There is no flow control or retransmission in the protocol. If data stops arriving, a
3-second watchdog abandons the download, deletes the partial file and sends `DNLD_FAIL_RESEND`.
See [FileDownload.md](FileDownload.md).

### File upload

```
   Host                                          Meadow
    |-- UPLOAD_FILE_INIT (path) ------------------->|
    |<------------------------------ TEXT_ACCEPTED ---|
    |<-------- INIT_UPLOAD_OKAY (size, CRC32, name) ---|   or INIT_UPLOAD_FAIL
    |<---------------------------- TEXT_CONCLUDED ---|   from the safety net
    |-- UPLOAD_START_DATA_SEND -------------------->|
    |<------------------ UPLOADING_FILE_DATA seq 1 ---|
    |<------------------ UPLOADING_FILE_DATA seq n ---|
    |<------------------------ UPLOAD_FILE_COMPLETED ---|
    |<------------ TEXT_INFORMATION "... sent to CLI" ---|
    |<---------------------------- TEXT_CONCLUDED ---|
```

`UPLOAD_FILE_INIT` is concluded by the safety net straight after its reply, and
`UPLOAD_START_DATA_SEND` has its own `TEXT_CONCLUDED`. See
[FileUploadDeleteList.md](FileUploadDeleteList.md).

### Failed and refused requests

| Situation | What the host sees |
|-----------|--------------------|
| Unknown or unavailable request type | `TEXT_REJECTED` ("Meadow received unknown/unavailable CLI request:0x...."), `TEXT_CONCLUDED` |
| Protocol version below the minimum | `TEXT_ERROR` (update Meadow.CLI), `TEXT_CONCLUDED`; the request is dropped |
| Command refused because a download is active | `TEXT_ERROR` "Request 0x... failed, error -N", `TEXT_CONCLUDED` |
| Data packet with no download active | `TEXT_ERROR` "File data processing failed, error -N" |
| Unsupported board (F7 Feather V1, no S25FL support) | Every request: `TEXT_ERROR`, plus `INIT_DOWNLOAD_FAIL` for download and delete requests, then `TEXT_CONCLUDED` |
| File download or delete while Mono is enabled | `TEXT_ERROR` "Mono must be disabled for file download", `INIT_DOWNLOAD_FAIL`, `TEXT_CONCLUDED` |

## Protocol Versions

The `version` field of every **command** packet is checked on arrival. Data packets have no version
field and are not checked.

| Constant | Value |
|----------|-------|
| `HCOM_PROTOCOL_MINIMUM_PROTOCOL_NUMBER` | 7 |
| `HCOM_PROTOCOL_PREFERRED_VERSION_NUMBER` | 8 |

| Version received | Result |
|------------------|--------|
| 8 or higher | Accepted without comment |
| 7 | `TEXT_INFORMATION` ("Meadow is expecting a different CLI Protocol version. Please update Meadow.CLI. (version received: 0007 expected: 0008)"), the device switches to version 7 for its replies, and the request is processed |
| Below 7 | `TEXT_ERROR` with the same text, `TEXT_CONCLUDED`, request dropped |

Every message the device sends is stamped with `g_current_hcom_protocol_version`. It starts at 8
and changes only when a version-7 command arrives. It is never raised again until the device
restarts. For version 7 the device also changes what it sends: the device-information reply then
carries the short version strings. From version 7 onwards a request handler is itself responsible
for checking the version if its behaviour depends on it.

## Things to Know

- **Download data packets have a different header from every other packet.** A download data
  packet is only a 2-byte sequence number plus data, while upload data packets use the full
  12-byte header. Code that parses packets must key on the sequence number being 0 or not.
- **The device does not check data sequence numbers.** The number only tells data from command.
  Lost, repeated or out-of-order data packets are not detected at this level; only the final CRC
  and size check would catch them. The number is 16 bits, so a sequence that wrapped to 0 would be
  taken for a command packet.
- **The version downgrade is sticky.** After a version-7 CLI has connected, replies carry version
  7 until the device restarts, even if a version-8 CLI connects later.
- **`g_current_hcom_maximum_packet_size` is declared and exported but nothing reads it.** The
  fixed 8192 constant is what is actually used.
- **Text is cut at the first CR or LF.** Anything multi-line sent through the simple text path is
  truncated to its first line. The device-information reply relies on this to drop its trailing
  newline.
- **Several request and message types sit in the wrong class.** `MONO_UPDATE_RUNTIME` and
  `GET_INITIAL_FILE_BYTES` are header-only class but carry a body, and `INIT_UPLOAD_OKAY` is text
  class but carries a file structure. The header file marks these as breaking changes to fix
  "when the time comes"; they are protocol as shipped, so they are documented as they are.
  `GET_FILES_AND_FOLDERS` (`0xf3`) and `OTA_REGISTER_DEVICE` (`0xf7`) are also flagged as being in
  the wrong number range.
- **Unused and removed numbers.** `FORMAT_FLASH_FILE_SYS` (`0x0103`) has no handler. Text code
  `0x0302` must not be reused.
- **The transmitted length of a text message is clamped to 8192 bytes, but the copy into the send
  buffer is not.** `hcom_host_send_buffered_msg()` limits the length it transmits but copies the
  whole message into an 8192-byte buffer first. No current caller sends more than 8180 bytes, so
  it cannot be hit today (Issue #821).
- **The `UPLOAD_FILE_INIT` comment in the router is out of date.** It says the `TEXT_CONCLUDED`
  comes from the `UPLOAD_START_DATA_SEND` case; in practice the safety net also sends one straight
  after the init reply, as shown above.

## Where the Protocol Is Implemented

| Concern | File |
|---------|------|
| Definitions | `F7MeadowOS/include/meadow/hcom_protocol.h` |
| COBS encode and decode | `apps/system/hcom/comms/hcom_host_encode.c` |
| Reading the serial port, splitting into packets | `apps/system/hcom/comms/hcom_host_receive.c`, `hcom_host_enq_deq.c`, `apps/system/hcom/misc/meadow_cirbuf.c` |
| Validation, version checks, the Concluded safety net | `apps/system/hcom/comms/hcom_host_preprocess.c` |
| Request dispatch and the Accepted / Concluded sequences | `apps/system/hcom/comms/hcom_host_route.c` |
| Building and transmitting messages | `apps/system/hcom/comms/hcom_host_send.c` |

## Related Documents

[HostLinkUsb.md](HostLinkUsb.md) (the transport underneath),
[ReceivePipeline.md](ReceivePipeline.md) and [SendPipeline.md](SendPipeline.md) (how frames are
read and written), [RequestRouting.md](RequestRouting.md) (the dispatcher), and the feature
documents named in the request tables.
