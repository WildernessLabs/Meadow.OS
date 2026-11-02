# File Upload, Delete and Listing

## Overview

[FileDownload.md](FileDownload.md) covers writing a file to the device. This document covers the other
file requests, which are simpler because none of them is a long-running stateful transfer except
upload:

| Operation | Requests | Style |
|-----------|----------|-------|
| **Upload** (read a file from the device) | `UPLOAD_FILE_INIT`, `UPLOAD_START_DATA_SEND`, `UPLOAD_ABORT_DATA_SEND` | two-request transaction, then a data stream |
| **Initial bytes** (read the start of a file) | `GET_INITIAL_FILE_BYTES` | one request, one reply |
| **Delete** | `DELETE_FILE_BY_NAME` | one request |
| **List a directory** | `LIST_FILES_SUBDIR`, `LIST_FILES_SUBDIR_CRC` | one request, many reply lines |
| **List everything** | `GET_FILES_AND_FOLDERS` | one request, many reply lines |

All of them use the same path rules as a download (`/meadow0`, `/sdcard`, a bare name, at most 8
elements; see [FileDownload.md](FileDownload.md#which-files-can-be-written)) and the same shared file
structure, so only one file operation exists at a time
([RequestRouting.md](RequestRouting.md#the-shared-file-structure)). They all run on the `HcomProc` thread
and can take a while for large files or directories.

## Upload (Device to Host)

### The sequence

```
   Host                                        Meadow                       _uploadAction
    |-- UPLOAD_FILE_INIT (path) ----------------->|  parse path, open the file     None
    |                                             |  must be a regular file
    |                                             |  read it all once for its CRC32
    |<------------------------ TEXT_ACCEPTED -----|
    |<--- INIT_UPLOAD_OKAY (size, CRC32, path) ---|  (or INIT_UPLOAD_FAIL)           -> Initialized
    |<------------------------ TEXT_CONCLUDED ---|  from the safety net
    |           ... host opens its destination file ...
    |-- UPLOAD_START_DATA_SEND ------------------>|  needs state Initialized         -> Uploading
    |<-------------- UPLOADING_FILE_DATA seq 1 ---|  rewind; read up to 8180 bytes
    |<-------------- UPLOADING_FILE_DATA seq 2 ---|  at a time
    |                    ...                      |
    |<----------------- UPLOAD_FILE_COMPLETED ----|                                  -> None
    |<---- TEXT_INFORMATION "File '...' successfully sent to CLI"
    |<------------------------ TEXT_CONCLUDED ---|
```

The two-step design lets the host decide, from the size and CRC in `INIT_UPLOAD_OKAY`, whether it can
create its destination before any data is sent.

### Step 1: `UPLOAD_FILE_INIT`

`hcom_file_upld_proc_start_file_upload()` in `hcom_file_upld_proc.c`:

1. **A name must be given.** An empty path is refused with "No Meadow file name given".
2. **Open the file** `O_RDONLY`. Failure gives "File '...' could not be found in Meadow file system." (no
   such file) or "File '...' could not be opened, error:N".
3. **It must be a regular file.** `open()` succeeds on a directory, and `read()` would then return
   directory entries, so the code checks with `fstat()` and refuses anything else with "'...' is a
   directory, not a file".
4. **Read the whole file once to compute its CRC32** (1024-byte reads, the same routine as a download's
   read-back check).
5. **Reply.** `INIT_UPLOAD_OKAY` carries the file size, the CRC32 and the **full device path**, so a bare
   name `app.dll` comes back as `/meadow0/app.dll`. Any failure sends `TEXT_ERROR` with the reason,
   then `INIT_UPLOAD_FAIL`, and frees the file structure (closing the file). The format of the OK reply
   is in [WireProtocol.md](WireProtocol.md#body-formats-worth-knowing).

The file stays open, held in the shared structure, until the data has been sent or something frees the
structure.

### Step 2: `UPLOAD_START_DATA_SEND`

`hcom_file_upld_proc_begin_file_uploading()` requires the upload state to be `Initialized`. If it is
not (no `UPLOAD_FILE_INIT` first, or an earlier one already used), it sends `TEXT_ERROR` "Must
initialize upload before each upload" and `INIT_UPLOAD_FAIL`, and returns `-ENODATA`.

Otherwise `hcom_file_upld_proc_build_upload_packet()`:

1. `lseek()` back to the start of the file.
2. Loops: `read()` up to **8180 bytes** (`HCOM_PROTOCOL_COMMAND_MAX_PAYLOAD_LEN`) into a packet buffer
   and send it as `UPLOADING_FILE_DATA`, with the sequence number counting up from 1 and the long
   (30-second) stall budget ([SendPipeline.md](SendPipeline.md#timing)). Stops when `read()` returns 0.
3. Sends `UPLOAD_FILE_COMPLETED`, then `TEXT_INFORMATION` "File '...' successfully sent to CLI".
4. Frees the shared structure; the router sends `TEXT_CONCLUDED`.

A read error ends the loop and sends `TEXT_ERROR` "File '...' data upload failed". No
`UPLOAD_FILE_COMPLETED` is sent in that case, but `TEXT_CONCLUDED` still follows.

### Aborting

`UPLOAD_ABORT_DATA_SEND` is not implemented: it replies `TEXT_ERROR` "File Read Abort is not
implemented", framed by the usual `TEXT_ACCEPTED` and `TEXT_CONCLUDED`. Nothing is aborted. There is
also no timeout while the device waits for `UPLOAD_START_DATA_SEND`.

## Initial Bytes

`GET_INITIAL_FILE_BYTES` returns the first part of a file in one message. The source comment calls it
rarely used and says it was originally a way to tell whether a file had already been downloaded.
`hcom_file_upld_proc_initial_bytes_in_file()` applies the same name and regular-file checks as upload,
reads from the start into a buffer sized to one packet (8180 bytes of data), and sends it as a single
`SEND_INITIAL_FILE_BYTES` message with sequence number 0 and the long stall budget. A file smaller than
the buffer is returned whole; an empty file gives a message with no data. Failures send `TEXT_ERROR` and
`INIT_UPLOAD_FAIL`. The file structure is always freed afterwards.

## Delete

`DELETE_FILE_BY_NAME` arrives as a file-start message with the path, and is prepared in the same way as
a download ([RequestRouting.md](RequestRouting.md#command-packets)): the path is parsed and checked, an
SD card must be mounted for an `/sdcard` path, and **Mono must be disabled**. The path must name a file;
a trailing `/` is refused.

`hcom_file_delete_stm32f7_file_by_name_internal()` then:

1. **Unlinks the file.** A failure (including "no such file") sends `TEXT_ERROR` "Meadow failed to
   delete file '...', <reason>" and stops.
2. **Removes empty parent directories**, deepest first, but only those the path itself names: for a path
   with more than two elements it tries `rmdir()` on each directory level in turn, up to the number of
   sub-directories in the path (`/meadow0` and `/sdcard` are never removed). It stops at the first
   directory that is not empty, which is not an error. Any other `rmdir()` failure is logged and
   returned.
3. Sends `TEXT_INFORMATION` "File '...' deleted".

The reply sequence is `TEXT_ACCEPTED`, the result, `TEXT_CONCLUDED`.

The download watchdog uses the same function to remove a partly written file.

## Listing a Directory

`LIST_FILES_SUBDIR` and `LIST_FILES_SUBDIR_CRC` take a path to a **directory, ending in `/`** (for
example `/meadow0/lib/`), or an empty path for `/meadow0`.
`hcom_file_lists_all_files_in_subdirectories()` in `hcom_file_lists_subdir.c` handles both, one reply
message per entry:

| Message | Content |
|---------|---------|
| `TEXT_LIST_HEADER` | start of a list (no body); **sent before the directory is opened** |
| `TEXT_LIST_MEMBER` | `Directory:<the requested path>` as the first line |
| `TEXT_LIST_MEMBER` | a file name (plain list); `/name` for each sub-directory and block device |
| `TEXT_CRC_MEMBER` | `name [0xCRC32] N KB (bytes bytes)` for each file (CRC list) |
| `TEXT_LIST_MEMBER` or `TEXT_CRC_MEMBER` | a totals line last |

An example of the CRC form for a directory holding one file and one sub-directory:

```
   TEXT_LIST_HEADER
   TEXT_LIST_MEMBER   Directory:/meadow0/
   TEXT_CRC_MEMBER    app.dll [0x1a2b3c4d] 12 KB (11874 bytes)
   TEXT_LIST_MEMBER   /lib
   TEXT_CRC_MEMBER    A total of 1 file using 12 KB (11874 bytes) and 1 directory
```

The plain list ends with `A total of N file(s) and M director(y/ies)`.

Rules:

- Regular files are always listed; sub-directories and block devices are listed (as `/name`) **only when
  the request gave a path** with the `/meadow0/` or `/sdcard/` prefix. A bare or empty path (the older
  CLI form) lists files only. `.` and `..` are skipped. Character devices and links are ignored.
- The CRC form reads every file in the directory completely to compute its CRC32, so it can take a long
  time on a large directory, and the processing thread does nothing else meanwhile. "KB" is the space
  the file occupies, taken from the file system's block count.
- Entries come in file-system order; nothing is sorted.
- If the directory cannot be opened, the host has already received `TEXT_LIST_HEADER`; it then gets
  `TEXT_ERROR` "'...' is not a directory or can't be opened, errno N" (without it the host would just see
  an empty list) and the request concludes normally.
- If a CRC calculation fails, the whole listing is abandoned at that file (see Things to Know).

## Listing Everything

`GET_FILES_AND_FOLDERS` (used by the HcomDiagUi "File List - Nested" command; current Meadow.CLI does
not send it) lists **every device, directory and file reachable from `/`**, nested, using
`hcom_file_lists_all_dev_dir_and_files()` in `hcom_file_lists.c`. Unlike the listing above it accepts no
path and sends no header or totals. Each line is a `TEXT_INFORMATION` message:

| Entry | Line |
|-------|------|
| Directory | `name/`, indented three spaces per level |
| Anything else | `name [type]` with `type` one of file, char, block, link, fifo, sem, mq, shm, mtd, sock or unknown |

The walk is iterative, not recursive: it keeps a stack of open directory levels on the heap (starting
at 8 levels and doubling when needed, each level holding a `PATH_MAX` path buffer), so deep trees cannot
overflow the task stack. The top-level `/proc` directory is skipped unless the request's `userData` is
non-zero; a directory called `proc` further down is listed. `/proc` is only mounted in builds with both
`CONFIG_FS_PROCFS` and NSH. A subdirectory that cannot be opened is logged and skipped.

Because it starts at `/`, this walk covers the whole device tree, `/dev` as well as the file systems, so
it can send a large number of messages.

## Messages the Device Sends

| Message | Used by |
|---------|---------|
| `TEXT_ACCEPTED`, `TEXT_CONCLUDED` | every request here (upload's start request and the data request differ, see above) |
| `INIT_UPLOAD_OKAY`, `INIT_UPLOAD_FAIL` | upload and initial bytes |
| `UPLOADING_FILE_DATA`, `UPLOAD_FILE_COMPLETED` | upload data |
| `SEND_INITIAL_FILE_BYTES` | initial bytes |
| `TEXT_LIST_HEADER`, `TEXT_LIST_MEMBER`, `TEXT_CRC_MEMBER` | `LIST_FILES_SUBDIR` and its CRC form |
| `TEXT_INFORMATION` | nested listing lines; delete and upload results |
| `TEXT_ERROR` | every failure |

## Things to Know

- **Upload does not notice a lost data packet.** The loop ignores the result of each send. If the host
  stops reading, or a send stalls past its budget, that packet is lost and the loop carries on, sends
  `UPLOAD_FILE_COMPLETED` and reports "successfully sent". Only the host's own comparison with the CRC32
  from `INIT_UPLOAD_OKAY` would reveal it. (A send to a host that is not listening returns success, so
  the device also reads an entire file for nobody.)
- **The file is read twice.** Once for the CRC at the start, and again to send it.
- **The header is sent before the directory is checked**, so a failed listing shows the host a header
  and then an error.
- **Delete reports success text even if parent clean-up failed.** The "File '...' deleted" message is
  sent whatever happens in step 2, although a failed `rmdir()` is returned as an error.
- **Of the operations here, only delete is blocked while Mono is enabled** (downloads are too, see
  [FileDownload.md](FileDownload.md)). Uploads and listings are allowed.
- **`UPLOAD_FILE_INIT` keeps a descriptor open between its two requests.** If the host never sends
  `UPLOAD_START_DATA_SEND`, the file stays open until another file request frees the structure; and that
  other request discards the prepared upload.
- **Stale comments in the source.** The sequence description in `hcom_file_upld_proc.c` gives
  `UPLOADING_FILE_DATA` as 0x0312 (it is 0x0403); `hcom_file_lists.c` opens with a comment about "the old
  file list scheme that is not able to display subdirectories", but the file contains the nested
  all-devices listing, not an old list.
- **One message per list line.** Every entry is a separate message through the send lock, so a long
  listing to a slow host can be held up by the stall budget on any one of them.

## Source Map

| Concern | File |
|---------|------|
| Upload and initial bytes | `apps/system/hcom/file/hcom_file_upld_proc.c` |
| Delete (and empty-directory clean-up) | `apps/system/hcom/file/hcom_file_delete_stm32f7.c`, `hcom_file_misc.c` (`hcom_file_misc_delete_existing()`) |
| Directory listing, with and without CRC | `apps/system/hcom/file/hcom_file_lists_subdir.c` |
| Nested listing of everything | `apps/system/hcom/file/hcom_file_lists.c` |
| CRC32 of a file | `apps/system/hcom/file/hcom_file_misc.c` |
| Path parsing and the shared structure | `apps/system/hcom/file/hcom_file_dir_mgmt_utils.c`, `F7MeadowOS/include/meadow/hcom_dnld_shared.h` |
| Routing and the safety-net Concluded | `apps/system/hcom/comms/hcom_host_route.c`, `hcom_host_preprocess.c` |

## Related Documents

[FileDownload.md](FileDownload.md), [WireProtocol.md](WireProtocol.md),
[RequestRouting.md](RequestRouting.md), [SendPipeline.md](SendPipeline.md),
[SdCard.md](../SdCard.md).
