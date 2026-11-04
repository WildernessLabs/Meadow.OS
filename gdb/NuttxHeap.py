#
#   GDB/Python commands for the NuttX 12 default heap manager (mm/mm_heap).
#
#   Commands:
#
#       show heap [user|kernel]         List every node in the heap.
#       show freeheap [user|kernel]     List the free nodes in the heap.
#       nx_check_heap                   Check the node chain of both heaps.
#
#   The user heap is g_mmheap (from nuttx_user.elf) and the kernel heap is
#   g_kmmheap (from nuttx.elf); both are pointers to a struct mm_heap_s.
#
#   Node layout (mm/mm_heap/mm.h): every node starts with a struct mm_allocnode_s
#   { preceding, size }.  Bit 0 of size is set when the node is allocated and bit 1
#   when the previous node is free; preceding is only valid when the previous node
#   is free.  With CONFIG_MM_BACKTRACE >= 0 (not possible in the protected build today) the header also
#   holds the owner's pid and a sequence number, shown by show heap.  The node
#   size includes the header, and the next node starts at
#   node + size.  The first node of a region is at mm_heapstart[region] and the
#   chain ends with the guard node at mm_heapend[region].
#
import gdb

MM_ALLOC_BIT = 0x1
MM_PREVFREE_BIT = 0x2
MM_MASK_BIT = MM_ALLOC_BIT | MM_PREVFREE_BIT

def _read_word(address, size):
    memory = gdb.selected_inferior().read_memory(address, size)
    return int.from_bytes(bytes(memory), "little")

class NuttXHeap(object):
    '''(NuttX) Walk the nodes of one heap.'''

    HEAPS = {"user": "g_mmheap", "kernel": "g_kmmheap"}

    def __init__(self, heap_name):
        '''
        :param heap_name: "user" or "kernel" (or the variable name g_mmheap / g_kmmheap).
        :raises gdb.GdbError: if the heap cannot be found.
        '''
        for name, variable in self.HEAPS.items():
            if heap_name == variable:
                heap_name = name
        if heap_name not in self.HEAPS:
            raise gdb.GdbError("Invalid heap %s, expected user or kernel" % heap_name)
        self._heap_name = heap_name
        self._heap_variable_name = self.HEAPS[heap_name]
        allocnode = gdb.lookup_type("struct mm_allocnode_s")
        self._size_bytes = allocnode["size"].type.sizeof
        self._size_offset = allocnode["size"].bitpos // 8
        self._preceding_offset = allocnode["preceding"].bitpos // 8
        self._allocnodesize = allocnode.sizeof
        #
        #   With CONFIG_MM_BACKTRACE >= 0 every node also records the owner's pid and
        #   an allocation sequence number.
        #
        field_names = [field.name for field in allocnode.fields()]
        self._owner = ("pid" in field_names) and ("seqno" in field_names)
        if self._owner:
            self._pid_offset = allocnode["pid"].bitpos // 8
            self._pid_bytes = allocnode["pid"].type.sizeof
            self._seqno_offset = allocnode["seqno"].bitpos // 8
            self._seqno_bytes = allocnode["seqno"].type.sizeof
        symbol = gdb.lookup_global_symbol(self._heap_variable_name)
        if symbol is None:
            raise gdb.GdbError("Cannot find %s; are the symbols for both ELF files loaded?" %
                               self._heap_variable_name)
        self._heap = symbol.value()
        if int(self._heap) == 0:
            raise gdb.GdbError("%s is NULL; the heap has not been initialised." % self._heap_variable_name)

    @property
    def name(self):
        return self._heap_name

    @property
    def has_owner(self):
        '''True when the nodes record the owner pid (CONFIG_MM_BACKTRACE >= 0).'''
        return self._owner

    def owner(self, node):
        '''(pid, seqno) recorded in an allocated node, or None.'''
        if not self._owner:
            return None
        pid = _read_word(node + self._pid_offset, self._pid_bytes)
        if pid & (1 << (8 * self._pid_bytes - 1)):
            pid -= 1 << (8 * self._pid_bytes)
        return (pid, _read_word(node + self._seqno_offset, self._seqno_bytes))

    @property
    def variable_name(self):
        return self._heap_variable_name

    def regions(self):
        '''List of (start, end) node addresses for each region.'''
        heap = self._heap.dereference()
        result = []
        for region in range(int(heap["mm_nregions"])):
            start = int(heap["mm_heapstart"][region])
            end = int(heap["mm_heapend"][region])
            if start != 0:
                result.append((start, end))
        return result

    def nodes(self, start, end, problems=None):
        '''
        Yield (node address, node size, allocated, previous free, preceding) for each
        node from start up to and including the end guard node.

        If problems is a list, inconsistencies are appended to it and the walk stops
        at the first node whose size makes it impossible to continue.
        '''
        node = start
        previous_size = None
        previous_allocated = None
        while node <= end:
            raw_size = _read_word(node + self._size_offset, self._size_bytes)
            size = raw_size & ~MM_MASK_BIT
            allocated = (raw_size & MM_ALLOC_BIT) != 0
            previous_free = (raw_size & MM_PREVFREE_BIT) != 0
            preceding = None
            if previous_free:
                preceding = _read_word(node + self._preceding_offset, self._size_bytes)
            if problems is not None and previous_allocated is not None:
                if previous_free == previous_allocated:
                    problems.append("0x%08x: previous-free bit is %d but the previous node is %s" %
                                    (node, int(previous_free), "allocated" if previous_allocated else "free"))
                if previous_free and (preceding != previous_size):
                    problems.append("0x%08x: preceding %d does not match previous node size %d" %
                                    (node, preceding, previous_size))
            yield (node, size, allocated, previous_free, preceding)
            if node == end:
                return
            if (size < self._allocnodesize) or (node + size > end):
                if problems is not None:
                    problems.append("0x%08x: size %d is invalid (region ends at 0x%08x)" % (node, size, end))
                return
            previous_size = size
            previous_allocated = allocated
            node += size
        if problems is not None:
            problems.append("node chain overran the end of the region at 0x%08x" % end)

    def get_heap_allocations(self, free_only=False):
        '''
        List of strings describing every node (or only the free nodes).

        Each line gives the payload address (node + header), the node size including
        the header and whether the node is free.
        '''
        lines = []
        regions = self.regions()
        lines.append("%d heap region(s)" % len(regions))
        for start, end in regions:
            lines.append("heap region 0x%08x - 0x%08x" % (start, end))
            allocated_bytes = 0
            free_bytes = 0
            allocated_count = 0
            free_count = 0
            largest_free = 0
            for node, size, allocated, previous_free, preceding in self.nodes(start, end):
                if node == end:
                    break
                if allocated:
                    allocated_bytes += size
                    allocated_count += 1
                else:
                    free_bytes += size
                    free_count += 1
                    largest_free = max(largest_free, size)
                if free_only and allocated:
                    continue
                line = "  0x%08x %8d %s" % (node + self._allocnodesize, size, "" if allocated else "(free)")
                if allocated and self._owner:
                    pid, seqno = self.owner(node)
                    line += "pid %d seq %d" % (pid, seqno)
                lines.append(line)
            lines.append("  allocated %d bytes in %d nodes, free %d bytes in %d nodes, largest free %d" %
                         (allocated_bytes, allocated_count, free_bytes, free_count, largest_free))
        return lines

    def get_free_heap_nodes(self):
        return self.get_heap_allocations(free_only=True)

    def check_heap_nodes(self):
        '''List of strings describing inconsistent nodes; empty when the heap is consistent.'''
        problems = []
        for start, end in self.regions():
            last = None
            for node in self.nodes(start, end, problems):
                last = node[0]
            if last != end:
                problems.append("region 0x%08x - 0x%08x: walk ended at 0x%08x, not at the end node" %
                                (start, end, last if last is not None else 0))
        return problems

