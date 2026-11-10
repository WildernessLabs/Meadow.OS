#
#   GDB helpers for Meadow.OS (NuttX 12, armv7-m, protected build).
#
#   Commands:
#
#       nx_bt [full]            Backtrace of the selected thread, annotated with
#                               exception, SVC and syscall information and with
#                               interpreted (Mono) frames expanded.
#       nx_dumpregs <addr>      Decode a NuttX register save area (for example
#                               tcb->xcp.regs or the context argument of
#                               arm_svcall / arm_doirq).
#       nx_dumpregs_hw <addr>   Decode a hardware exception frame (r0-r3, r12,
#                               lr, pc, xpsr) stacked at <addr>.
#       nx_saveregs             Save the CPU registers.
#       nx_restoreregs          Restore the registers saved by nx_saveregs.
#
#   Loading this file also registers a frame unwinder for dispatch_syscall.
#   dispatch_syscall is a naked assembly trampoline without unwind
#   information, so without the unwinder GDB stops the backtrace there and
#   never reaches the user-space caller of a system call.  With it the plain
#   GDB "bt" (and IDE call stacks) cross from the kernel back into user space.
#
#   The helpers in this file (nx_*) are also used by Nuttx_Tasks.py and
#   Tracing.py, so source this file first.
#
import gdb

try:
    from gdb.unwinder import Unwinder, register_unwinder
    try:
        from gdb.unwinder import FrameId
    except ImportError:
        class FrameId(object):
            def __init__(self, sp, pc):
                self.sp = sp
                self.pc = pc
except ImportError:
    Unwinder = None

verbose = False
is_qemu = False

#
#   Registers in the order used by g_reg_offs (arch/arm/src/armv7-m/arm_tcbinfo.c),
#   which is also the order of the GDB org.gnu.gdb.arm.m-profile feature.
#
NX_CORE_REGS = ["r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9",
                "r10", "r11", "r12", "sp", "lr", "pc", "xpsr"]

#
#   Number of syscall values reserved for the architecture before the first
#   NuttX syscall (arch/arm/include/syscall.h, non-flat builds).
#
NX_SYS_RESERVED = 8

#
#   Register save area details not covered by g_reg_offs
#   (arch/arm/include/armv7-m/irq.h and exc_return.h).
#
NX_REG_EXC_RETURN = 11
EXC_RETURN_STD_CONTEXT = 0x10
NX_HW_FRAME_STANDARD = 8 * 4
NX_HW_FRAME_EXTENDED = (8 + 18) * 4

NX_SVCALL_NAMES = {
    0: "SYS_syscall",
    1: "SYS_restore_context",
    2: "SYS_switch_context",
    3: "SYS_syscall_return",
    4: "SYS_task_start",
    5: "SYS_pthread_start",
    6: "SYS_signal_handler",
    7: "SYS_signal_handler_return",
}

NX_EXCEPTION_NAMES = {
    0: "thread mode",
    1: "reset",
    2: "NMI",
    3: "HardFault",
    4: "MemManage",
    5: "BusFault",
    6: "UsageFault",
    11: "SVCall",
    12: "DebugMonitor",
    14: "PendSV",
    15: "SysTick",
}

###########################################################################
#
#   Memory and symbol helpers.
#
###########################################################################

def nx_reg(frame, name):
    '''
    A register of a frame as an unsigned 32-bit integer.  GDB gives r0-r12 a
    signed type, so an address such as 0xc0318010 (SDRAM) would otherwise come
    back negative.
    '''
    return int(frame.read_register(name)) & 0xffffffff

def nx_read_u32(address):
    '''Read a little endian 32-bit word from target memory.'''
    memory = gdb.selected_inferior().read_memory(int(address) & 0xffffffff, 4)
    return int.from_bytes(bytes(memory), "little")

def nx_symbol_value(name):
    '''
    Value of a global or file-static variable, or None.

    Unlike parse_and_eval this does not need the selected frame, so it can be used
    inside a frame unwinder.
    '''
    symbol = gdb.lookup_global_symbol(name)
    if symbol is None:
        symbol = gdb.lookup_static_symbol(name)
    if symbol is None:
        return None
    return symbol.value()

def nx_eval(expression):
    '''Evaluate an expression, returning None if it cannot be evaluated.'''
    try:
        return gdb.parse_and_eval(expression)
    except gdb.error:
        return None

def nx_symbol_name(address):
    '''Name of the symbol containing address, or None.'''
    try:
        block = gdb.block_for_pc(int(address))
    except RuntimeError:
        block = None
    #
    #   Take the outermost function: an inlined function has a block of its own
    #   inside the block of the function it was inlined into.
    #
    function = None
    while block is not None:
        if block.function is not None:
            function = block.function
        block = block.superblock
    if function is not None:
        return function.print_name
    text = gdb.execute("info symbol 0x%x" % int(address), to_string=True)
    if text.startswith("No symbol"):
        return None
    return text.split(" in section")[0].split(" + ")[0].strip()

