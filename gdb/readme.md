# GDB debugging

## Getting started

Start the OpenOCD server and GDB from the workspace root with the debug script (see
`Documentation/Debugging.md`):

```
./debug.sh --server     # terminal 1: OpenOCD GDB server on port 4242
./debug.sh              # terminal 2: arm-none-eabi-gdb in the gdb directory
```

`debug.sh` starts GDB in this directory, so GDB runs the `.gdbinit` here. That file loads the symbols,
the Python scripts in this directory and the upstream NuttX scripts (`nuttx/tools/pynuttx`), then
connects to OpenOCD.

## Manual configuration

These steps are only needed when GDB is not started by `debug.sh` from this directory.

* Load the ELF symbols for the kernel and user images (run GDB from this directory, or adjust the paths):

```
file -readnow ../nuttx/nuttx.elf
add-symbol-file -readnow ../nuttx/nuttx_user.elf
```

* Load the Python scripts.  `Nuttx.py` must come first as the other scripts use its helpers:

```
source Nuttx.py
source Nuttx_Tasks.py
source NuttxHeap.py
source Tracing.py
source Svd.py
source Meadow.py
source Mono.py
```

### Other helpful configuration scripts

* `set output-radix 16`

You can set this so GDB outputs integer data types as hexadecimal.
which is pretty helpful since that's the default representation
usually used for memory addresses and other constants in the
the memory map and linker scripts.

Set by default when using the `.gdbinit` init script.

* `set disassemble-next-line on`

If debugging using the `ni` or `si` assembly-level next/step
instructions, then this could be useful by telling GDB to print
the next assembly instruction on each successive step.

This is not set by default, use if working a lot with assembly.

## Commands

### Meadow commands

| Command | Script | Description |
|---------|--------|-------------|
| `nx_bt [pid] [nomanaged]` | `Nuttx.py` | Backtrace of the selected thread or of task `pid`, with annotations for exceptions, SVC calls and syscalls, and interpreted .NET frames expanded |
| `nx_dumpregs <addr>` | `Nuttx.py` | Decode a NuttX register save area (`tcb->xcp.regs`, or the `regs`/`context` argument of `arm_doirq` and `arm_svcall`) |
| `nx_dumpregs_hw <addr>` | `Nuttx.py` | Decode a hardware exception frame (r0-r3, r12, lr, pc, xpsr) stacked at `addr` |
| `nx_saveregs` / `nx_restoreregs` | `Nuttx.py` | Save and restore the CPU registers |
| `nx_dmesg` (also `dmesg`) | `Nuttx.py` | Print the RAM log (syslog) |
| `nx_tasks` | `Nuttx_Tasks.py` | List all tasks: state, priority, TCB, stack size and use, and what each task waits for |
| `nx_task <pid>` | `Nuttx_Tasks.py` | Details of one task, including open files and saved registers |
| `show alltcb` | `Nuttx_Tasks.py` | PID and name of every task |
| `show tcb <pid>` | `Nuttx_Tasks.py` | Print the `struct tcb_s` of a task |
| `show regs [pid]` | `Nuttx_Tasks.py` | Saved registers of one or all tasks and the source at the saved PC |
| `show interrupted-thread` | `Nuttx_Tasks.py` | Registers of the context interrupted by the current exception |
| `show check_stack` | `Nuttx_Tasks.py` | Stack regions and stack pointers of all tasks in address order |
| `show mybt <pid or name or tcb>` | `Nuttx_Tasks.py` | Heuristic backtrace: words on a task's stack that point into code |
| `show heap [user or kernel]` | `NuttxHeap.py` | List the nodes of a heap, with totals |
| `show freeheap [user or kernel]` | `NuttxHeap.py` | List the free nodes of a heap |
| `nx_check_heap` | `NuttxHeap.py` | Check the node chains of the kernel and user heaps |
| `trace_start`, `trace_stop`, `trace_add_backtrace`, `show trace_data` | `Tracing.py` | Record backtraces from breakpoint command lists |
| `trace_heap_start`, `trace_heap_stop`, `show heap_trace` | `Tracing.py` | Heap tracing, see below |
| `nx_alive [seconds]` | `Meadow.py` | Is the core running?  DHCSR state (halted, sleeping, lockup) and the system tick count across a short run (about 1000 per second is healthy) |
| `nx_reset` | `Meadow.py` | Reset reason (BK30R, the RCC_CSR flags the bootloader saved: 0x14 = reset pin + software), reset and power-cycle counts, the Meadow flags in BK31R and the fault logging status in BK29R |
| `nx_fault` | `Meadow.py` | Fault report: CFSR/HFSR/DFSR decoded, MMFAR/BFAR, vector catch state, the interrupted context and its backtrace |
| `nx_mpu` | `Meadow.py` | The MPU regions with size, access and attributes, named by the kernel/user sections and heaps they cover |
| `nx_irqs [all]` | `Meadow.py` | NVIC enabled/pending/active interrupts with priority, NuttX handler and name |
| `espcp_status` | `Meadow.py` | ESP32 link: mode, threads, queues and who waits on them, requests waiting for a response, error counters |
| `svd [PERIPHERAL [REGISTER]]` | `Svd.py` | Peripheral registers from `STM32F777.svd`, decoded field by field.  Data registers whose read has side effects (DR, RDR, RXDR, FIFO) are only read when named |
| `nx_jit <address>` | `Mono.py` | The managed method whose JIT code contains an address |
| `nx_jit_compile` | `Mono.py` | Which task is JIT-compiling which method, and the interpreter settings |