def _heap_from_argument(arg):
    arg = arg.strip()
    if arg == "":
        arg = "user"
    return NuttXHeap(arg)

class NX_show_heap(gdb.Command):
    '''
    (NuttX) List the nodes of a heap.

    Usage: show heap [user|kernel]      (default user)
    '''

    def __init__(self):
        super(NX_show_heap, self).__init__("show heap", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        heap = _heap_from_argument(arg)
        print("Showing heap allocations for %s (%s)" % (heap.name, heap.variable_name))
        print("  %-10s %8s" % ("Address", "Size"))
        for line in heap.get_heap_allocations():
            print(line)

NX_show_heap()

class NX_show_free_heap(gdb.Command):
    '''
    (NuttX) List the free nodes of a heap.

    Usage: show freeheap [user|kernel]      (default user)
    '''

    def __init__(self):
        super(NX_show_free_heap, self).__init__("show freeheap", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        heap = _heap_from_argument(arg)
        print("Showing free nodes for %s (%s)" % (heap.name, heap.variable_name))
        print("  %-10s %8s" % ("Address", "Size"))
        for line in heap.get_free_heap_nodes():
            print(line)

NX_show_free_heap()

class NX_check_heap(gdb.Command):
    '''(NuttX) Check the node chains of the kernel and user heaps.'''

    def __init__(self):
        super(NX_check_heap, self).__init__("nx_check_heap", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        for name in ("kernel", "user"):
            heap = NuttXHeap(name)
            problems = heap.check_heap_nodes()
            if len(problems) == 0:
                print("%s heap OK" % name.capitalize())
            else:
                print("%s heap: %d problem(s) found" % (name.capitalize(), len(problems)))
                for problem in problems:
                    print("    %s" % problem)

NX_check_heap()