def nx_exception_name(ipsr):
    '''Printable name for an exception number (xPSR bits 0-8).'''
    if ipsr in NX_EXCEPTION_NAMES:
        return NX_EXCEPTION_NAMES[ipsr]
    if ipsr >= 16:
        return "IRQ %d" % (ipsr - 16)
    return "reserved exception %d" % ipsr

###########################################################################
#
#   Task control blocks.
#
###########################################################################

def nx_tcb_pointer_type():
    return gdb.lookup_type("struct tcb_s").pointer()

def nx_tcbs():
    '''List of all TCB pointers (gdb.Value) in g_pidhash.'''
    pidhash = nx_symbol_value("g_pidhash")
    count = int(nx_symbol_value("g_npidhash"))
    result = []
    if int(pidhash) == 0:
        return result
    for index in range(count):
        tcb = pidhash[index]
        if int(tcb) != 0:
            result.append(tcb)
    return result

def nx_readytorun_tcb():
    '''Head of g_readytorun: the task the scheduler has selected to run.'''
    head = nx_symbol_value("g_readytorun")["head"]
    return head.cast(nx_tcb_pointer_type())

def nx_running_tcb():
    '''
    TCB of the task whose context is in the CPU registers.

    This is g_running_tasks[0].  It differs from the head of g_readytorun while an
    exception that switches tasks is being handled: the scheduler has already
    selected the next task, but the CPU still holds the interrupted one.
    '''
    running = nx_symbol_value("g_running_tasks")
    if running is not None:
        tcb = running[0]
        if int(tcb) != 0:
            return tcb
    return nx_readytorun_tcb()

def nx_tcb_for_thread(thread=None):
    '''
    TCB for a GDB thread.

    OpenOCD's NuttX awareness uses the TCB address as the thread id, and gives the
    CPU registers to the thread at the head of g_readytorun; for that thread the
    task actually on the CPU is returned.  When there is no thread list (no RTOS
    awareness, QEMU) the task on the CPU is used.
    '''
    if (thread is None) and (_nx_context_tcb is not None):
        return _nx_context_tcb
    if thread is None:
        thread = gdb.selected_thread()
    if thread is not None:
        candidates = [value for value in thread.ptid[1:] if value]
        for tcb in nx_tcbs():
            if int(tcb) in candidates:
                if int(tcb) == int(nx_readytorun_tcb()):
                    return nx_running_tcb()
                return tcb
    return nx_running_tcb()

def nx_tcb_for_pid(pid):
    '''TCB for a PID, or None.'''
    for tcb in nx_tcbs():
        if int(tcb.dereference()["pid"]) == pid:
            return tcb
    return None

def nx_gdb_thread_for_tcb(tcb):
    '''The GDB thread whose id is the TCB address, or None.'''
    for thread in gdb.selected_inferior().threads():
        if int(tcb) in thread.ptid[1:]:
            return thread
    return None

#
#   TCB whose saved context is currently loaded into the CPU registers by
#   nx_with_task_context, or None.
#
_nx_context_tcb = None

def nx_with_task_context(tcb, function):
    '''
    Call function() with the CPU registers holding the saved context of a task.

    OpenOCD's NuttX awareness cannot always be relied on for the registers of tasks
    that are not running (builds without the FixGDBPythonScripts fix read g_tcbinfo
    with the pre-NuttX 12 layout), so the context is taken from tcb->xcp.regs.  The registers
    of the running thread are overwritten while function runs and are always
    restored afterwards.  For the running task function is called directly.
    '''
    global _nx_context_tcb
    running = nx_running_tcb()
    original_thread = gdb.selected_thread()
    #
    #   OpenOCD gives the CPU registers to the thread of the g_readytorun head.
    #
    running_thread = nx_gdb_thread_for_tcb(nx_readytorun_tcb())
    if int(tcb) == int(running):
        if running_thread is not None:
            running_thread.switch()
        try:
            return function()
        finally:
            if original_thread is not None:
                original_thread.switch()
    regs = int(tcb.dereference()["xcp"]["regs"])
    if regs == 0:
        raise gdb.GdbError("Task %d has no saved register context." % int(tcb.dereference()["pid"]))
    saved = nx_read_saved_regs(regs)
    if running_thread is not None:
        running_thread.switch()
    gdb.newest_frame().select()
    names = [name for name in NX_CORE_REGS if name != "xpsr"]
    original = {}
    for name in names:
        original[name] = nx_reg(gdb.newest_frame(), name)
    #
    #   Set the TCB before writing the registers: GDB unwinds while the registers
    #   are being written and the dispatch_syscall unwinder must already see the
    #   syscall records of this task.
    #
    _nx_context_tcb = tcb
    try:
        for name in names:
            gdb.execute("set $%s = %d" % (name, saved[name]), to_string=True)
        return function()
    finally:
        _nx_context_tcb = None
        gdb.newest_frame().select()
        for name in names:
            gdb.execute("set $%s = %d" % (name, original[name]), to_string=True)
        if original_thread is not None:
            original_thread.switch()