`nx_alive` resumes and halts the core.  `nx_mpu` writes MPU_RNR to select each region and restores
it.  Everything else only reads the target.

### Upstream pynuttx commands

`.gdbinit` also loads the upstream NuttX scripts (`nuttx/tools/pynuttx`).  Checked on Meadow.OS
2.999.53.6:

| Status | Commands |
|--------|----------|
| Work | `info nxthreads`, `deadlock`, `wdog`, `fdinfo`, `mount`, `foreach inode`, `hexdump`, `addr2line`, `mm heap`, `mm frag`, `mm free`, `mm dump` (allocation counts by size), `mm check` / `memcheck`, `mm map` / `memmap` (needs numpy and matplotlib from the `.venv` that `debug.sh` builds), `mm leak` / `memleak` (needs pyelftools from the `.venv`; about 3 minutes) |
| Partial | `irqinfo` (handlers only: counts need `SCHED_IRQMONITOR`, which needs PROCFS), `stack-usage` (current use only; use `nx_tasks` for the maximum), `mm dump` (no owner PIDs: they need `MM_BACKTRACE >= 0`) |
| Fail | `ps` (reads several macros, such as `_SIGSET_NELEM` and the TCB flag masks, that the build does not include; `nx_tasks` covers the same ground) |
| Not registered | `worker`, `netstats`, `netcheck`, `info shm` (pynuttx registers them from configuration macros that are not in the build) |

The build is compiled with `-g`, which leaves C macros out of the debug information; pynuttx reads
many values from macros.  `mm check` and `mm map` failed for that reason (`MM_MIN_CHUNK` missing, and
sequence numbers of -1 without `MM_BACKTRACE`); both are fixed in `nuttx/tools/pynuttx/nxgdb/mm.py` and
`memdump.py`.

`mm leak` reported thousands of false leaks, for three reasons, all fixed in `memleak.py` and `mm.py`:
it scanned only the user heap, but the task control blocks and task groups that point to task stacks,
task information and environments are in the kernel heap; it reported the marker nodes at the start
and end of every heap region; and it read each node from 4 bytes too early, so it missed a pointer in
the last word of an allocation.  Without `MM_BACKTRACE` it cannot say who allocated a reported node.

`./build.sh --clean --force --gdbdiag` turns on `STACK_COLORATION`: stack high-water marks ("Max" in
`nx_tasks`, "most used" in `nx_task`).  It costs a fill of each stack when a thread starts.  pynuttx
`stack-usage` still shows 0 for the maximum, because it detects the option from macro information that
the build does not include.

