# The Mono Remote Debugger Bridge

## Overview

To debug managed code running on the device, a debugger on the host (Visual Studio, with the debugging
support in Meadow.CLI) has to talk to Mono's soft-debugger agent inside the Mono task. The two are
connected through HCOM. This document describes the **device side** of that connection:
`apps/system/hcom/mono/hcom_remote_vs_debug.c` and the part of `hcom_mono_control.c` that gives Mono a
debugger socket. The host side is outside this repository.

The bridge is compiled in by default (`CONFIG_HCOM_MONO_REMOTE_DEBUGGING`, default `y`), but its own
header comment calls it a work in progress that "has never been put into use", and says that the
changes made to it after a code review (Issue #326) were **not tested on hardware** because they needed
a Mono runtime that was not then available. Treat the behaviour below as read from the code, not as
verified in use.

```
   Visual Studio  <--->  Meadow.CLI  <--- HCOM over USB --->   HCOM task group              Mono task
                                                         +--------------------------+  +-------------------+
   debugger data ------- DEBUGGING_DEBUGGER_DATA ------->| HcomProc: send() ------- socket -------> debugger |
                                                         |                          |  | agent  (Mono's own |
   <---- DEBUGGING_MONO_DATA ----------------------------| "RemoteDbg" thread:      |  |  socket-fd)        |
                                                         |   poll(), recv() <------ /dev/monodbg ---------- |
                                                         +--------------------------+  +-------------------+
```

The data in both directions is an opaque byte stream; HCOM does not interpret the debugger protocol.

## Starting a Debug Session

A session lasts for **one start** of the device and is requested by the host:

1. The host sends `MONO_START_DBG_SESSION` (0x011e). HCOM replies `TEXT_ACCEPTED`, sets the battery-backed
   bit `HCOM_BBREG_MONO_DEBUGGING_START_BIT` (0x2000), and restarts the device. No `TEXT_RECONNECT` is
   sent for this request, and the `TEXT_CONCLUDED` comes from the new instance after the restart
   ([WireProtocol.md](WireProtocol.md#restart-requests)).
2. On the next start, `hcom_mono_remote_dbg_setup()` (start-up step 15,
   [UserspaceStartup.md](UserspaceStartup.md)) finds the bit, starts the `RemoteDbg` thread and then
   **clears the bit**, so any later restart is a normal one. The result of starting the thread is not
   checked.
3. The thread (priority 130, 2048-byte stack, detached) creates a local stream socket named
   `/dev/monodbg` (`socket(PF_LOCAL, SOCK_STREAM)`, `bind`, `listen` with a backlog of 2), and marks the
   socket active.
4. Later in start-up, Mono is started. Its entry point, `mono_main_proxy()` in `hcom_mono_control.c`, sees
   that a session is active and:
   - opens its own socket and `connect()`s it to `/dev/monodbg` (up to 5 attempts, 100 ms apart; if every
     attempt fails the Mono task ends and **Mono does not start**);
   - builds the command line `--debug`, `--debugger-agent=transport=socket-fd,address=<fd>` and a run
     mode: `--interp` by default, or `--soft-breakpoints` if the configuration chose JIT or AOT;
   - calls `meadow_mono_main()` with it.
5. The `RemoteDbg` thread's blocking `accept()` returns when Mono connects, and forwarding begins.

If no session was requested, `mono_main_proxy()` does not use a socket and simply passes Mono the
configured options, less the first argument. In a debug session the configured Mono options other than the
run mode are **not** passed; only the three arguments above.

## Forwarding

### Mono to host

The `RemoteDbg` thread loops (`hcom_mono_remote_dbg_read_mono_send_to_host_loop()`):

1. `poll()` the connected socket for up to **5 minutes** (`HCOM_MONO_REMOTE_DBG_RECV_TIMEOUT_MS`). The
   timeout is long enough for a developer paused at a breakpoint, and short enough to notice a dead peer.
   The comment records that `SO_RCVTIMEO` could not be used because NuttX local stream sockets ignore it.
2. `recv()` up to 8180 bytes (`HCOM_PROTOCOL_COMMAND_MAX_PAYLOAD_LEN`) into a buffer.
3. Send them to the host as `DEBUGGING_MONO_DATA` (0x0401) with `hcom_host_send_binary_data_msg()`,
   exactly as received.

The data is a byte stream: one message carries whatever bytes happened to be ready, with no relation to the
debugger's own message boundaries. The comment says the host is assumed to "piece it back together".

The first time through, the thread sleeps for **4 seconds** before forwarding. The comment explains that
Visual Studio was missing the first message and that a delay "allows the two systems to establish
communication"; it calls this a stop-gap ("a long term solution is required") and speaks of **2** seconds,
but the code sleeps for 4.

### Host to Mono

A `DEBUGGING_DEBUGGER_DATA` (0x0401) message arrives on the `HcomProc` thread and is handled in the request
router with no `TEXT_ACCEPTED` and no `TEXT_CONCLUDED`, since it is a stream and not a request
([RequestRouting.md](RequestRouting.md#mono)). `hcom_mono_remote_dbg_recv_host_sending_to_mono()` does a
single `send()` of the message body on the connected socket (`_transmit_sd`). If there is no connection
the data is dropped with a syslog error.

### Session end

The session ends when `poll()` times out, `recv()` fails (`ECONNRESET` is logged as "Mono dropped
connection"), or `recv()` returns 0 ("mono broke the connection"). The thread closes the socket, clears
`_transmit_sd`, waits 500 ms and goes back to `accept()`. Mono does not normally reconnect, because its
task ends when it finishes and the device then restarts ([MonoLifecycle.md](MonoLifecycle.md)).

## Shared State

| Item | Written by | Read by | Notes |
|------|-----------|---------|-------|
| `_hcom_mono_remote_dbg_socket_active` | `RemoteDbg`, once the socket is listening | the Mono task's `mono_main_proxy()` | Decides whether to connect to `/dev/monodbg`. Not `volatile`. |
| `_transmit_sd` | `RemoteDbg` on accept and on session end | `HcomProc` when sending to Mono | A plain `int` with no lock |
| `_listen_sd` | `RemoteDbg` | the (never-called) shutdown | Closing it forces `accept()` to return |
| `_shutting_down` | `hcom_mono_remote_dbg_shutdown()` | `RemoteDbg` | `volatile`; the shutdown function is never called |

## Things to Know

- **The debug thread starts only for a requested session.** A normal start never creates it, so
  `/dev/monodbg` does not exist and Mono runs without the proxy's debugger arguments.
- **The session is one-shot.** The request bit is cleared as soon as the thread is started, even if
  starting it failed.
- **A failed connect stops Mono.** If `mono_main_proxy()` cannot connect to the socket after 5 attempts,
  the Mono task exits without starting, and the monitor thread then restarts the device.
- **Sends to Mono can block `HcomProc`.** The single `send()` on a stream socket is not guarded by a
  timeout; if Mono is not reading, the request-processing thread can wait in it, and nothing else is
  processed ([RequestRouting.md](RequestRouting.md)). The number of bytes accepted is not checked, so a
  short send would silently lose the rest.
- **A small window exists on session end.** The thread closes the connected socket before it sets
  `_transmit_sd` to -1, so a host message arriving in between would be sent on a closed descriptor.
- **The two directions share the number 0x0401.** `HCOM_MDOW_REQUEST_DEBUGGING_DEBUGGER_DATA` (host to
  device) and `HCOM_HOST_REQUEST_DEBUGGING_MONO_DATA` (device to host) have the same value; the direction
  distinguishes them ([WireProtocol.md](WireProtocol.md)).
- **No reconnection logic for Mono.** A debug session is expected to last as long as Mono does.
- **Stale or approximate comments.** The header says the module needs "the not-yet-available Mono
  runtime"; Mono is built into the default configuration. The first-message delay comment says 2 seconds
  while the code sleeps 4.
- **A test build can replace Mono.** With `HCOM_VS_DEBUGGING_TESTS_INCLUDE_IN_BUILD`, the Mono entry runs
  `MonoVsRemoteDebugTestSetup()` (`tests/vs_debug_tests.c`) instead of the runtime.

## Source Map

| Concern | File |
|---------|------|
| The socket server and both forwarding directions | `apps/system/hcom/mono/hcom_remote_vs_debug.c` |
| Mono's entry point and its end of the socket | `apps/system/hcom/mono/hcom_mono_control.c` (`mono_main_proxy()`, `hcom_mono_remote_dbg_open_mono_sock()`) |
| Routing | `apps/system/hcom/comms/hcom_host_route.c` |
| Socket name, option strings | `apps/system/hcom/hcom_common.h` (`HCOM_MONO_REMOTE_DBG_*`) |
| Start-up and request bit | `apps/system/hcom/hcom_startup_manager.c`, `F7MeadowOS/include/meadow/hcom_bbreg_defn.h` |
| Test replacement for Mono | `apps/system/hcom/tests/vs_debug_tests.c` |

## Related Documents

[MonoLifecycle.md](MonoLifecycle.md), [WireProtocol.md](WireProtocol.md),
[RequestRouting.md](RequestRouting.md), [UserspaceStartup.md](UserspaceStartup.md).
