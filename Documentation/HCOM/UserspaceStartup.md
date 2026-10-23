# HCOM Startup and the User-Space Threads

## Overview

The `hcom` application is Meadow.OS's user-space init task. NuttX starts it at the end of its own
bring-up, after the kernel-side work in `board_late_initialize()` is finished
([KernelStartup.md](KernelStartup.md)). It is selected by `CONFIG_INIT_ENTRYPOINT="hcom_main"`, and
its source is `apps/system/hcom/hcom_startup_manager.c`.

`hcom_main()` does two jobs in sequence. First it initialises every HCOM module and starts the
helper threads, one step at a time and in a deliberate order. Then, instead of returning, it
**becomes the request-processing thread**: its last call is `hcom_host_process_setup()`, which loops
for the life of the device (see [RequestRouting.md](RequestRouting.md)). So the thread that does the
starting is the same thread that later runs `HcomProc`.

All the helper threads are pthreads created by `hcom_main()` or by modules it calls, so they share
`hcom_main`'s **task group**: the same file descriptors, which matters because a descriptor opened
by one of them is only valid in the group (see [SendPipeline.md](SendPipeline.md)). The Mono runtime
is the exception: it is a separate task, created last.

## The Init Task

| Setting | Value | Source |
|---------|-------|--------|
| Entry point | `hcom_main()` | `CONFIG_INIT_ENTRYPOINT` |
| Stack | 65,536 bytes | `CONFIG_INIT_STACKSIZE` in the `meadow_os` defconfig |
| Priority during start-up | 100 | `CONFIG_INIT_PRIORITY` (the default) |
| Priority while processing requests | 180 | `sched_setparam()` in `hcom_host_process_setup()` |

The thread starts at 100 and is raised to 180 only once start-up is complete, just before it also
unblocks `SIGALRM` and enters the loop. The macro `HCOM_THREAD_STACKSIZE_HCOM_PROCESS` in
`meadow_thread_config.h` refers to `CONFIG_USERMAIN_STACKSIZE`, an option that no longer exists in this
NuttX version, and is not used; the stack is the init task's.

## The Threads

| Thread | Priority | Stack | Created by | Purpose |
|--------|----------|-------|------------|---------|
| `hcom_main` / `HcomProc` | 100, then 180 | 65,536 | NuttX | Start-up, then request processing ([RequestRouting.md](RequestRouting.md)) |
| `HcomRecv` | 180 | 2048 | `hcom_host_recv_setup()` | Reads the USB port ([ReceivePipeline.md](ReceivePipeline.md)) |
| `Traverse` | 120 | 2048 | `hcom_traverse_ku_init_user_setup()` | Reads kernel trace and notices from `/dev/hcom_traverse_ku` and forwards them to the host ([TraceSyslog.md](TraceSyslog.md)) |
| `MonoStdxxx` | 120 | 2048 | `hcom_mono_stdxxx_read_setup()` | Forwards Mono's stdout and stderr ([MonoLifecycle.md](MonoLifecycle.md)) |
| `RemoteDbg` | 130 | 2048 | `hcom_mono_remote_dbg_setup()`, only when a debug session was requested | Debugger bridge ([MonoRemoteDebug.md](MonoRemoteDebug.md)) |
| `Mono` (a task, not a pthread) | 80 | `CONFIG_PTHREAD_STACK_DEFAULT` (65,536) | `hcom_mono_ctrl_start_mono_main()` | The Mono runtime, in its own task group |
| `MonoMonitor` | 100 | 2048 | `hcom_mono_ctrl_start_mono_main()` | Waits for Mono to exit, then resets the board |
| `nsh` (a task) | `CONFIG_SYSTEM_NSH_PRIORITY` | `CONFIG_SYSTEM_NSH_STACKSIZE` | `hcom_diag_misc_launch_nsh()`, only when NSH is built in | A shell on a spare serial port ([DeveloperCommandsNsh.md](DeveloperCommandsNsh.md)) |

Higher numbers are higher priority in NuttX. The ESP32 threads (`EspRecv`, the network monitor) are
also created during start-up but are not covered here. The download watchdog runs on the user-space
work queue (`CONFIG_LIBC_USRWORK`, 4096-byte stack), not on a thread of its own.

Names and priorities are defined in `F7MeadowOS/include/meadow/meadow_thread_config.h`, and the
threads are named with `pthread_setname_np()` so they can be recognised in the process list.