`MM_BACKTRACE` (the owner of every heap allocation) cannot be turned on in this protected build: the
user-space copy of the heap code then calls the kernel-only `nxsched_get_tcb()` and the user image
fails to link.  `show heap` prints the owner PID and sequence number if a build ever has them.

### Threads and the OpenOCD in use

OpenOCD's NuttX awareness shows each task as a GDB thread, so `thread N` followed by `bt` gives the
backtrace of any task.  This needs an OpenOCD built with the `FixGDBPythonScripts` change to
`src/rtos/nuttx.c` in `$MEADOW_OPENOCD_DIR`.  Without it (upstream OpenOCD included, as of November 2026)
OpenOCD reads `g_tcbinfo` with the layout used before NuttX 12, so the registers it reports for tasks that
are **not running** are wrong and `bt`, `thread N` and IDE call stacks show garbage for those tasks.

`nx_bt <pid>` works with either build: for a task that is not running it loads the context saved in the
task's TCB into the CPU registers, unwinds, and restores the registers afterwards.

### Stack traces

Stack traces can be obtained using the `bt` command.  Loading `Nuttx.py` registers a frame unwinder for
`dispatch_syscall`, the assembly trampoline that runs system calls.  Without it a backtrace of a task that
is inside a system call stops at `dispatch_syscall`; with it the backtrace continues into the user-space
code that made the call.

`nx_bt` adds the task name, annotations and (for the Mono interpreter) the managed methods being run:

```
(gdb) nx_bt 9
Task hcom_main (pid 9, tcb 0x20054188)
Not running; unwinding from the context saved in tcb->xcp.regs
#0   0x08157108 in nxsem_wait_slow () at semaphore/sem_wait.c:259
#1   0x08167342 in dispatch_syscall () at armv7-m/arm_svcall.c:84 [syscall dispatch]
#2   0x080e7fe4 in nxsem_wait_slow () at proxies/PROXY_nxsem_wait_slow.c:9
#3   0x0808b24e in sem_wait () at semaphore/sem_wait.c:92
#4   0x080dfb54 in hcom_host_enq_deq_wait_for_work () at comms/hcom_host_enq_deq.c:293
#5   0x080dfb54 in hcom_host_enq_deq_dequeue_packet () at comms/hcom_host_enq_deq.c:244
#6   0x080dfcec in hcom_host_process_run_loop () at comms/hcom_host_preprocess.c:476
#7   0x080e03e4 in hcom_host_process_setup () at comms/hcom_host_preprocess.c:199
#8   0x0808bd56 in hcom_main () at hcom_startup_manager.c:585
#9   0x08088484 in nxtask_startup () at sched/task_startup.c:72
#10  0x0815809a in nxtask_start () at task/task_start.c:106
#11  0x00000000 in ?? ()
```

Frames of `mono_interp_exec_method` are followed by the managed methods it is running, read from the
interpreter's `InterpFrame` chain, for example `[managed] System.Object::GetType`.  This needs the
interpreter (`--interp`).

JIT-compiled code has no symbols.  With `Mono.py` loaded, such frames are named from Mono's JIT info
table instead of `?? ()`, for example `0xc12c0b2a in [JIT] System.Threading.Monitor::Monitor_wait+0x59`.
GDB cannot unwind through JIT code, so the backtrace ends at the first JIT frame.

### Debugging a fault

A fault (for example a NULL pointer write in user code, which the MPU turns into a MemManage fault)
goes through `exception_common` like any interrupt.  Two ways to stop on it before NuttX panics and
resets the board:

* Vector catch (`.gdbinit` turns it on): the core halts on the fault entry.  `bt` shows
  `<signal handler called>` and then the faulting function and its callers.
* `hbreak arm_memfault` (or `arm_hardfault`; BusFault and UsageFault escalate to HardFault): `bt`
  shows the handler, `arm_doirq`, `exception_common`, `<signal handler called>`, then the faulting
  code.

