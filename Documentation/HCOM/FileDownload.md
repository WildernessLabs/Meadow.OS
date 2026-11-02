# File Download (Host to Device)

## Overview

A download writes a file sent by the host into the device's flash file system (`/meadow0`) or onto
an SD card (`/sdcard`). It is a three-phase transaction: a **start** request that opens the file, a
stream of **data packets**, and an **end** request that closes the file and verifies it. The
transport adds no acknowledgements or retransmission, so this code supplies its own integrity check
(a CRC32 and a size check) and its own stall detector (a 3-second watchdog).

```
   Host                                     Meadow                         state
    |-- START_FILE_TRANSFER ----------------->|  parse path, checks            Invalid -> Starting
    |       (size, CRC32, path)               |  delete any old file
    |                                         |  create the new file
    |<-------------------- TEXT_ACCEPTED -----|  arm 3 s watchdog              -> FileXfer
    |<-------------- INIT_DOWNLOAD_OKAY ------|
    |-- data seq 1 ------------------------->|  CRC += data; write; re-arm
    |-- data seq 2 ------------------------->|  ...
    |<---------- TEXT_INFORMATION "File 10% downloaded"   (about every 10 %)
    |       ...                               |
    |-- END_FILE_TRANSFER ------------------->|  cancel watchdog; close
    |                                         |  read the file back, compare     -> None
    |<-------- TEXT_INFORMATION | TEXT_ERROR -|
    |<------------------ TEXT_CONCLUDED ------|
```

The same code serves the Mono Runtime update (`MONO_UPDATE_RUNTIME` and `MONO_UPDATE_FILE_END`),
which differs only in what happens after the file is written
([MonoLifecycle.md](MonoLifecycle.md)). Downloading to the ESP32 is a separate path and is not covered.

