#
#   GDB/Python commands for NuttX 12 tasks.  Needs the helpers in Nuttx.py, so
#   source Nuttx.py first.
#
#   Commands:
#
#       nx_tasks                    List all tasks.
#       nx_task <pid>               Details of one task: state, wait object, stack,
#                                   open files and saved registers.
#       show alltcb                 PID and name of every task.
#       show tcb <pid>              Print the struct tcb_s of a task.
#       show regs [pid]             Saved registers of one or all tasks and the
#                                   source line at the saved PC.
#       show interrupted-thread     Registers of the context interrupted by the
#                                   current exception.
#       show check_stack            Stack regions and stack pointers of all tasks
#                                   in address order.
#       show mybt <pid|name|tcb>    Heuristic backtrace: scan a task's stack for
#                                   words that point into code.
#
import gdb
import gdb.types

#
#   Fill pattern used by CONFIG_STACK_COLORATION (arch/arm/src/common/arm_internal.h).
#
NX_STACK_COLOR = 0xdeadbeef

#
#   Bit in sem_t.flags marking a semaphore that is a mutex (include/semaphore.h)
#   and the mask that removes the blocking bit from the mutex holder.
#
NX_SEM_TYPE_MUTEX = 4
NX_SEM_MHOLDER_MASK = 0x7fffffff

def nx_parse_int(text):
    return int(gdb.parse_and_eval(text))