In either case `nx_fault` prints the decoded cause (e.g. `MemManage DACCVIOL`, `MMFAR 0x00000000`),
the faulting PC and LR and the backtrace.  Checked on the board with a deliberate NULL write in
`hcom_main`: both methods name the faulting line and every caller up to `hcom_main`.

Kernel (privileged) code faults on NULL too, but differently: the MPU lets privileged code use the
default memory map, and address 0 is the ITCM RAM, which is disabled in this build
(`CONFIG_ARMV7M_ITCM` is off; ITCMCR at 0xE000EF90 reads 0x72, enable bit clear).  An access there is
a BusFault (CFSR PRECISERR, BFAR = the address), which escalates to HardFault because BusFault is
not enabled; measured with a read of address 4 from the idle task.  If ITCM is ever enabled, kernel
NULL accesses will silently read and write ITCM RAM instead; an access watchpoint over the low
addresses (`awatch *(char (*)[1024]) 0`) or an MPU no-access region at 0 would then be needed.

### Mapping from addresses to symbols

The `info line *<address>` command can be used to map from a
memory address to a source file/line.

```
(gdb) info line *0x800bf69
Line 83 of "common/up_task_start.c" starts at address 0x800bf68 <up_task_start+24> and ends at 0x800bf70 <sys_call4>.
```

### Registers

You can show the output of all registers under the current frame
using the `info reg` command (or `i r` for short).

```
(gdb) info reg
r0             0x20034a00          0x20034a00
r1             0x0                 0x0
r2             0x0                 0x0
r3             0x0                 0x0
r4             0xc0191890          0xc0191890
r5             0xc0191888          0xc0191888
r6             0xc018aa70          0xc018aa70
r7             0xc01918a8          0xc01918a8
r8             0xc0188f50          0xc0188f50
r9             0x0                 0x0
r10            0xc018a880          0xc018a880
r11            0xc0191b70          0xc0191b70
r12            0x8008b65           0x8008b65
sp             0xc0191878          0xc0191878
lr             0x8070fd9           0x8070fd9
pc             0x806a85e           0x806a85e <interp_exec_method_full+2542>
xpsr           0x61000000          0x61000000
msp            0xc01913a8          0xc01913a8
psp            0xc0191878          0xc0191878
control        0x3                 0x3
faultmask      0x0                 0x0
basepri        0x0                 0x0
primask        0x0                 0x0
fpscr          0x80000010          0x80000010
```

```
(gdb) i r sp pc lr
sp             0xc0191878          0xc0191878
pc             0x806a85e           0x806a85e <interp_exec_method_full+2542>
lr             0x8070fd9           0x8070fd9
```

### Breakpoints

There are multiple ways to set breakpoints, from using file 
paths and line numbers to explicit code addresses, which can
be done using the `b` command as shown below.

```
(gdb) b sched/pthread/pthread_condtimedwait.c:290
Breakpoint 1 at 0x8017f1c: file pthread/pthread_condtimedwait.c, line 290.
```

```
(gdb) b *0x8017f1c
Breakpoint 2 at 0x8017f1c: file pthread/pthread_condtimedwait.c, line 290.
```

### Disassembly

It can helpful to disassembly some piece of code, either using
a symbol or a memory address.

GDB provides a couple of commands to do this.

The first is the `disass <symbol>` command:

```
(gdb) disass up_pthread_start
Dump of assembler code for function up_pthread_start:
   0x0801da44 <+0>:	push	{r7, lr}
   0x0801da46 <+2>:	sub	sp, #8
   0x0801da48 <+4>:	add	r7, sp, #0
   0x0801da4a <+6>:	str	r0, [r7, #4]
   0x0801da4c <+8>:	str	r1, [r7, #0]
   0x0801da4e <+10>:	ldr	r3, [r7, #4]
   0x0801da50 <+12>:	ldr	r2, [r7, #0]
   0x0801da52 <+14>:	mov	r1, r3
   0x0801da54 <+16>:	movs	r0, #5
   0x0801da56 <+18>:	bl	0x801da1c <sys_call2>
   0x0801da5a <+22>:	nop
   0x0801da5c <+24>:	adds	r7, #8
   0x0801da5e <+26>:	mov	sp, r7
   0x0801da60 <+28>:	pop	{r7, pc}
End of assembler dump.
```

