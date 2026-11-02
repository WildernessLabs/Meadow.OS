# The CLI Send Pipeline

## Overview

Everything the device sends to the host goes through one function and one lock. Unlike the receive
side there is **no send thread**: whichever thread has something to say calls a send function, takes
the send lock, encodes the message, writes it to the USB serial port and releases the lock. This
document describes that path, how it copes with a host that is not listening (the normal state of a
deployed device), and the timing rules.

```
   HcomProc    Traverse    MonoStdxxx    RemoteDbg    HcomRecv
   (replies,   (kernel     (Mono's       (debugger    (post-restart
    file data)  trace)      output)       bytes)       Concluded)
       |           |            |             |            |
       +-----------+-----+------+-------------+------------+
                         |   hcom_host_send_*()  (any thread in the HCOM task group)
                         v
              +--------------------------+
              |  _hostXmitSem  (a lock)  |   one message at a time
              +------------+-------------+
                           v
              host reachable?  no  -> discard the message, return OK
                           | yes
                           v
              build header + body -> COBS encode -> 00 <encoded> 00
                           |
                           v
              write(/dev/ttyACM0, O_NONBLOCK) in a loop, retrying while the driver is full
                           |
                           v
              Host
```

The matching input side is [ReceivePipeline.md](ReceivePipeline.md); the format of what is sent is
in [WireProtocol.md](WireProtocol.md); how the port is opened is in
[HostLinkUsb.md](HostLinkUsb.md).

## Who Sends

| Sender | Thread | What it sends |
|--------|--------|---------------|
| Request handlers | `HcomProc` (the `hcom_main` thread) | `TEXT_ACCEPTED`, information and errors, replies, file-list lines, upload data, `TEXT_CONCLUDED` |
| Kernel trace and notices | `Traverse` (reads the kernel-to-user FIFO) | `TEXT_TRACE_MSG`, `TEXT_NEXT_LOW_PWR`, other kernel text |
| Mono output | `MonoStdxxx` | `TEXT_MONO_STDOUT`, `TEXT_MONO_STDERR` |
| Debugger bridge | `RemoteDbg` | `DEBUGGING_MONO_DATA` |
| Post-restart notice | `HcomRecv`, at first connection | `TEXT_CONCLUDED` after a host-requested restart |