def nx_in_exception():
    '''Exception number from IPSR when the CPU halted in handler mode, otherwise 0.'''
    try:
        return int(gdb.newest_frame().read_register("xpsr")) & 0x1ff
    except (gdb.error, ValueError):
        return 0

###########################################################################
#
#   Register save areas.
#
###########################################################################

def nx_reg_offsets():
    '''
    Byte offsets of r0-r12, sp, lr, pc and xpsr in a NuttX register save area.

    The offsets are read from g_reg_offs so that they follow the NuttX build.  If
    the symbol is not available the armv7-m layout with the FPU enabled is used
    (see arch/arm/include/armv7-m/irq.h).
    '''
    offsets = nx_symbol_value("g_reg_offs")
    if offsets is not None:
        return dict((name, int(offsets[index])) for index, name in enumerate(NX_CORE_REGS))
    sw_xcpt_regs = 12 + 16
    index = {"sp": 0, "r4": 2, "r5": 3, "r6": 4, "r7": 5, "r8": 6, "r9": 7,
             "r10": 8, "r11": 9, "r0": sw_xcpt_regs + 0, "r1": sw_xcpt_regs + 1,
             "r2": sw_xcpt_regs + 2, "r3": sw_xcpt_regs + 3, "r12": sw_xcpt_regs + 4,
             "lr": sw_xcpt_regs + 5, "pc": sw_xcpt_regs + 6, "xpsr": sw_xcpt_regs + 7}
    return dict((name, value * 4) for name, value in index.items())

def nx_read_saved_regs(regs_address):
    '''
    Read the core registers from a NuttX register save area.

    The saved SP is the value before the hardware stacked the exception frame,
    ignoring the alignment word the hardware adds when xPSR bit 9 is set; this is
    corrected here in the same way as OpenOCD does.
    '''
    regs_address = int(regs_address)
    offsets = nx_reg_offsets()
    values = {}
    for name, offset in offsets.items():
        values[name] = nx_read_u32(regs_address + offset)
    #
    #   exception_common stores the SP assuming an extended (FPU) hardware frame.
    #   When EXC_RETURN shows a standard frame, compute the SP from the frame size.
    #
    exc_return = nx_read_u32(regs_address + NX_REG_EXC_RETURN * 4)
    if (exc_return & 0xffffff00) == 0xffffff00:
        hardware_frame = regs_address + offsets["r0"]
        if exc_return & EXC_RETURN_STD_CONTEXT:
            values["sp"] = hardware_frame + NX_HW_FRAME_STANDARD
        else:
            values["sp"] = hardware_frame + NX_HW_FRAME_EXTENDED
    if values["xpsr"] & (1 << 9):
        values["sp"] += 4
    return values

def nx_read_hardware_frame(frame_address, exc_return, current):
    '''
    Registers from a hardware exception frame (r0-r3, r12, lr, pc, xpsr) stacked at
    frame_address.  r4-r11 are not stacked by the hardware, so they come from
    current (the registers at the halt).
    '''
    names = ["r0", "r1", "r2", "r3", "r12", "lr", "pc", "xpsr"]
    values = dict(current)
    for index, name in enumerate(names):
        values[name] = nx_read_u32(frame_address + 4 * index)
    if exc_return & EXC_RETURN_STD_CONTEXT:
        values["sp"] = frame_address + NX_HW_FRAME_STANDARD
    else:
        values["sp"] = frame_address + NX_HW_FRAME_EXTENDED
    if values["xpsr"] & (1 << 9):
        values["sp"] += 4
    return values