If you prefer to disassembly code from an address then use `x/<size>i`:

```
(gdb) x/10i 0x0801da44
   0x801da44 <up_pthread_start>:	push	{r7, lr}
   0x801da46 <up_pthread_start+2>:	sub	sp, #8
   0x801da48 <up_pthread_start+4>:	add	r7, sp, #0
   0x801da4a <up_pthread_start+6>:	str	r0, [r7, #4]
   0x801da4c <up_pthread_start+8>:	str	r1, [r7, #0]
   0x801da4e <up_pthread_start+10>:	ldr	r3, [r7, #4]
   0x801da50 <up_pthread_start+12>:	ldr	r2, [r7, #0]
   0x801da52 <up_pthread_start+14>:	mov	r1, r3
   0x801da54 <up_pthread_start+16>:	movs	r0, #5
   0x801da56 <up_pthread_start+18>:	bl	0x801da1c <sys_call2>
```

```
(gdb) x/10i $pc
=> 0x806a85e <interp_exec_method_full+2542>:	ldr.w	r1, [r7, #332]	; 0x14c
   0x806a862 <interp_exec_method_full+2546>:	cbz	r1, 0x806a8cc <interp_exec_method_full+2652>
   0x806a864 <interp_exec_method_full+2548>:	str	r3, [sp, #0]
   0x806a866 <interp_exec_method_full+2550>:	mov	r2, r11
   0x806a868 <interp_exec_method_full+2552>:	subs	r3, r6, #4
   0x806a86a <interp_exec_method_full+2554>:	add.w	r0, r8, #8
   0x806a86e <interp_exec_method_full+2558>:	bl	0x8069780 <interp_throw>
   0x806a872 <interp_exec_method_full+2562>:	ldr.w	r3, [r8, #12]
   0x806a876 <interp_exec_method_full+2566>:	cmp	r3, r11
   0x806a878 <interp_exec_method_full+2568>:	bne.w	0x806aaac <interp_exec_method_full+3132>
```

## Heap Tracing

Heap tracing is implemented in `Tracing.py`.  It records every `mm_malloc` and `mm_free` call, keeping the
allocations that have not been freed together with the backtrace of the code that made them.  The target
stops on every allocation and free, so it runs very slowly while tracing is on.

For each allocation the address returned, the size requested and the heap are recorded.  A free of a
recorded address removes the allocation; a free of an address that was not recorded is listed separately.

### Starting and stopping a trace

```
trace_heap_start [user|kernel|all] [max_events]
continue
```

`trace_heap_start` clears the recorded data and sets two internal breakpoints, on `mm_malloc` and
`mm_free` (in both the kernel and the user image, so four hardware breakpoints out of eight).  The
breakpoints record the data and continue automatically.  With `max_events` the target stops once that many
allocations and frees have been recorded; otherwise interrupt it with Ctrl-C.  `trace_heap_stop` removes
the breakpoints and keeps the data.  The breakpoints are on function entry, so there is no need to rebuild
`mm_malloc.c` without optimisation or to edit line numbers when the heap code changes.

`source SetupHeapTracing.src` runs `trace_heap_start` for all heaps.

To record the heap operations of one piece of code, stop at the start of it, run `trace_heap_start`, set
a breakpoint at the end of it and continue.  At the end, run `trace_heap_stop` and `show heap_trace`.

Allocations made by `mm_memalign` and in-place `mm_realloc` are not seen, so their later frees are listed
as frees of unknown addresses.

### Displaying the Heap Tracing Recording

The information recorded is shown with the command `show heap_trace`.  This shows two types of information:
* Memory allocated but not released
* Memory released but no allocation has been recorded

