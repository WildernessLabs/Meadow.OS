#
#   Backtrace and heap tracing for Meadow.OS.  Uses NuttxAndMonoBacktrace from
#   Nuttx.py, so source Nuttx.py first.
#
#   Backtrace tracing:
#
#       trace_start                 Clear the recorded data and enable tracing.
#       trace_stop                  Disable tracing.
#       trace_add_backtrace         Record the backtrace of the current thread
#                                   (use in a breakpoint's command list).
#       show trace_data             Print the recorded backtraces.
#
#   Heap tracing:
#
#       trace_heap_start [user|kernel|all] [max_events]
#                                   Clear the recorded data and break on mm_malloc
#                                   and mm_free.  Each allocation is recorded with
#                                   its backtrace and removed again when it is
#                                   freed; execution continues automatically
#                                   until max_events allocations and frees have
#                                   been recorded (default: no limit, interrupt
#                                   with Ctrl-C).
#       trace_heap_stop             Remove the heap tracing breakpoints.
#       show heap_trace             Print the allocations that have not been
#                                   freed, and frees of unknown addresses.
#
#   Heap tracing stops the target on every allocation and free, so expect the
#   board to run very slowly while it is enabled.  Allocations made by
#   mm_memalign and in-place mm_realloc are not seen; their later frees are
#   listed as frees of unknown addresses.
#
import gdb

g_trace_information = []
g_heap_information = {}
g_heap_unknown_frees = []
g_tracing = False
g_heap_breakpoints = []
g_heap_filter = "all"
g_heap_events = 0
g_heap_max_events = 0
g_ignored_heap_methods = ["malloc", "mm_malloc", "mm_zalloc", "mm_calloc", "kmm_malloc", "kmm_zalloc",
                          "zalloc", "calloc", "kmm_calloc"]

def heap_address(name):
    '''Address of the struct mm_heap_s for "user" or "kernel", or None.'''
    variable = "g_mmheap" if name == "user" else "g_kmmheap"
    symbol = gdb.lookup_global_symbol(variable)
    if symbol is None:
        return None
    return int(symbol.value())

def heapname(address):
    '''Name of the heap whose struct mm_heap_s is at address.'''
    if address == heap_address("kernel"):
        return "kernel"
    if address == heap_address("user"):
        return "user"
    return "0x%08x" % address

def _heap_wanted(heap):
    return (g_heap_filter == "all") or (heapname(heap) == g_heap_filter)

def _count_heap_event():
    '''Count a recorded event; True when the event limit has been reached.'''
    global g_heap_events
    g_heap_events += 1
    if (g_heap_max_events > 0) and (g_heap_events >= g_heap_max_events):
        print("trace: %d heap events recorded, stopping" % g_heap_events)
        return True
    return False

def _frame_argument(frame, name):
    value = frame.read_var(name)
    if value.is_optimized_out:
        raise gdb.GdbError("%s is optimized out in %s" % (name, frame.name()))
    return int(value)