## The Start-Up Sequence

The normal path through `hcom_main()`, in order. "Setup" calls that do nothing are listed because
their position records an intended order, and several are placeholders.

| # | Step | What it does |
|---|------|--------------|
| 1 | Create `_startupWaitSem` | A signalling semaphore (count 0, `SEM_PRIO_NONE`) used to wait for helper threads, below |
| 2 | Copy the configuration (only with `CONFIG_HCOM_MONO_STDERR_STDOUT`) | Takes a deep copy of the OS configuration and sets `g_copy_application_output_to_uart` (copy Mono output to the syslog UART) from it |
| 3 | `hcom_via_nx_upd_setup()` | Opens `/dev/upd`, the descriptor used for every later call into the kernel |
| 4 | Board check | If the board is the unsupported F7 Feather V1, jump to the reduced start-up below |
| 5 | `hcom_diag_trace_priority_level_init()` | Restores the syslog mask from the battery-backed register and the configuration. Done early because later log calls are filtered by it |
| 6 | Log the heap | One line with the user heap's size and free space, so memory use can be compared between releases |
| 7 | `hcom_common_utils_setup()`, `hcom_diag_misc_setup()` | Do nothing (placeholders) |
| 8 | ESP32 network monitor, ESP32 reset | ESP32; not covered |
| 9 | `hcom_diag_nsh_support_setup()` | Sets a flag (only with NSH) |
| 10 | `hcom_misc_rqst_setup()` | Does nothing |
| 11 | File module setups | Initialise file-download state (`hcom_file_dnld_stm32f7_setup()`, `hcom_file_dnld_esp32_setup()`, `hcom_file_upld_proc_setup()`, `hcom_file_write_setup()`); no allocation, no threads |
| 12 | `hcom_host_enq_deq_setup()` | Allocates the receive circular buffer and creates its two semaphores |
| 13 | `hcom_host_route_setup()` | Sets one variable |
| 14 | `hcom_mono_stdxxx_read_setup()` | Creates the Mono output FIFOs and starts `MonoStdxxx`; **waits** until the thread reports that it is running |
| 15 | `hcom_mono_remote_dbg_setup()` | Initialises variables; starts `RemoteDbg` only if the "start a debug session" flag is set in the battery-backed register, which it then clears |
| 16 | ESP32 UART comms setup | ESP32; not covered |
| 17 | `hcom_host_send_setup()` | Allocates the encode buffer and registers for low-power notices. Must precede the receive setup so a reply can be sent as soon as a request arrives |
| 18 | `hcom_host_recv_setup()` | Starts `HcomRecv`; **waits** until it reports that it is running |
| 19 | `hcom_traverse_ku_init_user_setup()` | Opens the kernel-to-user FIFO and starts `Traverse`. Placed after the send setup because `Traverse` sends |
| 20 | `hcom_mono_ctrl_mono_main_setup()` | Does nothing (its comment says it configures the blue LED, but the function is empty) |
| 21 | Open the ESP32 events queue | ESP32; not covered |
| 22 | Start Mono (only with `CONFIG_SYSTEM_MONO`) | `hcom_mono_ctrl_start_mono_main()`, the "last stop": it needs everything above to be ready. Its result is not used |
| 23 | Start NSH (only with `CONFIG_SYSTEM_NSH`) | `hcom_diag_misc_launch_nsh(1)` |
| 24 | `hcom_host_process_setup()` | Raises the priority and runs the request loop. Does not return |

Two blocks are compiled out with `#if(0)`: the system updaters (`os_update()`, `app_update()`,
`firmware_update()`), and starting the cellular PPP daemon. The comments record that they caused
link errors after the NuttX rebase.

### The wait handshake

Steps 14 and 18 start a thread and must not continue until it is running, because later steps assume
it. They use `_startupWaitSem` and a small protocol:

1. `hcom_main` resets `_semaphoreRet` to `OK`, **before** creating the thread (so a fast thread cannot
   have its result overwritten).
2. The setup function creates the thread and returns.
3. `hcom_main` calls `hcom_startup_mgr_takesem()`, which waits on the semaphore and returns
   `_semaphoreRet`.
4. The new thread, early in its life, calls `hcom_startup_mgr_release_sem()` (success) or
   `hcom_startup_mgr_release_sem_err(ret)` (failure). `HcomRecv` releases at the top of its function;
   `MonoStdxxx` releases after it has created and opened its FIFOs, and reports errors from those.