Unreleased heap allocations are shown with a partial backtrace; the calls to `malloc` and similar are not
shown.  This shows the backtrace from the main point of interest i.e. the user or OS code:

```
Memory allocation 0xc0b0dee0, requested 36 from user heap
    #2   0x08186802 in meadow_os_deep_copy_config () at F7MeadowOSLink/meadow_os/meadow_os_config.c:175
    #3   0x08167342 in dispatch_syscall () at armv7-m/arm_svcall.c:84 [syscall dispatch]
    #4   0x080e7e82 in meadow_os_deep_copy_config () at proxies/PROXY_meadow_os_deep_copy_config.c:11
    #5   0x0808d61a in hcom_misc_rqst_get_device_info () at nx_rqsts/hcom_misc_requests.c:100
    #6   0x080e0b2e in hcom_host_route_request_by_cmd_type () at comms/hcom_host_route.c:208
```

The release of unknown addresses can occur as a result of:
* Memory allocated before the trace started
* Allocations that are not seen (see above)
* Application errors

Free operations from unknown (unrecorded) addresses are shown as follows:

```
Free from unknown addresses:
    Address: 0x2007ff70 on user heap
    Address: 0x2007fcd0 on user heap
```

It is possible that the output from the heap tracing operation can be extensive.  It may be desirable to
send the data to a log file (see below).

### Backtrace tracing

`trace_start` clears the recorded data and enables tracing, `trace_add_backtrace` (for use in a
breakpoint's command list) records the backtrace of the current thread, and `show trace_data` prints the
recorded backtraces.

## Recording Session to a Log File

`GDB` sessions can be recorded to a log file and this presents a useful way of capturing sessions or large amounts of output
for future analysis.  The following commands allow logging to be enabled / disabled:
* `set logging file filename` - Set the name of the file to be used to capture output
* `set logging enabled [on | off]` - Turn logging on or off

## Troubleshooting

* `info files`

This will show how the sections of the ELF executables are mapped
into memory. You can use this to troubleshoot weird issues like
breakpoints suddently not hitting.

```
(gdb) info files
Symbols from "WildernessLabs/Meadow/nuttx/nuttx".
Remote serial target in gdb-specific protocol:
Debugging a target over a serial line.
	While running this, GDB does not access memory from...
Local exec file:
	`WildernessLabs/Meadow/nuttx/nuttx', file type elf32-littlearm.
	Entry point: 0x8000310
	0x08000000 - 0x08020938 is .text
	0x08020938 - 0x08020940 is .ARM.exidx
	0x20020000 - 0x200201a6 is .data
	0x200201a8 - 0x20022864 is .bss
	0x08040000 - 0x08040030 is .userspace in WildernessLabs/Meadow/nuttx/configs/stm32f777zit6-meadow/kernel/nuttx_user.elf
	0x08040030 - 0x081a32a8 is .text in WildernessLabs/Meadow/nuttx/configs/stm32f777zit6-meadow/kernel/nuttx_user.elf
	0x081a32a8 - 0x081a32b0 is .ARM.exidx in WildernessLabs/Meadow/nuttx/configs/stm32f777zit6-meadow/kernel/nuttx_user.elf
	0x20030000 - 0x200316a0 is .data in WildernessLabs/Meadow/nuttx/configs/stm32f777zit6-meadow/kernel/nuttx_user.elf
	0x200316c0 - 0x2003cf30 is .bss in WildernessLabs/Meadow/nuttx/configs/stm32f777zit6-meadow/kernel/nuttx_user.elf
```

* `mov r0, r0`

If you start seeing weird output like repeated `movs	r0, r0` instructions  while disassemble code,
then reset the board and restart the debugging session.

```
(gdb) disass __start
Dump of assembler code for function __start:
=> 0x08000310 <+0>:	movs	r0, r0
   0x08000312 <+2>:	movs	r0, r0
   0x08000314 <+4>:	movs	r0, r0
   0x08000316 <+6>:	movs	r0, r0
```
 No newline at end of file