def nx_interrupted_context():
    '''
    The context interrupted by the current exception, or None when the CPU is not
    in handler mode.  Returns a dictionary with "source" (text), "address" and
    "regs" (register values).

    In order of preference:
    - tcb->xcp.regs of g_running_tasks[0], which arm_doirq sets;
    - the pointer exception_common keeps in r11, before arm_doirq has set it;
    - the hardware exception frame, when the core stopped on the first instruction
      of exception_common (the exception entry, e.g. a vector catch) before NuttX
      saved anything.
    Inside the rest of exception_common's prologue the context cannot be found.
    '''
    ipsr = nx_in_exception()
    if ipsr == 0:
        return None
    running = nx_running_tcb()
    regs = int(running.dereference()["xcp"]["regs"])
    if regs != 0:
        return {"source": "tcb->xcp.regs of %s" % running.dereference()["name"].string(),
                "address": regs, "regs": nx_read_saved_regs(regs)}
    newest = gdb.newest_frame()
    layout = nx_exception_layout(newest.architecture())
    pc = nx_reg(newest, "pc")
    if layout.valid() and (pc == layout.start):
        #
        #   Stopped on the exception entry (the vectors point at exception_common):
        #   only the hardware has saved anything so far.
        #
        current = dict((name, nx_reg(newest, name)) for name in NX_CORE_REGS)
        exc_return = current["lr"]
        if (exc_return & 0xffffff00) != 0xffffff00:
            return None
        stack = "psp" if (exc_return & 0x4) else "msp"
        frame_address = nx_reg(newest, stack)
        return {"source": "hardware frame on %s (exception entry)" % stack.upper(),
                "address": frame_address,
                "regs": nx_read_hardware_frame(frame_address, exc_return, current)}
    #
    #   r11 holds the pointer only while exception_common is waiting for arm_doirq to
    #   return, i.e. when it is an outer frame stopped at the return address.
    #
    frame = newest
    while frame is not None:
        if layout.valid() and (frame.pc() == layout.after_call) and (frame.name() == "exception_common"):
            regs = nx_reg(frame, "r11")
            if regs != 0:
                return {"source": "exception_common (r11)", "address": regs,
                        "regs": nx_read_saved_regs(regs)}
            break
        try:
            frame = frame.older()
        except gdb.error:
            frame = None
    return None

def nx_print_regs(values, names):
    for name in names:
        if name in values:
            print("%-6s 0x%08x" % (name, values[name] & 0xffffffff))

###########################################################################
#
#   dispatch_syscall unwinder.
#
###########################################################################

class NuttxSyscallLayout(object):
    '''
    Instruction addresses inside dispatch_syscall that change how its frame is read.

    dispatch_syscall (arch/arm/src/armv7-m/arm_svcall.c) builds a frame holding r4-r6,
    the LR of the code that issued the SVC and (orig_SP - new_SP), calls the stub and
    then removes the frame again.
    '''

    def __init__(self, architecture):
        self.start = None
        self.end = None
        self.after_sub_sp = None
        self.after_str_ip = None
        self.add_sp = None
        symbol = gdb.lookup_static_symbol("dispatch_syscall")
        if symbol is None:
            symbol = gdb.lookup_global_symbol("dispatch_syscall")
        if symbol is None:
            return
        block = gdb.block_for_pc(int(symbol.value().address))
        self.start = block.start
        self.end = block.end
        previous = None
        for instruction in architecture.disassemble(self.start, self.end - 1):
            text = self.normalise(instruction["asm"])
            address = instruction["addr"]
            if previous is not None:
                if previous == "sub sp, sp, r12":
                    self.after_sub_sp = address
                if previous == "str r12, [sp, #16]":
                    self.after_str_ip = address
            if text in ("add sp, r2", "add sp, sp, r2"):
                self.add_sp = address
            previous = text

    @staticmethod
    def normalise(text):
        '''
        Disassembly text with comments, width suffixes and the ip alias removed, so
        that "sub.w sp, sp, ip" and "sub sp, sp, r12" compare equal.
        '''
        text = text.split("@")[0].split(";")[0]
        words = text.split()
        if not words:
            return ""
        mnemonic = words[0].split(".")[0]
        operands = " ".join(words[1:]).replace("ip", "r12")
        return ("%s %s" % (mnemonic, operands)).strip()

    def valid(self):
        return None not in (self.start, self.after_sub_sp, self.after_str_ip, self.add_sp)

    def contains(self, pc):
        #
        #   pc == end is the return address of the svc that ends dispatch_syscall,
        #   seen in the hardware frame when the SYS_syscall_return SVC is taken.
        #
        return (self.start is not None) and (self.start <= pc <= self.end)

_nx_syscall_layout = None

def nx_syscall_layout(architecture=None):
    '''
    Layout of dispatch_syscall.  The first call must pass an architecture; inside an
    unwinder use the pending frame's, as newest_frame() cannot be used there.
    '''
    global _nx_syscall_layout
    if _nx_syscall_layout is None:
        if architecture is None:
            architecture = gdb.newest_frame().architecture()
        _nx_syscall_layout = NuttxSyscallLayout(architecture)
    return _nx_syscall_layout

def nx_syscall_caller(pc, sp, lr, ip, tcb, depth):
    '''
    Registers of the code that issued a system call, given the registers of a frame
    executing dispatch_syscall.

    depth is the number of dispatch_syscall frames newer than this one (nested
    system calls).  Returns (pc, sp, lr) or None.
    '''
    layout = nx_syscall_layout()
    if not layout.valid():
        return None
    if (pc < layout.after_sub_sp) or (pc > layout.add_sp):
        caller_sp = sp
        caller_lr = lr
    elif pc < layout.after_str_ip:
        caller_sp = sp + ip
        caller_lr = lr
    else:
        caller_sp = sp + nx_read_u32(sp + 16)
        caller_lr = nx_read_u32(sp + 12)
    xcp = tcb.dereference()["xcp"]
    index = int(xcp["nsyscalls"]) - 1 - depth
    if index < 0:
        return None
    caller_pc = int(xcp["syscall"][index]["sysreturn"])
    return (caller_pc, caller_sp, caller_lr)