class NX_task(object):
    '''Reference to a NuttX task and methods for introspecting it.'''

    def __init__(self, tcb_ptr):
        self._tcb_ptr = tcb_ptr
        self._tcb = tcb_ptr.dereference()
        self.pid = int(self._tcb["pid"])

    @staticmethod
    def tasks():
        '''All tasks, sorted by PID.'''
        tasks = [NX_task(tcb) for tcb in nx_tcbs()]
        return sorted(tasks, key=lambda task: task.pid)

    @staticmethod
    def pids():
        return [task.pid for task in NX_task.tasks()]

    @classmethod
    def for_pid(cls, pid):
        tcb = nx_tcb_for_pid(pid)
        if tcb is None:
            return None
        return cls(tcb)

    @classmethod
    def for_tcb(cls, tcb):
        for candidate in nx_tcbs():
            if int(candidate) == int(tcb):
                return cls(candidate)
        return None

    @classmethod
    def find(cls, text):
        '''Task from a PID, a task name or a TCB address.'''
        text = text.strip()
        try:
            number = nx_parse_int(text)
        except gdb.error:
            number = None
        if number is not None:
            task = cls.for_pid(number)
            if task is None:
                task = cls.for_tcb(number)
            if task is not None:
                return task
        for task in cls.tasks():
            if task.name == text:
                return task
        raise gdb.GdbError("No task matches '%s'." % text)

    @property
    def address(self):
        return int(self._tcb_ptr)

    @property
    def name(self):
        return self._tcb["name"].string()

    @property
    def priority(self):
        return int(self._tcb["sched_priority"])

    def _state_value(self):
        return int(self._tcb["task_state"])

    @property
    def state(self):
        '''Name of the task's current state (enum tstate_e).'''
        state = self._state_value()
        names = gdb.types.make_enum_dict(gdb.lookup_type("enum tstate_e"))
        for name, value in names.items():
            if int(value) == state:
                return name.replace("TSTATE_", "")
        return "UNKNOWN(%d)" % state

    def _state_is(self, state):
        names = gdb.types.make_enum_dict(gdb.lookup_type("enum tstate_e"))
        return (state in names) and (int(names[state]) == self._state_value())

    @property
    def is_running(self):
        return self.address == int(nx_running_tcb())

    @property
    def is_waiting(self):
        return int(self._tcb["waitobj"]) != 0

    @property
    def waiting_for(self):
        '''Description of the object the task is waiting on.'''
        waitobj = int(self._tcb["waitobj"])
        if waitobj == 0:
            if self._state_is("TSTATE_WAIT_SIG"):
                return "signal"
            return ""
        description = "0x%08x" % waitobj
        symbol = nx_symbol_name(waitobj)
        if symbol:
            description += " <%s>" % symbol
        if self._state_is("TSTATE_WAIT_SEM"):
            sem = gdb.Value(waitobj).cast(gdb.lookup_type("sem_t").pointer()).dereference()
            if int(sem["flags"]) & NX_SEM_TYPE_MUTEX:
                holder = int(sem["val"]["mholder"]) & NX_SEM_MHOLDER_MASK
                task = NX_task.for_pid(holder)
                holder_name = task.name if task is not None else "?"
                description = "mutex %s held by pid %d (%s)" % (description, holder, holder_name)
            else:
                description = "semaphore %s count %d" % (description, int(sem["val"]["semcount"]))
        elif self._state_is("TSTATE_WAIT_MQNOTEMPTY") or self._state_is("TSTATE_WAIT_MQNOTFULL"):
            description = "message queue %s" % description
        return description

    @property
    def stack_top(self):
        return int(self._tcb["stack_base_ptr"]) + int(self._tcb["adj_stack_size"])

    @property
    def stack_size(self):
        return int(self._tcb["adj_stack_size"])

    @property
    def saved_registers(self):
        '''Registers saved in the TCB, or None (running task or no saved context).'''
        regs = int(self._tcb["xcp"]["regs"])
        if regs == 0:
            return None
        return nx_read_saved_regs(regs)

    @property
    def registers(self):
        '''Current registers for the running task, otherwise the saved ones.'''
        if self.is_running:
            frame = gdb.newest_frame()
            return dict((name, nx_reg(frame, name)) for name in NX_CORE_REGS)
        return self.saved_registers

    @property
    def stack_pointer(self):
        registers = self.registers
        if registers is None:
            return None
        return registers["sp"]

    @property
    def stack_used(self):
        '''Bytes of stack in use at the current or saved stack pointer.'''
        sp = self.stack_pointer
        if sp is None or self.stack_size == 0:
            return None
        return self.stack_top - sp

    @property
    def stack_high_water(self):
        '''
        Most stack ever used, when the stack was filled by CONFIG_STACK_COLORATION;
        None otherwise.
        '''
        base = int(self._tcb["stack_base_ptr"])
        size = self.stack_size
        if (base == 0) or (size == 0) or (nx_read_u32(base) != NX_STACK_COLOR):
            return None
        #
        #   The stack grows down, so the untouched (still coloured) part is at the
        #   bottom.  Read upwards in chunks and stop at the first used word, so only
        #   the unused part of the stack is read over the debug link.
        #
        color = NX_STACK_COLOR.to_bytes(4, "little")
        offset = 0
        chunk_size = 4096
        while offset < size:
            length = min(chunk_size, size - offset)
            memory = bytes(gdb.selected_inferior().read_memory(base + offset, length))
            for index in range(0, length - 3, 4):
                if memory[index:index + 4] != color:
                    return size - (offset + index)
            offset += length
        return 0

    @property
    def file_descriptors(self):
        '''Dictionary of file descriptor -> inode name for open files.'''
        result = dict()
        group = self._tcb["group"]
        if int(group) == 0:
            return result
        fdlist = group.dereference()["tg_fdlist"]
        rows = int(fdlist["fl_rows"])
        fds = fdlist["fl_fds"]
        if int(fds) == 0:
            return result
        per_row = fdlist["fl_prefds"].type.range()[1] + 1
        for row in range(rows):
            row_pointer = fds[row]
            if int(row_pointer) == 0:
                continue
            for column in range(per_row):
                file = row_pointer[column]["f_file"]
                if int(file) == 0:
                    continue
                inode = file.dereference()["f_inode"]
                name = "?"
                if int(inode) != 0:
                    try:
                        name = inode.dereference()["i_name"].address.cast(
                            gdb.lookup_type("char").pointer()).string()
                    except gdb.error:
                        name = "0x%08x" % int(inode)
                result[row * per_row + column] = name
        return result

    def __repr__(self):
        return "<NX_task {}>".format(self.pid)

    def __str__(self):
        return "{}:{}".format(self.pid, self.name)

def format_registers(registers):
    if registers is None:
        return "  registers: not saved (task is running or has no saved context)\n"
    text = ""
    text += "  R0  %08x %08x %08x %08x\n" % (registers["r0"], registers["r1"], registers["r2"], registers["r3"])
    text += "  R4  %08x %08x %08x %08x\n" % (registers["r4"], registers["r5"], registers["r6"], registers["r7"])
    text += "  R8  %08x %08x %08x %08x\n" % (registers["r8"], registers["r9"], registers["r10"], registers["r11"])
    text += "  R12 %08x\n" % registers["r12"]
    text += "  SP  %08x LR %08x PC %08x XPSR %08x\n" % (registers["sp"], registers["lr"], registers["pc"],
                                                       registers["xpsr"])
    return text

