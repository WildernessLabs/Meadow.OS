# Request Processing and Routing

## Overview

Once the receive pipeline has produced a decoded packet ([ReceivePipeline.md](ReceivePipeline.md)),
the `HcomProc` thread passes it to `hcom_host_handle_packet()`. That function decides whether it is
file data or a command, validates a command, does any preparation the command needs, and hands it to
`hcom_host_route_request_by_cmd_type()`, a large `switch` that sends each request type to its handler.
This document describes those two steps and the conventions every handler follows. What the handlers
do is covered in the feature documents.

```
   decoded packet
        |
        v
   sequence number != 0 ? --- yes --> DATA PACKET
        | no                           F7 download active?     yes -> write it to the file
        v                              ESP32 download active?  yes -> hand it to the ESP32 code
   COMMAND PACKET                      neither                 no  -> error (-EBADMSG)
        |
        1. at least 12 bytes?
        2. protocol version acceptable?
        3. board supported?
        4. an F7 download active and this is not its end request?   -> refuse
        5. prepare (path parsing, Mono check, SD card, directories) for file requests
        6. route: switch on the request type -> handler
        7. clean up the shared file state after file requests
        |
        v
   back to the processing loop: report any error, send the Concluded if still owed
```

All of this runs on the one `HcomProc` thread, one packet at a time. A handler that takes a long time
(a CRC listing of many files, a flash erase, the Mono image flash) holds up every later request; the
receive thread keeps queueing until the circular buffer is full, and the host is then stalled.

## Data Packets

A packet whose first two bytes (the sequence number) are not zero is file data. The packet must be at
least 2 bytes long, otherwise it is ignored with `-EBADMSG`.

| Condition | What happens |
|-----------|--------------|
| A download to the device's file system is active | `hcom_file_dnld_stm32f7_recvd_file_data()` writes it. An error is returned to the loop, which reports `TEXT_ERROR` "File data processing failed" |
| An ESP32 firmware download is active | `hcom_file_dnld_esp32_recvd_file_data()` takes it; the result is ignored |
| Neither | Logged at debug level (the host is confused or something is wrong) and `-EBADMSG` is returned |

See [FileDownload.md](FileDownload.md) for what is done with the data.

## Command Packets

For a command packet the code first records that this request is now awaiting a `TEXT_CONCLUDED`
(`hcom_host_send_conclude_pending_set(0)`, then again with the request type once it is known). The
checks and steps, in order:

1. **Size.** A packet shorter than the 12-byte header returns `-EBADMSG`.
2. **Protocol version.** Below 7 the request is refused with `TEXT_ERROR` and `TEXT_CONCLUDED`; 7
   is accepted with a notice and the device downgrades its replies. The rules are in
   [WireProtocol.md](WireProtocol.md#protocol-versions).
3. **Supported board.** If the board is the F7 Feather V1 (detected once, at startup, as hardware
   version `MEADOW_F7_HW_VERSION_NUMB_F7V1`), nothing else has been set up, so every request is
   answered with `TEXT_ERROR` ("F7FeatherV1 (S25FL flash) is not supported by this Meadow.OS
   release"), plus `INIT_DOWNLOAD_FAIL` for the download and delete requests, then `TEXT_CONCLUDED`.
4. **Download in progress.** While a download to the device's file system is active, the only
   accepted commands are `END_FILE_TRANSFER` and `MONO_UPDATE_FILE_END`; anything else returns
   `-EBADRQC`. (An ESP32 download is not gated this way.)
5. **Preparation**, which depends on the request:

   | Requests | Preparation |
   |----------|-------------|
   | `LIST_FILES_SUBDIR`, `LIST_FILES_SUBDIR_CRC` | Parse the path (a directory ending in `/`) into the shared file structure; if it is on `/sdcard`, check the card is mounted |
   | `UPLOAD_FILE_INIT`, `GET_INITIAL_FILE_BYTES` | Parse the path (a file name) into the shared structure; SD card check |
   | `START_FILE_TRANSFER`, `MONO_UPDATE_RUNTIME`, `DELETE_FILE_BY_NAME` | `hcom_host_process_init_write_or_del()`: refuse if Mono is enabled; parse the file-start body; SD card check; for the two download requests, create any missing sub-directories. Any failure sends `INIT_DOWNLOAD_FAIL` and clears the shared structure |
   | `END_FILE_TRANSFER`, `MONO_UPDATE_FILE_END` | None |
   | everything else | None |

   When preparation fails, the request's own `TEXT_ERROR` has already been sent. For the three
   requests handled by `hcom_host_process_init_write_or_del()` (the two downloads and the delete) a
   `TEXT_CONCLUDED` is also sent at once, because the end request that would normally conclude a
   transfer will never come.
6. **Route.** `hcom_host_route_request_by_cmd_type()` (below).
7. **Clean-up.** After `END_FILE_TRANSFER`, `MONO_UPDATE_FILE_END`, `UPLOAD_START_DATA_SEND`,
   `DELETE_FILE_BY_NAME`, `LIST_FILES_SUBDIR` and `LIST_FILES_SUBDIR_CRC`, the shared file structure is
   freed (`hcom_dir_mgmt_free_file_info()`).

`hcom_host_handle_packet()` then returns `OK`, **even if the handler failed**: the route function's
return value is only logged ("no point exiting now, just do the cleanup first"). Handlers are
expected to report their own errors to the host. Errors returned to the processing loop come only
from the validation steps above and from data packets.

### The shared file structure

File requests share a single `hcom_dnld_shared_t` (`F7MeadowOS/include/meadow/hcom_dnld_shared.h`),
allocated once at startup. It holds the parsed path, the category (`/meadow0`, `/sdcard`, bare name),
the open file descriptor, the sizes and CRCs and the download state. Every request that parses a path
begins by freeing whatever was there (`hcom_dir_mgmt_free_file_info()` closes any open file). As a
result:

- only one file operation can exist at a time;
- the two-request upload (`UPLOAD_FILE_INIT` then `UPLOAD_START_DATA_SEND`) keeps its state in this
  structure between the two requests, and any request that re-initialises it in between discards the
  prepared upload.

The structure and the path rules are described in [FileDownload.md](FileDownload.md).

## After the Handler: The Processing Loop

When `hcom_host_handle_packet()` returns, the loop (`hcom_host_process_run_loop()`) does two things:

- If it returned an error (other than `-EOWNERDEAD`, which nothing in HCOM ever produces), it reports
  one: for a data packet `TEXT_ERROR` "File data processing failed, error N"; for a command that
  has not yet concluded, `TEXT_ERROR` "Request 0x.... failed, error N". It then frees the shared file
  structure.
- It calls `hcom_host_conclude_if_pending()`, which sends the `TEXT_CONCLUDED` if the handler did not,
  except for the requests that conclude later
  ([WireProtocol.md](WireProtocol.md#how-the-rule-is-enforced)).

## The Route Table

`hcom_host_route_request_by_cmd_type()` in `hcom_host_route.c` is a `switch` on the request type. In
the tables, **A** is `TEXT_ACCEPTED` and **C** is `TEXT_CONCLUDED`. "Forward" means the request is
passed to the kernel (see below).

### Files

| Request | Handler | Replies |
|---------|---------|---------|
| `START_FILE_TRANSFER` | `hcom_file_dnld_stm32f7_file_begin()` | A, then `INIT_DOWNLOAD_OKAY` or `_FAIL`; C comes with the end request |
| `END_FILE_TRANSFER` | `hcom_file_dnld_stm32f7_file_end()` | result, C (no A) |
| `DELETE_FILE_BY_NAME` | `hcom_file_delete_stm32f7_file_by_name()` | A, result, C |
| `LIST_FILES_SUBDIR`, `..._CRC` | `hcom_file_lists_all_files_in_subdirectories()` | A, list lines, C |
| `GET_FILES_AND_FOLDERS` | `hcom_file_lists_all_dev_dir_and_files_start()` | A, list lines, C |
| `GET_INITIAL_FILE_BYTES` | `hcom_file_upld_proc_initial_bytes_in_file()` | A, data, C |
| `UPLOAD_FILE_INIT` | `hcom_file_upld_proc_start_file_upload()` | A, `INIT_UPLOAD_OKAY` or `_FAIL`, then C from the safety net |
| `UPLOAD_START_DATA_SEND` | `hcom_file_upld_proc_begin_file_uploading()` | data packets, completion, C (no A) |
| `UPLOAD_ABORT_DATA_SEND` | `hcom_file_upld_proc_abort_file_upload()` (not implemented) | A, `TEXT_ERROR`, C |

### Mono

| Request | Handler | Replies |
|---------|---------|---------|
| `MONO_DISABLE`, `MONO_ENABLE` | `hcom_mono_ctrl_disable_mono()` / `_enable_mono()` | A, information, `TEXT_RECONNECT`, restart |
| `MONO_RUN_STATE` | `hcom_mono_ctrl_report_mono_enabled_state()` | A, information, C |
| `MONO_UPDATE_RUNTIME` | `hcom_file_dnld_stm32f7_file_begin()` | like `START_FILE_TRANSFER`; C with `MONO_UPDATE_FILE_END` |
| `MONO_UPDATE_FILE_END` | `hcom_file_dnld_stm32f7_file_end()`, then forward `MONO_FLASH` | result, C (no A) |
| `MONO_FLASH` | forward | A, C |
| `MONO_START_DBG_SESSION` (debugger build option) | `hcom_mono_remote_dbg_enable()`, then restart | A; C after the restart |
| `DEBUGGING_DEBUGGER_DATA` (debugger build option) | `hcom_mono_remote_dbg_recv_host_sending_to_mono()` | none; a stream, not a request |

### Device, diagnostics and kernel

| Request | Handler | Replies |
|---------|---------|---------|
| `GET_DEVICE_INFORMATION` | `hcom_misc_rqst_get_device_info()` | A, `TEXT_DEVICE_INFO`, C |
| `GET_DEVICE_NAME` | `hcom_misc_rqst_get_device_name()` | A, `TEXT_DEVICE_INFO`, C |
| `RESTART_PRIMARY_MCU` | `hcom_via_nx_host_restart_meadow()` | A, `TEXT_RECONNECT`, restart |
| `BULK_FLASH_ERASE`, `PART_RENEW_FILE_SYS` | forward, then restart | A, `TEXT_RECONNECT`, restart |
| `ENTER_DFU_MODE` | `hcom_misc_rqst_enter_dfu_mode()`, then `hcom_via_nx_only_restart_meadow()` | A, then restart |
| `VERIFY_ERASED_FLASH` | forward | A, C |
| `CHANGE_TRACE_LEVEL` | `hcom_diag_trace_priority_change_trace_level()` | A, `TEXT_INFORMATION` ("Trace level changed from ... to ..."), C |
| `SEND_TRACE_TO_HOST`, `NO_TRACE_TO_HOST`, `SEND_TRACE_TO_UART`, `NO_TRACE_TO_UART`, `SEND_PROFILER_TO_UART`, `NO_PROFILER_TO_UART` | forward | A, C |
| `RTC_SET_TIME_CMD` | `hcom_via_nx_execute_rtc_set_clock()` | A, C (a failure is only logged) |
| `RTC_READ_TIME_CMD` | forward | A, `TEXT_INFORMATION` with the time (sent by the kernel handler), C |
| `RTC_WAKEUP_TIME_CMD` | `hcom_via_nx_execute_rtc_set_wakeup_time()` | A, C (a failure is only logged) |
| `DEVELOPER` | `hcom_developer_tests_developer(extraData, userData)` | A, C |
| `ENABLE_DISABLE_NSH` | `hcom_diag_misc_launch_nsh()` | A, information, C |

### Others

| Request | Behaviour |
|---------|-----------|
| ESP32 requests (`START_ESP_FILE_TRANSFER`, `END_ESP_FILE_TRANSFER`, `READ_ESP_MAC_ADDRESS`, `RESTART_ESP32`) | Compiled when `CONFIG_HCOM_ESP32_COMMS` is set (the default); not covered here |
| `OTA_REGISTER_DEVICE` | With `CONFIG_MEADOW_CLOUD` (set in the `meadow_os` defconfig) it runs the OTA registration; without it, `TEXT_REJECTED` "OTA not available in this build" and the safety net sends C |
| any unlisted type, including `FORMAT_FLASH_FILE_SYS` | `TEXT_REJECTED` ("unknown/unavailable CLI request"), then C |

## Handler Conventions

Almost every handler follows one of four patterns.

**Query:** send A, do the work (sending information or data as it goes), send C. Used by the device
information, trace level and listing requests, and by the RTC set and wakeup requests.

**Forwarded to the kernel:** send A, call `hcom_via_nx_forward_cli_cmd_to_nx(requestType, userData)`,
send C. The forwarder fills a `struct hcom_nx_cmd_data` and makes an ioctl on `/dev/upd`
(`HCOM_NX_UPD_CLI_COMMAND`); the kernel switches on the request type
([UpdInterface.md](UpdInterface.md)). Two details matter:

- The structure carries a pointer to `hcom_host_send_simple_string_msg()`, so **the kernel handler
  sends its own messages to the host** by calling that function.
- If the kernel handler returns an error, the user side writes the handler's log text (or a generic
  line) to syslog and carries on. **It does not send an error to the host**, so the host sees one only
  if the handler sent it. The route code then sends C regardless.

**Restart:** send A and `TEXT_RECONNECT`, then ask the kernel to restart. The call does not return, so
the code after it never runs and C is sent by the new instance after the restart
([HostLinkUsb.md](HostLinkUsb.md#restarts)).

**Multi-request transaction:** the start request sends A and leaves the C owed; the data and end
requests complete it (file download, upload and the Mono Runtime update). The safety net's list of
exceptions in `hcom_host_conclude_if_pending()` is what lets the start request stay open.

## Adding a Request

1. Define the number in `hcom_protocol.h`, in the right class (see
   [WireProtocol.md](WireProtocol.md#message-classes)) and without reusing a removed number.
2. Add a `case` to the route function: send A, call the handler, send C, following one of the
   patterns above. If the request must conclude later, add it to the exceptions in
   `hcom_host_conclude_if_pending()`.
3. If it takes a path, give it a preparation branch in `hcom_host_handle_packet()` and a clean-up
   entry.
4. Optionally add its name to the diagnostic decoder table (`hcom_diag_decode_protocol.c`), which is
   only used when message decoding is compiled in for debugging.
5. If it must run while a download is active, handle that in the download-in-progress check, which
   otherwise refuses it.

## Things to Know

- **Handler errors do not reach the processing loop.** The route function's return value is logged
  and then discarded, so the loop's error reporting never fires for a failed handler. Every handler
  must report its own failure to the host, and the safety net then supplies the `TEXT_CONCLUDED`.
- **Kernel-forwarded failures can be silent to the host.** As above, only syslog sees an error from
  the forwarder itself.
- **"Mono must be disabled" tests the setting, not whether Mono is running.** The check
  (`hcom_mono_ctrl_is_mono_enabled()`) reads the enabled state kept in the battery-backed registers.
  It applies to download and delete only; uploads and listings are allowed while Mono runs.
- **An ESP32 download is not protected the way an F7 download is.** The "only the end request is
  accepted" rule applies only to downloads to the device's own file system.
- **One file operation at a time.** The shared structure is global and every path-taking request
  resets it.
- **Long handlers stall the pipeline.** All requests run on one thread.
- **`ENABLE_DISABLE_NSH` has a stale comment** ("currently does nothing"). The handler does start
  NSH, in a build that includes it ([DeveloperCommandsNsh.md](DeveloperCommandsNsh.md)).
- **The `-EOWNERDEAD` test in the loop is a leftover.** Nothing in HCOM returns that value.
- **A comment in the `UPLOAD_FILE_INIT` case is out of date.** It says the `TEXT_CONCLUDED` is sent by
  the `UPLOAD_START_DATA_SEND` case; the safety net also sends one straight after the init reply.

## Source Map

| Concern | File |
|---------|------|
| Data/command split, validation, preparation, clean-up | `apps/system/hcom/comms/hcom_host_preprocess.c` |
| The route table | `apps/system/hcom/comms/hcom_host_route.c` |
| Forwarding to the kernel | `apps/system/hcom/nx_rqsts/hcom_via_nx_access.c` (`hcom_via_nx_forward_cli_cmd_to_nx()`) |
| Path parsing and the shared file structure | `apps/system/hcom/file/hcom_file_dir_mgmt_utils.c`, `F7MeadowOS/include/meadow/hcom_dnld_shared.h` |
| Request and message numbers | `F7MeadowOS/include/meadow/hcom_protocol.h` |
| Diagnostic name table | `apps/system/hcom/diag/hcom_diag_decode_protocol.c` |

## Related Documents

[WireProtocol.md](WireProtocol.md), [ReceivePipeline.md](ReceivePipeline.md),
[SendPipeline.md](SendPipeline.md), [FileDownload.md](FileDownload.md),
[FileUploadDeleteList.md](FileUploadDeleteList.md), [MonoLifecycle.md](MonoLifecycle.md),
[DeviceControl.md](DeviceControl.md), [UpdInterface.md](UpdInterface.md).