The message formats are in [WireProtocol.md](WireProtocol.md#file-data-packets); the dispatching is in
[RequestRouting.md](RequestRouting.md); the stall detector's mechanics are in
[ReceivePipeline.md](ReceivePipeline.md#the-download-watchdog-hook).

## Which Files Can Be Written

A path is accepted in one of three forms, decided by `hcom_dir_mgmt_categorize_pathname()` in
`hcom_file_dir_mgmt_utils.c`:

| Form | Example | Category | Becomes |
|------|---------|----------|---------|
| A bare name (no `/`) | `app.dll` | `pathnameOriginal` | `/meadow0/app.dll` |
| Starts with `/meadow0/` | `/meadow0/lib/app.dll` | `pathnameMeadow` | the same path |
| Starts with `/sdcard/` | `/sdcard/log/app.txt` | `pathnameSdcard` | the same path |

Everything else is refused. For a download the path must end in a file name, so a trailing `/` is an
error. (The same code is used for listings, where the opposite holds.)

| Path | Result |
|------|--------|
| `/` or `/dev/...` or `/anything/` | Refused: only `/meadow0` and `/sdcard` are reachable |
| `/meadow0/dir/` | Refused: "has ending '/'" |
| `/sdcard/...` when the device has no SD support | Refused: "valid, but not configured" |
| An empty or unrecognised form | Refused: "Path name ... is invalid" |
| More than 8 path elements | Refused: "Maximum subdirectory levels 6, request contained N" |

The 8-element limit (`HCOM_FILE_DNLD_MAX_NUMB_DIR_ELEMENTS`) counts `meadow0` or `sdcard` and the
file name, leaving 6 user sub-directories (`HCOM_FILE_DNLD_MAX_NUMB_USER_SUBDIRS`). A refusal sends a
`TEXT_ERROR` naming the reason, then `INIT_DOWNLOAD_FAIL` and `TEXT_CONCLUDED`
([RequestRouting.md](RequestRouting.md#command-packets)). The SD card form only exists in builds with
`CONFIG_STM32F7_SDMMC2`, and only when the device's configuration enables SD storage
(`sd_storage_supported`).

The code looks only at the prefix and the trailing slash. It does not remove `.` or `..` elements, and
what the file system does with such a path was not examined.

## Before the File Is Opened

`hcom_host_process_init_write_or_del()` (in the preprocessing step) does the following, in this order:

1. **Mono must be disabled.** If the Mono runtime is enabled the download is refused with `TEXT_ERROR`
   "Mono must be disabled for file download", `INIT_DOWNLOAD_FAIL` and `TEXT_CONCLUDED`. The test is the
   enabled setting in the battery-backed register, not whether Mono is currently running. (This blocks a
   file being replaced while the runtime may be using it.)
2. **Parse the path** into the shared file structure, as above.
3. **SD card mounted.** For an `/sdcard` path, `statfs()` on the mount point must succeed (the NuttX
   automounter mounts `/sdcard` when a card is present); otherwise `TEXT_ERROR` "SD-Card not inserted".
4. **Create missing directories.** If the path has more than two elements,
   `hcom_dir_mgmt_check_and_add_subdir()` walks it one level at a time. Each level is `stat()`ed; if it
   does not exist it is created with `mkdir(..., 0777)` and the level is checked again; if it exists
   but is not a directory the request fails with "last element not a directory".

## The Download State

`dnldCurrentState` in the shared structure tracks where a download is:

| State | Meaning |
|-------|---------|
| `HcomStm32F7DnldStateInvalid` (0) | No valid information in the structure (the starting state, and after the structure has been freed) |
| `HcomStm32F7DnldStateNone` (1) | No download in progress; set after every end, failure or watchdog clean-up |
| `HcomStm32F7DnldStateStarting` (2) | `START_FILE_TRANSFER` is being processed |
| `HcomStm32F7DnldStateFileXfer` (3) | The file is open and data is expected |

Starting and FileXfer count as "download active". While the state is active the request router refuses
every command except the end request, and data packets are accepted only in `FileXfer`.

## Step 1: Start

`hcom_file_dnld_stm32f7_file_begin()` runs these steps (`hcom_file_dnld_stm32f7_file_begin_steps()`):

1. Set the state to `Starting`.
2. Save the sender's file size and CRC32 from the file-start body.
3. **Delete any existing file at that path.** `hcom_file_misc_delete_existing()` unlinks it, ignoring
   "no such file". The source comment explains that this was added for what it calls "Meadow Issue #855"
   (not an issue in this repository), where overwriting in place (`O_TRUNC`) gave occasional
   assertions, and that deleting and re-creating is also kinder to LittleFS.
4. **Create the file** with `open(path, O_RDWR | O_CREAT, 0644)`. For `/meadow0` paths it first checks the
   file system is mounted.
5. **SD card only:** allocate the write buffer (below).
6. Set the state to `FileXfer`.
7. **Arm the watchdog** (3 seconds).
8. Send `INIT_DOWNLOAD_OKAY`.

If any step fails, the state returns to `None` and the host is told with `INIT_DOWNLOAD_FAIL`, **sent
exactly once** (a flag, `_initFailSent`, stops the wrapper adding a second). When the failure was in
creating the file, the text says why:

| Error | Text after "File '...' Init for download failed because" |
|-------|-----------------------------------------------------------|
| `-EEXIST` | Another file is being processed |
| `-ENAMETOOLONG` | File name too long |
| `-ENOENT` | No such directory (this includes "file system not mounted") |
| `-EMFILE` | Too many files open |
| anything else | Unexpected error:N |

Other failures (delete failed, out of memory, watchdog could not be armed) send "File download could not
start, error N".

## Step 2: Data

Each data packet (a 2-byte sequence number and up to 8190 bytes of data) is processed by
`hcom_file_dnld_stm32f7_recvd_file_data()`:

1. If the state is not `FileXfer`, the packet is refused with `-ENOTRECOVERABLE` after dropping any SD
   buffer and resetting the state to `None`.
2. **Progress message.** The percentage is computed from the bytes received *before* this packet; when
   its tens digit changes the host gets `TEXT_INFORMATION` "File N% downloaded". So the first message
   comes at 10 %, then 20 %, and so on.
3. **Running CRC.** The data (not the sequence number) is added to a running CRC32 with
   `crc32part()`.
4. **Write.** A `/meadow0` file is written straight away with `hcom_file_write_to_active_file()`; an
   `/sdcard` file is buffered (below). A write failure sends `TEXT_ERROR` "Write of '...', seq N failed",
   resets the state to `None` and returns the error.
5. **Count and re-arm.** The received size is increased and the watchdog is re-armed for another 3
   seconds.

There is no reply to a data packet. The sequence number is only logged.

## Step 3: End

`hcom_file_dnld_stm32f7_file_end()`:

1. **Cancel the watchdog.**
2. If the state is not `FileXfer`, fail with `-ENOTRECOVERABLE`.
3. **SD card only:** write out the last partial buffer.
4. **Close the file**, then **read it back and calculate its CRC32** from the file system
   (`hcom_file_misc_calc_crc_for_file()`, in 1024-byte reads).
5. **Compare and report.** The download succeeds only if the running CRC equals the CRC32 the sender
   gave at the start, **and** equals the read-back CRC, **and** the number of bytes received equals the
   sender's file size. The result is reported as:

| Outcome | Message to the host |
|---------|---------------------|
| All equal | `TEXT_INFORMATION` "Download of '...' succeeded (checksum:0x...)" |
| Size differs | `TEXT_ERROR` "... failed due to file size mismatch calculated:N, sender:N" |
| CRC differs | `TEXT_ERROR` "... failed due to checksum mismatch, received:0x..., sender:0x..., read-back:0x..." |
| The read-back could not be done | `TEXT_ERROR` "... state unknown due to checksum calculation fault:N" |

6. The state returns to `None`. The router then sends `TEXT_CONCLUDED`, and the shared structure is freed.

The three-way check catches different faults: running CRC against the sender's CRC shows damage in
transit (or missing or repeated packets), and read-back against running CRC shows a fault writing to
or reading from flash.

## SD Card Writes

A download to `/sdcard` goes through a 1 KB buffer (`HCOM_FILE_DNLD_SDCARD_BLOCK_SIZE`) aligned to 16
bytes (`HCOM_FILE_DNLD_SDCARD_ALIGNMENT`), allocated at the start and freed at the end or on any error.
The source records the reason: an SD write must be properly aligned or it writes data from outside the
buffer to the file. Incoming data is copied into the buffer, and each time it fills a 1 KB block is
written. The final partial block is written at the end. A leftover buffer from an abandoned download is
freed before the next one allocates its own.

The lower-level write function also copies each SD block into a freshly allocated buffer before writing
it, so SD data is copied twice.

## When Data Stops: The Watchdog

If the host stops sending, the 3-second timer expires and the request pipeline runs
`hcom_host_watchdog_dnld_cleanup_wdog()` on the processing thread
([ReceivePipeline.md](ReceivePipeline.md#the-download-watchdog-hook)). For an F7 download this closes
the file, **deletes the partial file**, clears the receive buffer, sends `DNLD_FAIL_RESEND` ("File '...'
download failed, resend") and sets the state to `None`. The host is expected to start the download again
from the beginning.

## Messages the Device Sends

| Message | When |
|---------|------|
| `TEXT_ACCEPTED` | the start request was recognised |
| `INIT_DOWNLOAD_OKAY` | the file is open; send data now |
| `INIT_DOWNLOAD_FAIL` | the download cannot start (usually preceded by a `TEXT_ERROR`) |
| `TEXT_INFORMATION` | progress (every 10 %) and a successful result |
| `TEXT_ERROR` | refused path, write failure, or a failed verification |
| `DNLD_FAIL_RESEND` | the watchdog gave up |
| `TEXT_CONCLUDED` | the end request has been handled, or the start did not succeed |

## Limits and Constants

| Item | Value | Name |
|------|-------|------|
| Stall timeout | 3 s, re-armed per packet | `HCOM_FILE_DNLD_STM32F7_WDOG_TIME_SEC` |
| Path elements | 8 (6 sub-directories) | `HCOM_FILE_DNLD_MAX_NUMB_DIR_ELEMENTS` |
| Data per packet | up to 8190 bytes | packet size 8192 less the 2-byte header |
| File creation mode | 0644 | `open()` in `hcom_file_write_open_active_file()` |
| Directory creation mode | 0777 | `mkdir()` |
| SD buffer | 1 KB, 16-byte aligned | `HCOM_FILE_DNLD_SDCARD_BLOCK_SIZE`, `_ALIGNMENT` |
| CRC read-back buffer | 1024 bytes | `HCOM_FILE_READ_BUFF_SIZE_FOR_CRC` |

The file size and the byte counts are 32-bit. Two build options, `HCOM_FILE_DNLD_CREATE_MEMORY_CACHE`
(cache the whole file in RAM, 8 MB limit) and `HCOM_FILE_DNLD_CACHE_NO_FILE_ACCESS`, exist for testing,
and are off.

## Things to Know

- **The old file is deleted when a download starts.** If the new download then fails or is abandoned,
  the previous version of the file is gone.
- **A failed verification leaves the file.** When the end request finds a size or checksum mismatch it
  reports `TEXT_ERROR` and sets the state to `None`, but does not delete the file. The host has to delete
  it or download again. (A watchdog timeout, by contrast, does delete the partial file.)
- **`file_end` returns `OK` whatever the result.** The only way the host learns of a failed check is
  the text of the message; the end request itself concludes normally.
- **A failure partway through leaves a partial file and a stream of errors.** A write error closes the
  file and resets the state, but does not delete what was written. Every data packet the host then
  sends is refused, and each one produces its own `TEXT_ERROR` ("File data processing failed"), because
  the processing loop reports every refusal.
- **Sequence numbers are not checked.** A lost or repeated packet is detected only by the final CRC and
  size comparison ([WireProtocol.md](WireProtocol.md#things-to-know)).
- **A short write is not an error.** If `write()` accepts fewer bytes than asked, the shortfall is
  logged and the function still returns success; the size comparison at the end would catch it.
- **Disabling Mono costs a restart.** The check uses the persistent "Mono disabled" setting, which
  `MONO_DISABLE` changes and applies by restarting the device. A host that wants to replace a file while
  Mono is enabled has to disable Mono first, and enable it again afterwards.
- **`dnldFilePartId` is always 1** (the file-system partition); the structure field is vestigial.
- **Failing to re-arm the watchdog aborts the download.** The error returns to the processing loop,
  which reports it, closes the file and frees the shared structure. The partial file stays.

## Source Map

| Concern | File |
|---------|------|
| Start, data, end, SD buffering | `apps/system/hcom/file/hcom_file_dnld_stm32f7.c` |
| Opening, writing and closing the file | `apps/system/hcom/file/hcom_file_write_stm32f7.c` |
| Path rules and sub-directory creation | `apps/system/hcom/file/hcom_file_dir_mgmt_utils.c` |
| CRC32 of a file, deleting an existing file | `apps/system/hcom/file/hcom_file_misc.c` |
| Pre-processing (Mono check, SD check) | `apps/system/hcom/comms/hcom_host_preprocess.c` |
| Watchdog | `apps/system/hcom/comms/hcom_host_watchdog.c` |
| Shared structure and constants | `F7MeadowOS/include/meadow/hcom_dnld_shared.h` |

## Related Documents

[WireProtocol.md](WireProtocol.md), [RequestRouting.md](RequestRouting.md),
[ReceivePipeline.md](ReceivePipeline.md), [FileUploadDeleteList.md](FileUploadDeleteList.md),
[MonoLifecycle.md](MonoLifecycle.md), [SdCard.md](../SdCard.md).