class NX_show_task(gdb.Command):
    '''
    (NuttX) Print information about a task.

    Usage: nx_task <pid>
    '''

    def __init__(self):
        super(NX_show_task, self).__init__("nx_task", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        if not arg.strip():
            raise gdb.GdbError("Usage: nx_task <pid>")
        task = NX_task.for_pid(nx_parse_int(arg))
        if task is None:
            raise gdb.GdbError("No task with pid %s." % arg.strip())
        text = "PID: %d  name: %s  state: %s  priority: %d  tcb: 0x%08x\n" % (
            task.pid, task.name, task.state, task.priority, task.address)
        used = task.stack_used
        text += "  stack: size %d" % task.stack_size
        if used is not None:
            text += ", in use %d" % used
        high_water = task.stack_high_water
        if high_water is not None:
            text += ", most used %d" % high_water
        text += "\n"
        if task.waiting_for:
            text += "  waiting for: %s\n" % task.waiting_for
        text += "  open files: %s\n" % task.file_descriptors
        if task.is_running:
            text += "  (running: current CPU registers)\n"
        text += format_registers(task.registers)
        print(text, end="")

NX_show_task()

class NX_show_tasks(gdb.Command):
    '''(NuttX) List all tasks.'''

    def __init__(self):
        super(NX_show_tasks, self).__init__("nx_tasks", gdb.COMMAND_USER)

    def invoke(self, args, from_tty):
        tasks = NX_task.tasks()
        print("Number of tasks: %d" % len(tasks))
        print("%5s %-24s %-18s %4s %10s %7s %7s %7s  %s" % ("PID", "Name", "State", "Pri", "TCB",
                                                           "Stack", "Used", "Max", "Waiting for"))
        for task in tasks:
            used = task.stack_used
            used_text = "-" if used is None else str(used)
            #
            #   Max is the high-water mark, only known with CONFIG_STACK_COLORATION
            #   (build.sh --gdbdiag).
            #
            high_water = task.stack_high_water
            max_text = "-" if high_water is None else str(high_water)
            print("%5d %-24s %-18s %4d 0x%08x %7d %7s %7s  %s" % (task.pid, task.name[:24], task.state,
                                                                task.priority, task.address, task.stack_size,
                                                                used_text, max_text, task.waiting_for))

NX_show_tasks()

class NX_show_interrupted_thread(gdb.Command):
    '''
    (NuttX) Print the registers of the context interrupted by the current exception.

    Only meaningful when the CPU halted in handler mode, for example in a fault
    handler.  arm_doirq stores the interrupted context in the xcp.regs of the
    running task (g_running_tasks[0]); before it has done so the pointer is taken
    from the exception_common frame, and when the core stopped on the exception
    entry itself (vector catch) the hardware exception frame is used.
    '''

    def __init__(self):
        super(NX_show_interrupted_thread, self).__init__("show interrupted-thread", gdb.COMMAND_USER)

    def invoke(self, args, from_tty):
        ipsr = nx_in_exception()
        if ipsr == 0:
            raise gdb.GdbError("Not in interrupt context.")
        context = nx_interrupted_context()
        if context is None:
            raise gdb.GdbError("No interrupted context found (exception %s)." % nx_exception_name(ipsr))
        running = nx_running_tcb().dereference()
        print("Exception: %s, interrupted task: %s (pid %d), context from %s at 0x%08x" % (
            nx_exception_name(ipsr), running["name"].string(), int(running["pid"]),
            context["source"], context["address"]))
        print(format_registers(context["regs"]), end="")

NX_show_interrupted_thread()

class NX_check_tcb(gdb.Command):
    '''
    (NuttX) Print the TCB of a task.

    Usage: show tcb <pid>
    '''

    def __init__(self):
        super(NX_check_tcb, self).__init__("show tcb", gdb.COMMAND_USER)

    def invoke(self, args, from_tty):
        if not args.strip():
            raise gdb.GdbError("Usage: show tcb <pid>")
        task = NX_task.for_pid(nx_parse_int(args))
        if task is None:
            raise gdb.GdbError("No task with pid %s." % args.strip())
        print(task._tcb)

NX_check_tcb()

class NX_check_stack_order(gdb.Command):
    '''
    (NuttX) List the stack regions and stack pointers of all tasks in address order.

    Each task contributes three addresses: the start of its stack allocation, the top
    of the stack and its current or saved stack pointer (marked _SP).  Useful for
    spotting overlapping stacks or a stack pointer outside its stack.
    '''

    def __init__(self):
        super(NX_check_stack_order, self).__init__("show check_stack", gdb.COMMAND_USER)

    def invoke(self, args, from_tty):
        entries = []
        for task in NX_task.tasks():
            entries.append((int(task._tcb["stack_alloc_ptr"]), task.name + " (stack start)"))
            entries.append((task.stack_top, task.name + " (stack top)"))
            sp = task.stack_pointer
            if sp is not None:
                warning = ""
                if not (int(task._tcb["stack_base_ptr"]) <= sp <= task.stack_top):
                    warning = "  <-- outside its stack"
                entries.append((sp, task.name + "_SP" + warning))
        print("stack address  :  process")
        for address, name in sorted(entries, reverse=True):
            print("0x%08x     :  %s" % (address, name))

NX_check_stack_order()

class NX_run_debug_util(gdb.Command):
    '''
    (NuttX) Show the saved registers of a task (or all tasks) and the source line at
    the saved PC.

    Usage: show regs [pid]
    '''

    def __init__(self):
        super(NX_run_debug_util, self).__init__("show regs", gdb.COMMAND_USER)

    def show(self, task):
        print("Registers of %s (pid %d):" % (task.name, task.pid))
        registers = task.registers
        print(format_registers(registers), end="")
        if registers is not None:
            print("Location of the PC:")
            try:
                gdb.execute("list *0x%x" % registers["pc"])
            except gdb.error as e:
                print("  %s" % e)
        print("")

    def invoke(self, args, from_tty):
        if args.strip():
            task = NX_task.for_pid(nx_parse_int(args))
            if task is None:
                raise gdb.GdbError("No task with pid %s." % args.strip())
            self.show(task)
        else:
            for task in NX_task.tasks():
                self.show(task)

NX_run_debug_util()

class NX_search_tcb(gdb.Command):
    '''(NuttX) Show the PID and name of every task.'''

    def __init__(self):
        super(NX_search_tcb, self).__init__("show alltcb", gdb.COMMAND_USER)

    def invoke(self, args, from_tty):
        tasks = NX_task.tasks()
        print("%d Tasks found:" % len(tasks))
        for task in tasks:
            print("PID: %4d  %s" % (task.pid, task.name))

NX_search_tcb()

class NX_my_bt(gdb.Command):
    '''
    (NuttX) 'Fake' backtrace: scan the stack of a task for words that point into code.

    Usage: show mybt <pid | task name | tcb address>

    Every word between the task's stack pointer and the top of its stack that is the
    address of a known function is printed with the percentage of the stack above
    it.  This also works when the real unwinder gives up, but return addresses left
    in dead stack and function pointers stored on the stack are listed too.
    '''

    def __init__(self):
        super(NX_my_bt, self).__init__("show mybt", gdb.COMMAND_USER)

    def invoke(self, args, from_tty):
        if not args.strip():
            raise gdb.GdbError("Usage: show mybt <pid | task name | tcb address>")
        task = NX_task.find(args)
        print("Task %s (pid %d), tcb 0x%08x" % (task.name, task.pid, task.address))
        sp = task.stack_pointer
        top = task.stack_top
        size = task.stack_size
        if sp is None:
            raise gdb.GdbError("The task has no saved stack pointer.")
        print("Stack pointer 0x%08x, stack top 0x%08x" % (sp, top))
        if not (top - size <= sp <= top):
            raise gdb.GdbError("The stack pointer is outside the task's stack.")
        memory = bytes(gdb.selected_inferior().read_memory(sp, top - sp))
        item = 0
        for offset in range(0, len(memory) - 3, 4):
            value = int.from_bytes(memory[offset:offset + 4], "little")
            #
            #   Return addresses have the Thumb bit set.
            #
            if (value & 1) == 0:
                continue
            address = value & ~1
            sal = gdb.find_pc_line(address)
            name = nx_symbol_name(address)
            if (name is None) or (sal.symtab is None):
                continue
            used = 100.0 * (top - (sp + offset)) / size
            print("#%-3d %5.1f%% 0x%08x in %s at %s:%d" % (item, used, value, name, sal.symtab.filename,
                                                          sal.line))
            item += 1

NX_my_bt()