class NuttxExceptionLayout(object):
    '''
    Return address of the call to arm_doirq in exception_common
    (arch/arm/src/armv7-m/arm_exception.S).

    exception_common saves the interrupted context and passes a pointer to it to
    arm_doirq, keeping a copy in r11 (fp) across the call.  Its CFI names r4 as the
    frame base instead, so GDB's own unwinder cannot get past it.
    '''

    def __init__(self, architecture):
        self.start = None
        self.end = None
        self.after_call = None
        symbol = gdb.lookup_global_symbol("exception_common")
        if symbol is None:
            symbol = gdb.lookup_static_symbol("exception_common")
        if symbol is None:
            return
        address = int(symbol.value().address)
        block = gdb.block_for_pc(address)
        if (block is not None) and (block.start == address):
            self.start = block.start
            self.end = block.end
        else:
            self.start = address
            self.end = address + 0x100
        previous = None
        for instruction in architecture.disassemble(self.start, self.end - 1):
            if (previous is not None) and previous.startswith("bl ") and ("arm_doirq" in previous):
                self.after_call = instruction["addr"]
                break
            previous = NuttxSyscallLayout.normalise(instruction["asm"])

    def valid(self):
        return self.after_call is not None

_nx_exception_layout = None

def nx_exception_layout(architecture):
    global _nx_exception_layout
    if _nx_exception_layout is None:
        _nx_exception_layout = NuttxExceptionLayout(architecture)
    return _nx_exception_layout

if Unwinder is not None:
    class NuttxUnwinder(Unwinder):
        '''
        Unwind the NuttX assembly frames that GDB cannot unwind by itself:

        - dispatch_syscall, back to the code that issued the system call;
        - exception_common, back to the code the exception interrupted.
        '''

        def __init__(self):
            super(NuttxUnwinder, self).__init__("nuttx")
            self._depths = {}

        def invalidate(self, *args):
            self._depths = {}

        def __call__(self, pending_frame):
            #
            #   GDB always unwinds from frame 0 after the frame cache is flushed
            #   (stop, register write, thread switch), so a frame 0 starts a new
            #   chain.  Breakpoints that continue automatically do not raise a
            #   stop event, so the events alone are not enough.
            #
            if pending_frame.level() == 0:
                self._depths = {}
            try:
                pc = nx_reg(pending_frame, "pc")
                architecture = pending_frame.architecture()
                if nx_syscall_layout(architecture).contains(pc):
                    return self.unwind_syscall(pending_frame, pc)
                exception = nx_exception_layout(architecture)
                if exception.valid() and (pc == exception.after_call):
                    return self.unwind_exception(pending_frame, exception)
            except (gdb.error, gdb.MemoryError, RuntimeError, ValueError) as e:
                if verbose:
                    print("nuttx unwinder: %s" % e)
            return None

        def unwind_syscall(self, pending_frame, pc):
            layout = nx_syscall_layout()
            sp = nx_reg(pending_frame, "sp")
            lr = nx_reg(pending_frame, "lr")
            ip = nx_reg(pending_frame, "r12")
            tcb = nx_tcb_for_thread()
            level = pending_frame.level()
            key = int(tcb)
            newer = [l for l in self._depths.get(key, {}) if l < level]
            depth = len(newer)
            caller = nx_syscall_caller(pc, sp, lr, ip, tcb, depth)
            if caller is None:
                return None
            self._depths.setdefault(key, {})[level] = depth
            caller_pc, caller_sp, caller_lr = caller
            #
            #   dispatch_syscall does not change the other registers that matter
            #   to the caller (r4-r11 are only copied to the stack), so they are
            #   passed through.
            #
            return self.unwind_info(pending_frame, layout.start,
                                    {"pc": caller_pc, "sp": caller_sp, "lr": caller_lr})

        def unwind_exception(self, pending_frame, layout):
            '''
            Unwind exception_common to the state at exception entry: PC = EXC_RETURN
            and the stack pointer at the hardware exception frame, with r4-r11 from
            the NuttX register save area.  GDB's own Cortex-M exception unwinder then
            takes the interrupted registers from the hardware frame.

            Handing GDB the interrupted PC directly would be wrong for faults: GDB
            treats the PC of every outer frame as a return address and looks up the
            instruction before it, which for a fault is in the code before the
            faulting instruction (often a different function).  GDB only skips that
            adjustment below its own exception frames.
            '''
            regs = nx_reg(pending_frame, "r11")
            if regs == 0:
                return None
            exc_return = nx_read_u32(regs + NX_REG_EXC_RETURN * 4)
            if (exc_return & 0xffffff00) != 0xffffff00:
                return None
            saved = nx_read_saved_regs(regs)
            hardware_frame = regs + nx_reg_offsets()["r0"]
            values = {"pc": exc_return, "lr": exc_return, "sp": hardware_frame}
            for name in ("r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11"):
                values[name] = saved[name]
            values["psp" if (exc_return & 0x4) else "msp"] = hardware_frame
            return self.unwind_info(pending_frame, layout.start, values)

        def unwind_info(self, pending_frame, function_start, values):
            '''
            Unwind information giving the caller the registers in values; the other
            general registers keep their value.  GDB needs xpsr (Thumb state) and
            consistent stack pointer aliases (msp/psp) to carry on unwinding.
            '''
            sp = nx_reg(pending_frame, "sp")
            values = dict(values)
            for alias in ("msp", "psp"):
                if alias in values:
                    continue
                try:
                    if nx_reg(pending_frame, alias) == sp:
                        values[alias] = values["sp"]
                except (gdb.error, ValueError):
                    pass
            info = pending_frame.create_unwind_info(FrameId(values["sp"], function_start))
            for register in pending_frame.architecture().registers("general"):
                try:
                    current = pending_frame.read_register(register)
                    if register.name in values:
                        current = gdb.Value(values[register.name] & 0xffffffff).cast(current.type)
                    info.add_saved_register(register, current)
                except (gdb.error, ValueError):
                    pass
            return info

    _nx_unwinder = NuttxUnwinder()
    register_unwinder(None, _nx_unwinder, replace=True)
    for _event in ("stop", "cont", "memory_changed", "register_changed", "new_objfile"):
        if hasattr(gdb.events, _event):
            getattr(gdb.events, _event).connect(_nx_unwinder.invalidate)