They all share the one lock, so a slow send by one delays every other sender. A long upload, which
may wait up to 30 seconds for the host (see [Timing](#timing)), holds up trace output and Mono's
output for as long as it is stalled.

## The Send Functions

There are two generations of functions. The first generation builds the header itself; in the second
the caller fills in a structure and the function only stamps the version.

| Function | Gen. | Returns | Header it builds | Stall budget |
|----------|------|---------|------------------|--------------|
| `hcom_host_send_header_msg()` | 1 | void | sequence 0, version, type, `userData` | 1.5 s |
| `hcom_host_send_simple_string_msg()` | 1 | void | as above, text body cut at the first CR or LF | 1.5 s |
| `hcom_host_send_raw_string_msg()` | 1 | int | as above, body sent exactly as given | 1.5 s |
| `hcom_host_send_binary_data_msg()` | 1 | void | as above, binary body | 1.5 s |
| `hcom_host_send_std_header_msg()` | 2 | int | stamps sequence 0 and the version on the caller's struct | 1.5 s |
| `hcom_host_send_std_data_msg()` | 2 | int | stamps the version only (the caller sets the sequence number) | 1.5 s |
| `hcom_host_send_std_data_msg_long_wait()` | 2 | int | as above | **30 s** |

All first-generation functions end in `hcom_host_send_buffered_msg()`, and the second-generation ones
in `hcom_host_send_standard_msg()`. Both lead to the one transmit routine,
`hcom_host_send_transmit_to_host()`. The source comments describe the first generation as superseded
by the second, but "the time to refactor the code they support has never been made available".

Only file upload uses the second-generation functions (`hcom_file_upld_proc.c`): the plain
`hcom_host_send_std_data_msg()` has no callers. The long-wait variant exists because the source
records that the CLI can stall for up to 20 seconds while processing received data.

Return values:

| Result | Meaning |
|--------|---------|
| `OK` | The message was written, **or it was discarded because no host is listening**. Callers cannot tell the two apart. |
| `-EAGAIN` | The send stalled for longer than its budget partway through, or send was called before `hcom_host_send_setup()` ran |
| `-EMEADOW_TASK_GRP_MISMATCH` | Called from a thread outside the HCOM task group |
| `-ENOMEM` | The lazily created transmit buffer could not be allocated |
| `-errno` | Any other write failure |

The `void` functions log failures other than `-EAGAIN` (with the caller's file and line) and
otherwise swallow them.

## What Happens on a Send

1. **Initialised?** Before startup reaches `hcom_host_send_setup()`, a send returns `-EAGAIN`, so
   messages sent earlier than that are lost.
2. **Take the lock** (`_hostXmitSem`, created with an initial count of 1).
3. **Is the host reachable?** (see below). If not, release the lock and return `OK`: the message is
   thrown away, and none of the following checks happen.
4. **Build the message** (first-generation functions; the second generation sends the caller's
   structure as it is). A header-only message is built on the stack. A message with a body is built
   in a buffer of 8192 bytes allocated on first use and then kept; per the source comment, most
   deployed devices never send such a message and so never pay for it. A body longer than the
   maximum payload (`HCOM_PROTOCOL_COMMAND_MAX_PAYLOAD_LEN`) is cut to that length before it is
   copied, so a message never exceeds 8192 bytes; the rest of the body is silently dropped.
5. **In the transmit routine, `hcom_host_send_transmit_to_host()`:**
   - A caller outside the HCOM task group gets `-EMEADOW_TASK_GRP_MISMATCH` (the check compares
     `getpid()` with the group recorded at setup, because the descriptor only works in the task
     group that opened it).
   - After shutdown it returns `OK` without sending.
6. **Encode.** The packet is COBS-encoded into a second buffer (`_encodedXmitBuff`, 8232 bytes,
   allocated at setup), leaving one byte free in front for a leading `0x00`; a trailing `0x00` is
   added after. If the result would overflow the buffer the code calls `PANIC()`; a packet within the
   protocol limit cannot do that.
7. **Write loop.** Write until every byte has gone, handling short writes, interruptions and a full
   driver (see below).
8. **Release the lock.** On success `_lastXmitBlocked` is cleared.

## Is the Host Listening?

A deployed device usually has no host attached, or has one on the port but no program reading it.
The code must not stall every send or leak memory in that state, so it tracks one flag,
`_lastXmitBlocked`, which starts out true.

```
   _lastXmitBlocked false (the last send worked)  -> just send
   _lastXmitBlocked true:
        no write descriptor? -> try to open it (4 attempts, 250 ms apart)
                                 cannot open -> report "blocked"
        write a single 0x00 probe byte (non-blocking)
              accepted (returned 1) -> clear the flag, go ahead and send
              refused               -> keep the flag, report "blocked"
```

While blocked, each send costs one failed one-byte write and the message is discarded. The probe byte
is the same `0x00` the framing ignores. Its comment explains the reasoning: whatever NuttX still has
buffered when a host finally starts reading may be half a message, so the next thing to arrive is
always a delimiter, after which the first complete message parses properly. This is also why every
message begins with a delimiter.

The flag becomes true again when a write stalls for longer than its budget, or when a write fails
with an error.

## The Write Loop and Its Timing

The descriptor is opened `O_WRONLY | O_NONBLOCK`. The source records the reason: without
`O_NONBLOCK` the write blocks after some bytes have been accepted, "as if some internal buffer(s)
fills", and a blocked write would freeze the calling thread.

The loop writes the remaining bytes. After each call:

| Result of `write()` | Action |
|---------------------|--------|
| Bytes written | Subtract them, keep going, and forget any stall in progress |
| `EINTR` | Try again |
| `EAGAIN` or zero bytes | The driver is full. If this stall has lasted less than the budget: `poll()` for the descriptor to become writable (50 ms at most), then sleep at least 2 ms, then try again. Otherwise give up: set `_lastXmitBlocked` and return `-EAGAIN` |
| `ENOTCONN` after low power | Close, reopen, and continue sending the remaining bytes (see [Low Power](#low-power)) |
| `ENOTCONN` otherwise | Close the descriptor, set the blocked flag, and return `OK` (the cable was pulled mid-send; treated as sent) |
| Any other error | Close the descriptor, set the blocked flag, return `-errno` |

### Timing

| Constant | Value | Role |
|----------|-------|------|
| `HCOM_XMIT_MAX_BLOCKED_MSEC_NORMAL` | 1500 ms | Stall budget for ordinary messages |
| `HCOM_XMIT_MAX_BLOCKED_MSEC_LONG` | 30,000 ms | Stall budget for upload data (the long-wait variant) |
| `HCOM_XMIT_MAX_BLOCKED_TIME_DELAY` | 50 ms | Longest single `poll()` wait |
| `HCOM_XMIT_MIN_RETRY_DELAY_USEC` | 2 ms | Floor on each retry |
| `HCOM_COMMS_MAX_XMIT_OPEN_ATTEMPTS` | 4 | Open attempts, 250 ms apart |

The budget is real elapsed time measured from the first `EAGAIN` of a stall (`CLOCK_MONOTONIC`). The
2 ms floor was added because, as the comment records, "confirmed on hardware" that `poll()` can
report the descriptor writable, or time out, in under a millisecond while `write()` keeps failing,
which let the old retry count run out almost instantly without the host getting any time to catch up.

What this means in practice: when a host program is attached but not reading, the first message that
fills the driver costs its sender up to 1.5 seconds (30 seconds for upload data); after that the
flag is set and sends cost almost nothing. A message that gave up partway leaves a partial frame in
the driver, which the next message's leading delimiter ends.

## The "Concluded Pending" Mechanism

Every request must end with exactly one `TEXT_CONCLUDED`
([WireProtocol.md](WireProtocol.md#the-request-conversation)). The send code keeps the state that
makes that checkable, in two volatile variables with three small functions:

| Function | Effect |
|----------|--------|
| `hcom_host_send_conclude_pending_set(rqstType)` | A command packet has arrived (called by the request processor): mark "pending" and remember its type |
| `hcom_host_send_conclude_is_pending(&rqstType)` | Is the current request still unconcluded, and what was it? |
| `hcom_host_send_conclude_pending_clear()` | Forget the request without sending a `TEXT_CONCLUDED` (for requests that conclude later) |

The mark is also cleared automatically whenever a `TEXT_CONCLUDED` is passed to
`hcom_host_send_buffered_msg()`, **even if the message is then discarded** because the host is not
listening ("the current request is answered, even if this send fails; a retry wouldn't fare better").
Only the first-generation path does this. The decision about when to send the missing
`TEXT_CONCLUDED` is made by the request processor
([RequestRouting.md](RequestRouting.md)).

## Low Power

The send code is one of three modules that register for low-power notifications
([HostLinkUsb.md](HostLinkUsb.md#low-power-stop-mode)):

- **Before sleep:** the callback takes the send lock, which waits for any send in progress to finish.
  If the write descriptor is open it closes it and sets `_lastXmitBlocked`. It sets
  `_lowPowerActive`. It keeps the lock, so every sender blocks until the sleep has ended.
- **After sleep:** the callback releases the lock.
- **Recovery afterwards:** the next send finds the blocked flag set and goes through the probe. If a
  message write (as opposed to the probe) later fails with `ENOTCONN` while `_lowPowerActive` is set,
  the flag is cleared, the port is reopened and the message continues from the byte where it left
  off. The probe has no such handling: a probe that fails with `ENOTCONN` just reports "blocked" and
  the message is discarded.

## Keeping the Logger From Calling Itself

Sending can itself want to log an error. If kernel trace is being forwarded to the host, logging from
inside the send path would produce a trace message, which would be sent, which could fail and log
again. `syslog_if_safe()` breaks the loop: with `CONFIG_RAMLOG_SYSLOG` set and trace-to-host switched
on (a battery-backed flag, `HCOM_BBREG_ROUTE_TRACE_MSG_TO_HOST_BIT`), it discards the log line; when
trace to host is off, it logs normally. Without a RAM log the line always goes to the ordinary
syslog.

## Things to Know

- **A discarded message looks like a successful one.** When the host is not listening, the send
  functions return `OK`. Code such as the upload loop therefore keeps reading and "sending" a file to
  nobody, rather than stopping.
- **One slow send stalls every sender.** The lock is held for the whole write, including the stall
  budget, so a stuck upload also delays Mono's output and kernel trace.
- **A partly sent message is not retried from the start after low power.** The resend after an
  `ENOTCONN` carries on from the bytes already written, so if the link dropped in the middle of a
  frame the host receives a truncated one, which it will normally reject.
- **Opening the port can take a second.** With the node unavailable (for example just after waking),
  four attempts 250 ms apart keep the lock held for about a second.
- **The PM callback and the send paths use slightly different tests for a valid descriptor**
  (`> 0` in the callback, `> 1` in the open routine, `== -1` in the blocked check). Since descriptors
  0 to 2 are the standard streams, the difference does not matter in practice.

## Source Map

| Concern | File |
|---------|------|
| Everything on this page | `apps/system/hcom/comms/hcom_host_send.c` |
| COBS encoding | `apps/system/hcom/comms/hcom_host_encode.c` |
| Declarations | `apps/system/hcom/hcom_common.h` |
| Opening the port and its device name | `apps/system/hcom/comms/hcom_host_receive.c` (`hcom_host_recv_get_device_name()`) |
| Callers: kernel trace thread | `apps/system/hcom/diag/hcom_traverse_ku_user.c` |
| Callers: Mono output | `apps/system/hcom/mono/hcom_stdxxx_redirect.c` |
| Callers: debugger bridge | `apps/system/hcom/mono/hcom_remote_vs_debug.c` |
| Callers: file upload | `apps/system/hcom/file/hcom_file_upld_proc.c` |

## Related Documents

[WireProtocol.md](WireProtocol.md), [HostLinkUsb.md](HostLinkUsb.md),
[ReceivePipeline.md](ReceivePipeline.md), [RequestRouting.md](RequestRouting.md),
[MonoLifecycle.md](MonoLifecycle.md), [TraceSyslog.md](TraceSyslog.md).
