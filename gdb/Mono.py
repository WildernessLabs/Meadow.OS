#
#   GDB commands for the Mono (.NET) runtime.  Needs the helpers in Nuttx.py, so
#   source Nuttx.py first.  Everything here reads target memory; nothing calls
#   into the runtime.
#
#   Commands:
#
#       nx_jit <address>    The managed method (or trampoline) whose JIT code
#                           contains address.
#       nx_jit_compile      What the JIT is compiling now, on every task, and the
#                           execution engine settings.
#
#   Loading this file also makes nx_bt name JIT code frames ("[JIT] ...")
#   instead of printing "?? ()".
#
import gdb

#
#   Entries per chunk of the JIT info table (MONO_JIT_INFO_TABLE_CHUNK_SIZE in
#   mono/metadata/jit-info.h).
#
MONO_JIT_INFO_CHUNK_SIZE = 64

def mono_jit_info_chunks():
    '''
    The chunks of Mono's JIT info table (the static jit_info_table in
    metadata/jit-info.c) as gdb.Values, or [] when Mono has not started.
    '''
    table = nx_symbol_value("jit_info_table")
    if (table is None) or (int(table) == 0):
        return []
    table = table.dereference()
    count = int(table["num_chunks"])
    chunk_pointer = gdb.lookup_type("MonoJitInfoTableChunk").pointer()
    first = table["chunks"].address.cast(chunk_pointer.pointer())
    return [first[index] for index in range(count) if int(first[index]) != 0]

def mono_jit_info_find(address):
    '''
    The MonoJitInfo (gdb.Value pointer) whose code contains address, or None.

    The chunks are sorted by code address and each records the end of the last
    code it holds (last_code_end), so only one chunk has to be searched.
    '''
    address = int(address)
    for chunk in mono_jit_info_chunks():
        chunk = chunk.dereference()
        if address >= int(chunk["last_code_end"]):
            continue
        count = min(int(chunk["num_elements"]), MONO_JIT_INFO_CHUNK_SIZE)
        for index in range(count):
            ji = chunk["data"][index]
            if int(ji) == 0:
                continue
            info = ji.dereference()
            if int(info["d"]["method"]) == 0:
                continue
            start = int(info["code_start"])
            if start <= address < start + int(info["code_size"]):
                return ji
        return None
    return None

def mono_jit_info_name(ji):
    '''Printable name for a MonoJitInfo: the managed method, or the trampoline name.'''
    info = ji.dereference()
    if int(info["is_trampoline"]):
        tramp = info["d"]["tramp_info"]
        name = tramp.dereference()["name"]
        return "trampoline %s" % (name.string() if int(name) else "?")
    kind = "interp " if int(info["is_interp"]) else ""
    try:
        return kind + nx_mono_method_name(info["d"]["method"])
    except (gdb.error, gdb.MemoryError, UnicodeDecodeError):
        return "%s<MonoJitInfo 0x%08x>" % (kind, int(ji))

def mono_jit_frame_name(pc):
    '''"[JIT] Class::Method+0x..." for a JIT code address, or None.'''
    try:
        ji = mono_jit_info_find(pc)
    except (gdb.error, gdb.MemoryError):
        return None
    if ji is None:
        return None
    offset = int(pc) - int(ji.dereference()["code_start"])
    return "[JIT] %s+0x%x" % (mono_jit_info_name(ji), offset)

class MonoJitInfoCommand(gdb.Command):
    '''
    (Mono) The managed method whose JIT code contains an address.

    Usage: nx_jit <address>

    Reads Mono's JIT info table (jit_info_table in metadata/jit-info.c) from
    memory, so it also works when the runtime is stopped in a bad state.
    '''

    def __init__(self):
        super(MonoJitInfoCommand, self).__init__("nx_jit", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        if not arg.strip():
            raise gdb.GdbError("Usage: nx_jit <address>")
        address = int(gdb.parse_and_eval(arg))
        if not mono_jit_info_chunks():
            raise gdb.GdbError("The JIT info table is empty (Mono not started?).")
        ji = mono_jit_info_find(address)
        if ji is None:
            print("0x%08x is not in any JIT-compiled code." % address)
            return
        info = ji.dereference()
        start = int(info["code_start"])
        print("0x%08x is %s+0x%x" % (address, mono_jit_info_name(ji), address - start))
        print("  code 0x%08x-0x%08x (%d bytes), MonoJitInfo 0x%08x%s%s" % (
            start, start + int(info["code_size"]), int(info["code_size"]), int(ji),
            ", from AOT" if int(info["from_aot"]) else "", ", from LLVM" if int(info["from_llvm"]) else ""))

MonoJitInfoCommand()

class MonoJitCommand(gdb.Command):
    '''
    (Mono) What the JIT is compiling now, and the execution engine settings.

    Looks for mini_method_compile on the stack of every task (using the context
    saved in each TCB) and prints the method being compiled.
    '''

    def __init__(self):
        super(MonoJitCommand, self).__init__("nx_jit_compile", gdb.COMMAND_DATA)

    @staticmethod
    def compiling():
        found = []
        frame = gdb.newest_frame()
        while frame is not None:
            if frame.name() == "mini_method_compile":
                try:
                    method = frame.read_var("method")
                    found.append(nx_mono_method_name(method) if not method.is_optimized_out else
                                 "<method optimized out>")
                except (ValueError, gdb.error, gdb.MemoryError) as e:
                    found.append("<%s>" % e)
            try:
                frame = frame.older()
            except gdb.error:
                frame = None
        return found

    def invoke(self, arg, from_tty):
        interpreter = nx_symbol_value("mono_use_interpreter")
        if interpreter is not None:
            print("Interpreter as default engine (--interp): %s" % ("yes" if int(interpreter) else "no"))
        classes = nx_symbol_value("mono_interp_jit_classes")
        if (classes is not None) and (int(classes) != 0):
            names = []
            node = classes
            while int(node) != 0 and len(names) < 50:
                data = node.dereference()["data"]
                names.append(data.cast(gdb.lookup_type("char").pointer()).string())
                node = node.dereference()["next"]
            print("Classes the JIT compiles in mixed mode (--interp=jit=): %s" % ", ".join(names))
        print("JIT info table: %d chunks" % len(mono_jit_info_chunks()))
        anything = False
        for tcb in nx_tcbs():
            try:
                methods = nx_with_task_context(tcb, self.compiling)
            except gdb.GdbError:
                continue
            for method in methods:
                anything = True
                print("Task %s (pid %d) is compiling %s" % (tcb.dereference()["name"].string(),
                                                           int(tcb.dereference()["pid"]), method))
        if not anything:
            print("No task is in mini_method_compile.")

MonoJitCommand()