###########################################################################
#
#   Mono interpreter frames.
#
###########################################################################

MONO_INTERP_FUNCTIONS = ("mono_interp_exec_method", "interp_exec_method_full")

def nx_mono_method_name(method):
    '''Namespace.Class+Nested::Method for a MonoMethod pointer, read without calling into the target.'''
    method = method.dereference()
    name = method["name"].string()
    klass = method["klass"]
    if int(klass) == 0:
        return name
    #
    #   Nested classes have no namespace of their own: prefix the enclosing classes
    #   (Interop+Sys), then the outermost class's namespace.
    #
    class_name = klass.dereference()["name"].string()
    outer = klass
    depth = 0
    while (int(outer.dereference()["nested_in"]) != 0) and (depth < 8):
        outer = outer.dereference()["nested_in"]
        class_name = outer.dereference()["name"].string() + "+" + class_name
        depth += 1
    name_space = outer.dereference()["name_space"].string()
    if name_space:
        class_name = name_space + "." + class_name
    return "%s::%s" % (class_name, name)

def nx_mono_current_interp_frame(frame):
    '''Value of the "frame" variable (InterpFrame *) in an interpreter frame, or None.'''
    try:
        value = frame.read_var("frame")
    except (ValueError, gdb.error):
        return None
    if value.is_optimized_out:
        return None
    return value

###########################################################################
#
#   Backtrace.
#
###########################################################################