class TraceStart(gdb.Command):
    '''Clear the recorded trace data and enable tracing.'''

    def __init__(self):
        super(TraceStart, self).__init__("trace_start", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        global g_trace_information, g_heap_information, g_heap_unknown_frees, g_tracing
        g_trace_information = []
        g_heap_information = {}
        g_heap_unknown_frees = []
        g_tracing = True
        print("Trace information reset")

TraceStart()

class TraceStop(gdb.Command):
    '''Disable tracing.'''

    def __init__(self):
        super(TraceStop, self).__init__("trace_stop", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        global g_tracing
        g_tracing = False
        print("Trace stopped")

TraceStop()

class AddBackTrace(gdb.Command):
    '''Record the backtrace of the current thread.'''

    def __init__(self):
        super(AddBackTrace, self).__init__("trace_add_backtrace", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        if g_tracing:
            bt = NuttxAndMonoBacktrace()
            g_trace_information.append(bt.backtrace(include_managed_code=True))
            print("Backtrace collected, continuing execution")
        else:
            print("Cannot collect backtrace as tracing is disabled")

AddBackTrace()

class ShowTraceData(gdb.Command):
    '''Print the backtraces recorded by trace_add_backtrace.'''

    def __init__(self):
        super(ShowTraceData, self).__init__("show trace_data", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        trace_count = 1
        for backtrace in g_trace_information:
            if trace_count != 1:
                print("")
            print("********** Trace %d" % trace_count)
            trace_count += 1
            for line in backtrace:
                print(line)

ShowTraceData()

class _MallocReturn(gdb.FinishBreakpoint):
    '''Records the result of one mm_malloc call when it returns.'''

    def __init__(self, frame, heap, size, backtrace):
        super(_MallocReturn, self).__init__(frame, internal=True)
        self.silent = True
        self._heap = heap
        self._size = size
        self._backtrace = backtrace

    def stop(self):
        address = int(self.return_value) if self.return_value is not None else 0
        if (address != 0) and g_tracing:
            g_heap_information[address] = {"requested": self._size, "heap": self._heap,
                                           "backtrace": self._backtrace}
            return _count_heap_event()
        return False

    def out_of_scope(self):
        pass

class _MallocBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(_MallocBreakpoint, self).__init__("mm_malloc", internal=True)
        self.silent = True

    def stop(self):
        try:
            frame = gdb.newest_frame()
            heap = _frame_argument(frame, "heap")
            if g_tracing and _heap_wanted(heap):
                size = _frame_argument(frame, "size")
                backtrace = NuttxAndMonoBacktrace().backtrace(include_managed_code=False, limit=20)
                _MallocReturn(frame, heap, size, backtrace)
        except (gdb.error, gdb.GdbError, ValueError) as e:
            print("trace: mm_malloc: %s" % e)
        return False

class _FreeBreakpoint(gdb.Breakpoint):
    def __init__(self):
        super(_FreeBreakpoint, self).__init__("mm_free", internal=True)
        self.silent = True

    def stop(self):
        try:
            frame = gdb.newest_frame()
            heap = _frame_argument(frame, "heap")
            address = _frame_argument(frame, "mem")
            if g_tracing and (address != 0) and _heap_wanted(heap):
                if address in g_heap_information:
                    del g_heap_information[address]
                else:
                    g_heap_unknown_frees.append({"address": address, "heap": heap})
                return _count_heap_event()
        except (gdb.error, gdb.GdbError, ValueError) as e:
            print("trace: mm_free: %s" % e)
        return False

class TraceHeapStart(gdb.Command):
    '''
    Start tracing heap allocations.

    Usage: trace_heap_start [user|kernel|all] [max_events]

    The heap defaults to all.  With max_events the target stops after that many
    allocations and frees have been recorded.
    '''

    def __init__(self):
        super(TraceHeapStart, self).__init__("trace_heap_start", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        global g_heap_filter, g_heap_events, g_heap_max_events
        words = arg.split()
        heap = words[0] if len(words) > 0 else "all"
        if (heap not in ("user", "kernel", "all")) or (len(words) > 2):
            raise gdb.GdbError("Usage: trace_heap_start [user|kernel|all] [max_events]")
        g_heap_max_events = int(gdb.parse_and_eval(words[1])) if len(words) > 1 else 0
        g_heap_events = 0
        gdb.execute("trace_heap_stop", to_string=True)
        gdb.execute("trace_start", to_string=True)
        g_heap_filter = heap
        g_heap_breakpoints.append(_MallocBreakpoint())
        g_heap_breakpoints.append(_FreeBreakpoint())
        print("Tracing %s heap allocations; continue the target to collect data." % heap)

TraceHeapStart()

class TraceHeapStop(gdb.Command):
    '''Stop tracing heap allocations; the recorded data is kept.'''

    def __init__(self):
        super(TraceHeapStop, self).__init__("trace_heap_stop", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        global g_tracing
        while g_heap_breakpoints:
            breakpoint = g_heap_breakpoints.pop()
            if breakpoint.is_valid():
                breakpoint.delete()
        g_tracing = False
        print("Heap tracing stopped")

TraceHeapStop()

class ShowHeapTraceData(gdb.Command):
    '''Print the allocations recorded by heap tracing that have not been freed.'''

    def __init__(self):
        super(ShowHeapTraceData, self).__init__("show heap_trace", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        print("%d allocation(s) not freed" % len(g_heap_information))
        for address in sorted(g_heap_information):
            heapinfo = g_heap_information[address]
            print("Memory allocation 0x%08x, requested %d from %s heap" %
                  (address, heapinfo["requested"], heapname(heapinfo["heap"])))
            for line in heapinfo["backtrace"]:
                if not [method for method in g_ignored_heap_methods if (" %s ()" % method) in line]:
                    print("    %s" % line)
        if len(g_heap_unknown_frees) > 0:
            print("Free from unknown addresses:")
            for unknown_free in g_heap_unknown_frees:
                print("    Address: 0x%08x on %s heap" % (unknown_free["address"], heapname(unknown_free["heap"])))

ShowHeapTraceData()