5. A negative result makes `hcom_main` stop.

The other helper threads (`Traverse`, `RemoteDbg`) do not use the handshake.

### Reduced start-up for an unsupported board

For the F7 Feather V1, which this release does not support, `hcom_startup_mgr_unsupported_board()` runs
instead of steps 5 to 24. It performs only `hcom_host_enq_deq_setup()`, `hcom_host_route_setup()`,
`hcom_host_send_setup()` and `hcom_host_recv_setup()` (with the same wait), then
`hcom_host_process_setup()`, so it ends in the same request loop. The device therefore still talks HCOM, but answers every request with an
error ([RequestRouting.md](RequestRouting.md#command-packets)).

## What Happens When a Step Fails

Almost every step is written as `if (ret < 0) { syslog(LOG_CRIT, ...); return ret; }`. Returning from
`hcom_main()` ends the init task, and with it the request loop and the HCOM threads, so **a failure in
any of these steps leaves the device running (the kernel is fine) but with no host communication**:

| Step | On failure |
|------|-----------|
| Configuration copy (2), `/dev/upd` (3), trace level (5), each `...setup()` that returns an error | Fatal: `hcom_main` returns |
| `hcom_mono_stdxxx_read_setup()` or `hcom_host_recv_setup()` (14, 18), including the thread's own error report | Fatal |
| ESP32 network monitor, ESP32 events queue | Do not return a result; start-up continues |
| Starting Mono (22) | The result is discarded; start-up continues whether or not Mono started |
| Starting NSH (23) | Not checked |

Failures are reported only to syslog, with severity `LOG_CRIT`, so they are normally visible through
the kernel log and not to the host.

## Order Dependencies

The sequence is not arbitrary; these constraints are visible in the code and its comments:

- `/dev/upd` before anything that calls the kernel (steps 3 onward).
- The trace level before anything that logs, since the mask filters every later message.
- The send setup before the receive setup, and both before `Traverse`: all three can send to the host.
- The Mono output FIFOs and their thread before Mono starts, or the first output would have nowhere to go.
- Mono last, so it is not competing with start-up and finds every facility it relies on.
- The request loop last: until it starts, nothing the host sends is acted on.

## Shutdown

`hcom_manager_shutdown()` is defined and calls each module's shutdown function, but it is never called;
the comment says "At present this is never called". HCOM runs until the device is reset.

## Things to Know

- **A start-up failure silences HCOM completely.** There is no retry and no message to the host; the
  evidence is a `LOG_CRIT` line in syslog.
- **The stack is the init task's, not a HCOM setting.** Anything that wants more or less stack for
  request processing changes `CONFIG_INIT_STACKSIZE`. The comment in `hcom_host_process_setup()`
  about `CONFIG_USERMAIN_PRIORITY` and `CONFIG_USERMAIN_STACKSIZE` describes options that no longer
  exist.
- **Start-up runs at priority 100, below most of the helper threads it creates** (120 to 180), so each
  of them can preempt `hcom_main` as soon as it exists. The wait handshake at steps 14 and 18 is what
  makes the order dependable, and steps that must follow a thread's start-up wait for it explicitly.
- **Several "setup" calls are placeholders**, and the comment on `hcom_mono_ctrl_mono_main_setup()` is
  wrong about what it does.
- **The Mono start result is ignored**, so a failed start (missing files, version mismatch, a recorded
  earlier crash) leaves HCOM running normally with no Runtime. See [MonoLifecycle.md](MonoLifecycle.md).

## Source Map

| Concern | File |
|---------|------|
| The sequence, the wait handshake, the reduced start-up | `apps/system/hcom/hcom_startup_manager.c` |
| Thread names, priorities and stack sizes | `F7MeadowOS/include/meadow/meadow_thread_config.h` |
| Init task configuration | `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/configs/meadow_os/defconfig` (`CONFIG_INIT_*`) |
| The processing thread's own set-up | `apps/system/hcom/comms/hcom_host_preprocess.c` (`hcom_host_process_setup()`) |
| Optional-feature switches | `nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/Kconfig` |

## Related Documents

[KernelStartup.md](KernelStartup.md) (what runs before `hcom_main`), [ReceivePipeline.md](ReceivePipeline.md),
[SendPipeline.md](SendPipeline.md), [RequestRouting.md](RequestRouting.md),
[MonoLifecycle.md](MonoLifecycle.md), [Userspace.md](../Userspace.md).