class NuttxAndMonoBacktrace(object):
    '''Backtrace of the selected thread as a list of strings.'''

    def __init__(self):
        self._backtrace = []

    def backtrace(self, include_managed_code, limit=200):
        self._backtrace = []
        frames = []
        frame = gdb.newest_frame()
        while (frame is not None) and (len(frames) < limit):
            frames.append(frame)
            try:
                frame = frame.older()
            except gdb.error as e:
                self._backtrace.append("Backtrace stopped: %s" % e)
                frame = None
        interp_frames = [nx_mono_current_interp_frame(f) if f.name() in MONO_INTERP_FUNCTIONS else None
                         for f in frames]
        number = 0
        for index, frame in enumerate(frames):
            self._backtrace.append(self.format_frame(frame, number, self.annotate_frame(frame)))
            number += 1
            if include_managed_code and (frame.name() in MONO_INTERP_FUNCTIONS):
                stop_at = None
                for outer in interp_frames[index + 1:]:
                    if outer is not None:
                        stop_at = int(outer)
                        break
                for line in self.managed_frames(interp_frames[index], stop_at):
                    self._backtrace.append("    %s" % line)
        if (frame is not None) and (len(frames) >= limit):
            self._backtrace.append("(more stack frames follow...)")
        return self._backtrace

    def managed_frames(self, interp_frame, stop_at):
        if interp_frame is None:
            return ["[managed frames unavailable: 'frame' is optimized out]"]
        lines = []
        count = 0
        try:
            while (int(interp_frame) != 0) and (int(interp_frame) != stop_at) and (count < 100):
                imethod = interp_frame.dereference()["imethod"]
                if int(imethod) != 0:
                    name = nx_mono_method_name(imethod.dereference()["method"])
                else:
                    name = "<unknown method>"
                lines.append("[managed] %s (InterpFrame 0x%08x)" % (name, int(interp_frame)))
                interp_frame = interp_frame.dereference()["parent"]
                count += 1
        except (gdb.error, gdb.MemoryError) as e:
            lines.append("[managed frames stopped: %s]" % e)
        return lines

    def format_frame(self, frame, number, annotation):
        pc = frame.pc()
        if frame.type() == gdb.SIGTRAMP_FRAME:
            #
            #   GDB's own frame for a hardware exception entry; its PC is the
            #   EXC_RETURN value.
            #
            return "#%-3d <exception frame, EXC_RETURN 0x%08x>" % (number, pc)
        name = frame.name()
        if name is None:
            #
            #   Code without symbols is usually Mono JIT code; Mono.py (when loaded)
            #   names it from the runtime's JIT info table.  For outer frames the PC
            #   is a return address, so look up the instruction before it.
            #
            jit_name = globals().get("mono_jit_frame_name")
            if jit_name is not None:
                jit = jit_name(pc - 1 if frame.level() > 0 else pc)
                if jit is not None:
                    text = "#%-3d 0x%08x in %s" % (number, pc, jit)
                    if annotation:
                        text += " [%s]" % annotation
                    return text
            name = "??"
        sal = frame.find_sal()
        location = ""
        if sal.symtab is not None:
            location = " at %s:%d" % (sal.symtab.filename, sal.line)
        else:
            objfile = gdb.current_progspace().solib_name(pc)
            if objfile:
                location = " from %s" % objfile
        text = "#%-3d 0x%08x in %s ()%s" % (number, pc, name, location)
        if annotation:
            text += " [%s]" % annotation
        return text

    def annotate_frame(self, frame):
        name = frame.name()
        if name is None:
            return None
        try:
            if name == "exception_common":
                return self.annotate_exception_common(frame)
            if name == "arm_svcall":
                return self.annotate_svcall(frame)
            if name == "dispatch_syscall":
                return "syscall dispatch"
            if name in MONO_INTERP_FUNCTIONS:
                return "managed"
            if name.startswith("sys_call"):
                return "syscall"
        except (gdb.error, gdb.MemoryError, ValueError):
            return None
        return None

    def annotate_exception_common(self, frame):
        #
        #   exception_common keeps the register save area pointer in r11 (fp).
        #
        ipsr = int(frame.read_register("xpsr")) & 0x1ff
        layout = nx_exception_layout(frame.architecture())
        if layout.valid() and (frame.pc() == layout.start):
            return "%s, exception entry" % nx_exception_name(ipsr)
        if not (layout.valid() and (frame.pc() == layout.after_call)):
            return "%s" % nx_exception_name(ipsr)
        regs = nx_reg(frame, "r11")
        if regs == 0:
            return "%s" % nx_exception_name(ipsr)
        saved = nx_read_saved_regs(regs)
        return "%s, interrupted pc 0x%08x" % (nx_exception_name(ipsr), saved["pc"])

    def annotate_svcall(self, frame):
        try:
            context = frame.read_var("context")
        except ValueError:
            return "svcall"
        if context.is_optimized_out:
            return "svcall"
        cmd = nx_read_u32(int(context) + nx_reg_offsets()["r0"])
        if cmd in NX_SVCALL_NAMES:
            return NX_SVCALL_NAMES[cmd]
        stubs = nx_symbol_value("g_stublookup")
        if stubs is not None:
            try:
                stub = nx_symbol_name(int(stubs[cmd - NX_SYS_RESERVED]))
                if stub:
                    return "syscall %s" % stub.replace("STUB_", "")
            except (gdb.error, gdb.MemoryError):
                pass
        return "syscall %d" % (cmd - NX_SYS_RESERVED)

