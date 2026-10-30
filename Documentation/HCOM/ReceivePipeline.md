# The CLI Receive Pipeline

## Overview

Everything the host sends arrives through a two-thread pipeline in the HCOM task. One thread does
nothing but read the USB serial port; a second thread cuts the byte stream into packets, decodes
them and acts on them. A circular buffer and two semaphores sit between them.

```
   USB serial port  /dev/ttyACM0
          |
          |  read(), up to 8192 bytes at a time
          v
   +-----------------------+                         +--------------------------------+
   | "HcomRecv" thread     |  add bytes              | "HcomProc" thread              |
   | hcom_host_recv_pthread|------+                  | (the hcom_main thread)         |
   +-----------------------+      |                  |                                |
          ^                       v                  |  get next packet               |
          |                +-------------+  packet   |  COBS decode                   |
          | _runRecvSem    | circular    |---------->|  size checks                   |
          +----------------|  buffer     |           |  hcom_host_handle_packet()     |
            "space freed"  | 4 x 8232 B  |<----------|  -> routing, handlers, replies |
                           +-------------+  _runProcSem                               |
                                              "data arrived"                          |
                                                     +--------------------------------+
                                                                   ^
                                  download watchdog (3 s timer) ---+  wakes this thread
```

This document describes the receiving side up to the point where a decoded packet is handed to the
request handlers; what happens next is in [RequestRouting.md](RequestRouting.md). The reverse
direction is in [SendPipeline.md](SendPipeline.md), and how the port itself is opened and kept is in
[HostLinkUsb.md](HostLinkUsb.md).

## The Two Threads

| | `HcomRecv` | `HcomProc` |
|---|------------|-----------|
| Function | `hcom_host_recv_pthread()` in `hcom_host_receive.c` | `hcom_host_process_run_loop()` in `hcom_host_preprocess.c` |
| Created | a `pthread` made by `hcom_host_recv_setup()` | none: it is the `hcom_main` thread itself, which calls `hcom_host_process_setup()` last and never returns |
| Priority | 180 (`HCOM_THREAD_PRIORITY_HCOM_RECEIVE`) | 100 during start-up, then 180 (`HCOM_THREAD_PRIORITY_HCOM_PROCESS`, set with `sched_setparam()` just before the loop starts) |
| Stack | 2048 bytes | 65,536 bytes: the init task's `CONFIG_INIT_STACKSIZE` (see [UserspaceStartup.md](UserspaceStartup.md)) |
| Job | read bytes; put them in the buffer | find packets; decode; dispatch |
| Blocks on | `read()`; `_runRecvSem` when the buffer is full | `_runProcSem` when no complete packet is waiting |

Both run at the same priority on purpose. The comment in `meadow_thread_config.h` records that
making the processing thread higher than the receive thread produced "very rare download errors",
while equal priorities showed none, and that the author believed the hand-off in
`hcom_host_enq_deq.c` could still be improved.

The processing thread also unblocks `SIGALRM` before it starts looping, because it must react to it.

## Stage 1: Reading the Port (`HcomRecv`)

`hcom_host_recv_setup()` allocates one receive buffer, subscribes to low-power notifications (when
power management is built in), creates two semaphores (the low-power gate and a lock around the
descriptor) and starts the thread. The
thread's first act is to release the startup manager (`hcom_startup_mgr_release_sem()`), so HCOM's
start-up continues while it goes on to open the port.

Its loop is:

1. If low power is imminent, wait on the low-power semaphore until it has ended
   ([HostLinkUsb.md](HostLinkUsb.md#low-power-stop-mode)).
2. If there is no open descriptor, open `/dev/ttyACM0` (retrying as described there).
3. The first time after boot, send the `TEXT_CONCLUDED` owed for a host-requested restart.
4. Call `hcom_host_recv_received_data()`, which loops on a blocking `read()` into the receive buffer
   and passes each chunk straight to `hcom_host_enq_deq_enqueue_rcvd_data()`. It returns only on an
   error, and says whether a one-second delay is wanted before reconnecting.
5. Close the descriptor and go round again.

Details of the read:

- The read length is `HCOM_PROTOCOL_CURRENT_PACKET_MAX_SIZE` (8192). The receive buffer is 8745
  bytes (`HCOM_PROTOCOL_SAFE_ENCODED_MSG_BUF_SIZE` plus 512 plus 1). The 512-byte
  `HCOM_HOST_RECEIVE_MAX_READ_SIZE` only contributes to that buffer size. A comment notes that a
  request above about 256 bytes makes no difference in practice, because the USB driver hands over
  what it has, so chunks are usually small.
- The thread does no parsing. It does not look for delimiters, does not decode, and does not care
  where a packet starts or ends.
- The error handling (which errors delay, which are ignored, what a zero-length read does) is in
  [HostLinkUsb.md](HostLinkUsb.md#connecting-and-reconnecting).

## Stage 2: The Circular Buffer

The buffer (`meadow_cirbuf.c`, type `host_com_cir_buffer_t`) is a byte ring that understands one
thing: a packet is "everything up to and including the next delimiter byte". It is created by
`hcom_host_enq_deq_setup()` with a delimiter of `0x00` and a capacity of
`HCOM_CIRCULAR_BUF_MEM_SIZE`: `HCOM_CIR_BUFFER_MAX_PACKETS` (4) times the 8232-byte maximum encoded
packet, 32,928 bytes. One byte is always kept free so that "head equals tail" can only mean empty.

| Function | Behaviour |
|----------|-----------|
| `hcom_cirbuf_add_bytes()` | Adds the whole block or nothing: `WONT_FIT` if the free space is less than the block, `BAD_ARG` for a zero-length block. Wraps around the end of the memory. |
| `hcom_cirbuf_get_next_packet()` | Searches from the tail for the delimiter, in the upper part and then, if the data wraps, in the lower part. Copies out the packet including its delimiter and moves the tail past it (`FOUND_MSG`), or reports `NONE_FOUND`. |
| (too large) | If the packet is longer than the destination (8232 bytes), it is skipped without copying and `DELETED_TOO_BIG` is returned with the size. |
| `hcom_cirbuf_clear_buffer()` | Resets head and tail to the start, discarding everything. Used by the watchdog and by the full-buffer recovery (below). |

The buffer has **no locking**. It is safe only because exactly one thread adds (moving the head) and
exactly one removes (moving the tail), with the semaphores below as signals. The header file says
that any new use that does not fit that pattern needs real locking. (The ESP32 receive path uses its
own instance single-threaded; it is not covered here.)

Because a packet is simply "up to the next `0x00`", an empty frame produces a one-byte packet (just
the delimiter). The processing thread discards those.

## Stage 3: The Hand-Off and Flow Control

The two threads coordinate through two counting semaphores, both created with priority inheritance
turned off (`SEM_PRIO_NONE`), because they are used for signalling rather than as locks.

| Semaphore | Posted by | Waited on by | Meaning |
|-----------|-----------|--------------|---------|
| `_runProcSem` | the receive thread after each chunk is added (and when the buffer is full); the watchdog | the processing thread | "there may be a packet to process" |
| `_runRecvSem` | the processing thread each time it removes a packet, or when it discards a full buffer | the receive thread, only when the buffer is full | "there may be room now" |

A flag, `_recvWaitingForSpace`, is set by the receive thread while it waits on `_runRecvSem`, so that
the processing thread can tell a full buffer from an empty one.

**Receive side** (`hcom_host_enq_deq_enqueue_rcvd_data()`), per chunk:

```
   add_bytes(chunk)
     SUCCESS   -> post _runProcSem; return (go and read more)
     WONT_FIT  -> post _runProcSem          (make sure the processor is awake and draining)
                  set _recvWaitingForSpace; post _runProcSem again
                  wait  _runRecvSem         (sleep until a packet has been removed)
                  clear _recvWaitingForSpace; try again
```

The reader therefore stops calling `read()` while the buffer is full. The host-side USB driver's own
buffering then fills and the host's writes should stall: backpressure is end to end.

**Processing side** (`hcom_host_enq_deq_dequeue_packet()`):

```
   loop:
     get_next_packet
       FOUND_MSG      -> post _runRecvSem; use up one pending wake-up on _runProcSem (trywait);
                         clear any stale watchdog flag; return the packet
       NONE_FOUND     -> if _recvWaitingForSpace: the buffer is full and holds no delimiter;
                         log "Receive buffer full with no delimiter, data discarded",
                         clear the buffer, clear the flag, post _runRecvSem; loop
                         wait _runProcSem  (a chunk arrived, or the watchdog fired)
                         if the watchdog fired: run the download clean-up, return -ETIME
                         otherwise loop
       DELETED_TOO_BIG-> log "Message too big, deleted"; loop
```

Notes on the design:

- **The semaphore counts are approximate.** Each post to a semaphore adds one to its count, so the
  count of `_runProcSem` is the number of "data may have arrived" wake-ups that have been posted but
  not yet used. It is not a measure of the data in the buffer: the receive thread posts once per
  *chunk*, but a chunk may contain several packets or only part of one, so the number of posts does
  not equal the number of packets. The fast path uses up one pending wake-up when it finds a packet
  without waiting, to stop them piling up, and the loop tolerates too many or too few by always
  re-checking the buffer. A leftover wake-up costs one extra trip round a loop; it never loses data.
- **The fast path clears the watchdog flag.** If a packet was found, data is flowing, so any pending
  watchdog wake-up is moot. If the download really has stalled, its timer is re-armed for each packet
  and will simply expire again.
- **A packet is cut at the first delimiter after the tail.** There is no minimum length and no
  checksum; validation is the processing thread's job.

## Stage 4: The Processing Thread (`HcomProc`)

`hcom_host_process_run_loop()` repeats, until shutdown:

1. **Dequeue** the next packet into a buffer of 8232 bytes. A `-ETIME` result means the watchdog
   fired; the thread logs "Watchdog timeout during download", frees any download state
   (`hcom_dir_mgmt_free_file_info()`) and carries on.
2. **Strip the trailing delimiter** and **COBS-decode** into a second 8232-byte buffer
   (`hcom_host_cobs_decoder()`). A result of 0, meaning an empty frame or an encoding that does not
   decode, is silently skipped.
3. **Check the decoded size.** More than 8192 bytes gives `TEXT_ERROR` "Received oversized packet",
   and the packet is dropped.
4. **Handle the packet** with `hcom_host_handle_packet()`: it is a command or data packet, is
   validated, and is routed. If it returns an error, the loop reports it to the host and frees the
   download state.
5. **Send the Concluded** if the request has not yet sent one
   (`hcom_host_conclude_if_pending()`), then go back to step 1.

The two buffers and the download-state structure are allocated once, at startup, and kept for the
life of the device. Step 4 onwards is described in [RequestRouting.md](RequestRouting.md); the
wire-level rules it enforces are in [WireProtocol.md](WireProtocol.md).

## The Download Watchdog Hook

A host can stop sending in the middle of a file download (the cable is pulled, the CLI crashes).
Nothing in the pipeline would notice, because the processing thread is simply asleep waiting for
data, so a 3-second watchdog is armed for every download (`HCOM_FILE_DNLD_STM32F7_WDOG_TIME_SEC`).

```
   download data packet ---> re-arm the timer (work_queue on USRWORK, 3 s)   (per packet)
          ...
   no more packets for 3 s
   USRWORK thread:   hcom_host_watchdog_worker()
                       -> hcom_host_enq_deq_dnld_watchdog_wakeup():  set flag, post _runProcSem
   HcomProc thread:  wakes in hcom_host_enq_deq_wait_for_work(), sees the flag
                       -> hcom_host_watchdog_dnld_cleanup_wdog():
                            cancel the timer
                            close the partly written file and delete it
                            clear the circular buffer
                            send DNLD_FAIL_RESEND ("File '...' download failed, resend")
                            set the download state to None
                       -> returns -ETIME to the processing loop
```

The timer expiry deliberately does no clean-up itself. The work is handed to the processing thread
because that is the thread that opened the file, and in NuttX a descriptor can only be used reliably
from the task group that owns it. The same timer serves the ESP32 firmware download, which has its
own abort path; the cleanup function checks which download was active.

The timer is a one-shot `work_queue()` item on the user-space work queue (`CONFIG_LIBC_USRWORK`,
4096-byte stack in the default configuration). The file-transfer details are in
[FileDownload.md](FileDownload.md).

## Shutdown

`hcom_host_recv_shutdown()`, `hcom_host_enq_deq_shutdown()` and `hcom_host_process_shutdown()` exist
and free the buffers and semaphores, but `hcom_manager_shutdown()`, which calls them, is never
called in the current code. In practice the pipeline runs until the device is reset.

## What Goes Wrong, and What Happens

| Input or situation | Result |
|--------------------|--------|
| Host stops sending mid-download | Watchdog clean-up after 3 s; `DNLD_FAIL_RESEND` |
| Host stops sending mid-packet, then a new session starts | The new session's leading delimiter ends the stale partial packet. It decodes to garbage or fails to decode, and is normally refused with an error reply (undersized, bad version, unexpected data or unknown request) |
| A complete packet that is too large (over 8232 encoded bytes) | Discarded, "Message too big, deleted" in syslog; no reply |
| A decoded packet that is too large (over 8192 bytes) | `TEXT_ERROR`, dropped |
| Host sends faster than the device processes | The buffer fills, the reader blocks, and the USB link stalls the host |
| More than the buffer's capacity arrives with no `0x00` delimiter | The buffered bytes are discarded, "Receive buffer full with no delimiter, data discarded" in syslog, and receiving carries on; no reply |

## Things to Know

- **A flood without a delimiter is discarded.** A conforming host never fills the buffer without a
  delimiter (every encoded packet is under 8232 bytes and ends in one), so a full buffer with no
  complete packet means malformed or non-COBS input. Before the fix for Issue #820 both threads then
  waited for each other until the device was reset; now the processing thread discards the whole
  buffer. Any valid packet that was arriving at the time is lost with it.
- **The semaphores are counting and unbounded.** Every packet found posts `_runRecvSem` even when the
  receive thread is not waiting, so unused wake-ups accumulate and later cause a few extra loop passes
  before the thread really sleeps. Harmless, but not a strict "space available" signal.
- **`HCOM_HOST_RECEIVE_MAX_READ_SIZE` is not the read size.** It only adds to the buffer size; the read
  asks for 8192 bytes.
- **`hcom_host_recv_receiving_loop()` is declared in `hcom_common.h` but not defined anywhere.**
- **Equal priorities are part of the design.** Changing either thread's priority has caused download
  errors before (see the comment quoted above).

## Source Map

| Concern | File |
|---------|------|
| Reading the port, reconnecting, low-power gate | `apps/system/hcom/comms/hcom_host_receive.c` |
| Hand-off, semaphores, flow control | `apps/system/hcom/comms/hcom_host_enq_deq.c` |
| Packet-cutting ring buffer | `apps/system/hcom/misc/meadow_cirbuf.c`, `F7MeadowOS/include/meadow/meadow_cirbuf.h` |
| Processing loop and packet handling | `apps/system/hcom/comms/hcom_host_preprocess.c` |
| COBS decode | `apps/system/hcom/comms/hcom_host_encode.c` |
| Download watchdog | `apps/system/hcom/comms/hcom_host_watchdog.c` |
| Thread priorities and names | `F7MeadowOS/include/meadow/meadow_thread_config.h` |
| Buffer and timing constants | `apps/system/hcom/hcom_common.h` |

## Related Documents

[WireProtocol.md](WireProtocol.md), [HostLinkUsb.md](HostLinkUsb.md),
[SendPipeline.md](SendPipeline.md), [RequestRouting.md](RequestRouting.md),
[FileDownload.md](FileDownload.md), [UserspaceStartup.md](UserspaceStartup.md).