class NuttxBacktrace(gdb.Command):
    '''
    (NuttX) Backtrace of the selected thread with exception, syscall and managed frame
    annotations.

    Usage: nx_bt [pid] [nomanaged]

    Without a pid the task of the selected GDB thread is used.  For a task that is
    not running the backtrace starts from the context saved in its TCB.
    '''

    def __init__(self):
        super(NuttxBacktrace, self).__init__("nx_bt", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        include_managed = True
        pid = None
        for word in arg.split():
            if word == "nomanaged":
                include_managed = False
            else:
                pid = int(gdb.parse_and_eval(word))
        if pid is None:
            tcb = nx_tcb_for_thread()
        else:
            tcb = nx_tcb_for_pid(pid)
            if tcb is None:
                raise gdb.GdbError("No task with pid %d." % pid)
        tcb_value = tcb.dereference()
        print("Task %s (pid %d, tcb 0x%08x)" % (tcb_value["name"].string(), int(tcb_value["pid"]),
                                               int(tcb)))
        if int(tcb) == int(nx_running_tcb()):
            ipsr = nx_in_exception()
            if ipsr != 0:
                print("CPU halted in handler mode: %s" % nx_exception_name(ipsr))
        else:
            print("Not running; unwinding from the context saved in tcb->xcp.regs")
        backtrace = NuttxAndMonoBacktrace()
        lines = nx_with_task_context(tcb, lambda: backtrace.backtrace(include_managed_code=include_managed))
        for line in lines:
            print(line)

NuttxBacktrace()

###########################################################################
#
#   Register commands.
#
###########################################################################

class NuttxDumpRegisters(gdb.Command):
    '''
    (NuttX) Decode a NuttX register save area.

    Usage: nx_dumpregs <address>
    The address is the value of tcb->xcp.regs or of the regs / context argument of
    arm_doirq, arm_svcall or the fault handlers.
    '''

    def __init__(self):
        super(NuttxDumpRegisters, self).__init__("nx_dumpregs", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        if not arg.strip():
            raise gdb.GdbError("Usage: nx_dumpregs <address>")
        address = int(gdb.parse_and_eval(arg))
        nx_print_regs(nx_read_saved_regs(address), NX_CORE_REGS)

NuttxDumpRegisters()

class NuttxDumpHWRegisters(gdb.Command):
    '''
    (NuttX) Decode a hardware exception frame.

    Usage: nx_dumpregs_hw <address>
    The address is the stack pointer (MSP or PSP) at which the processor stacked r0-r3,
    r12, lr, pc and xpsr on exception entry.
    '''

    def __init__(self):
        super(NuttxDumpHWRegisters, self).__init__("nx_dumpregs_hw", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        if not arg.strip():
            raise gdb.GdbError("Usage: nx_dumpregs_hw <address>")
        address = int(gdb.parse_and_eval(arg))
        names = ["r0", "r1", "r2", "r3", "r12", "lr", "pc", "xpsr"]
        values = dict((name, nx_read_u32(address + 4 * index)) for index, name in enumerate(names))
        nx_print_regs(values, names)

NuttxDumpHWRegisters()

class ARMRegContext(object):
    '''Saved copy of the general purpose registers of the newest frame.'''

    def __init__(self, frame=None):
        if frame is None:
            frame = gdb.newest_frame()
        self.names = [register.name for register in frame.architecture().registers("general")]
        self.values = {}
        for name in self.names:
            try:
                self.values[name] = nx_reg(frame, name)
            except (gdb.error, ValueError):
                pass

    def restore_registers(self):
        gdb.newest_frame().select()
        for name in self.names:
            if name in self.values:
                gdb.execute("set $%s = %d" % (name, self.values[name]), to_string=True)

    def dump_registers(self):
        nx_print_regs(self.values, self.names)

global_saved_regs_ctx = None

class NuttxSaveRegisters(gdb.Command):
    '''(NuttX) Save the CPU registers so that they can be restored with nx_restoreregs.'''

    def __init__(self):
        super(NuttxSaveRegisters, self).__init__("nx_saveregs", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        global global_saved_regs_ctx
        global_saved_regs_ctx = ARMRegContext()
        print("Saved %d registers." % len(global_saved_regs_ctx.values))

NuttxSaveRegisters()

class NuttxRestoreRegisters(gdb.Command):
    '''(NuttX) Restore the CPU registers saved by nx_saveregs.'''

    def __init__(self):
        super(NuttxRestoreRegisters, self).__init__("nx_restoreregs", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        if global_saved_regs_ctx is None:
            raise gdb.GdbError("No saved CPU registers context was found.")
        global_saved_regs_ctx.restore_registers()
        print("Registers restored.")

NuttxRestoreRegisters()

###########################################################################
#
#   RAM log.
#
###########################################################################

class NuttxDmesg(gdb.Command):
    '''
    (NuttX) Print the RAM log (syslog) buffer.

    The buffer (g_sysbuffer in drivers/syslog/ramlog.c) starts with a struct
    ramlog_header_s { rl_magic, rl_head }; rl_head counts every byte ever written,
    so once the buffer has wrapped the oldest data starts at rl_head % size.
    '''

    def __init__(self):
        super(NuttxDmesg, self).__init__("nx_dmesg", gdb.COMMAND_STATUS)

    def invoke(self, arg, from_tty):
        symbol = nx_symbol_value("g_sysbuffer")
        if symbol is None:
            raise gdb.GdbError("g_sysbuffer not found (is CONFIG_RAMLOG enabled?)")
        address = int(symbol.address)
        size = symbol.type.sizeof
        data = bytes(gdb.selected_inferior().read_memory(address, size))
        head = int.from_bytes(data[4:8], "little")
        buffer = data[8:]
        if head <= len(buffer):
            text = buffer[:head]
        else:
            start = head % len(buffer)
            text = buffer[start:] + buffer[:start]
        print(text.decode("utf-8", errors="replace"), end="")

NuttxDmesg()
