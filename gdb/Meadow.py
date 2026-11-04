#
#   Meadow F7 (STM32F777) board and fault diagnostics.  Needs the helpers in
#   Nuttx.py, so source Nuttx.py first; Svd.py is optional (IRQ names).
#
#   Commands:
#
#       nx_alive [seconds]  Is the core running?  DHCSR state and the system tick
#                           count across a short run.
#       nx_reset            Reset reason, reset counters and the Meadow flags kept
#                           in the battery-backed registers.
#       nx_fault            Fault status registers decoded, the interrupted
#                           context and its backtrace.
#       nx_mpu              The MPU regions, decoded and named.
#       nx_irqs [all]       NVIC state of the interrupts, with handlers.
#       espcp_status        State of the ESP32 coprocessor link.
#
#   All of them only read the target, except that nx_alive resumes and halts the
#   core and nx_mpu writes MPU_RNR to select each region (and restores it).
#
import time

import gdb

#
#   System control space registers (Armv7-M Architecture Reference Manual).
#
SCB_SHCSR = 0xE000ED24
SCB_CFSR = 0xE000ED28
SCB_HFSR = 0xE000ED2C
SCB_DFSR = 0xE000ED30
SCB_MMFAR = 0xE000ED34
SCB_BFAR = 0xE000ED38
SCB_AFSR = 0xE000ED3C
SCB_SHPR1 = 0xE000ED18
DHCSR = 0xE000EDF0
DEMCR = 0xE000EDFC
NVIC_ISER = 0xE000E100
NVIC_ISPR = 0xE000E200
NVIC_IABR = 0xE000E300
NVIC_IPR = 0xE000E400
MPU_TYPE = 0xE000ED90
MPU_CTRL = 0xE000ED94
MPU_RNR = 0xE000ED98
MPU_RBAR = 0xE000ED9C
MPU_RASR = 0xE000EDA0

#
#   STM32F7 RTC backup registers (BKnR = RTC base 0x40002800 + 0x50 + 4n) and the
#   RCC reset flags.
#
RTC_BKR0 = 0x40002850
RCC_CSR = 0x40023874

#
#   Meadow use of the backup registers (F7MeadowOS/include/meadow/hcom_bbreg_defn.h).
#
BBR_FAULT_SOURCE = 29
BBR_RESET_REASON = 30
BBR_MEADOW_FLAGS = 31

CFSR_BITS = [
    (0, "MemManage IACCVIOL: instruction access violation"),
    (1, "MemManage DACCVIOL: data access violation"),
    (3, "MemManage MUNSTKERR: fault on unstacking"),
    (4, "MemManage MSTKERR: fault on stacking"),
    (5, "MemManage MLSPERR: fault during lazy FP state save"),
    (7, "MemManage MMARVALID: MMFAR holds the faulting address"),
    (8, "BusFault IBUSERR: instruction bus error"),
    (9, "BusFault PRECISERR: precise data bus error"),
    (10, "BusFault IMPRECISERR: imprecise data bus error (PC is not the faulting instruction)"),
    (11, "BusFault UNSTKERR: fault on unstacking"),
    (12, "BusFault STKERR: fault on stacking"),
    (13, "BusFault LSPERR: fault during lazy FP state save"),
    (15, "BusFault BFARVALID: BFAR holds the faulting address"),
    (16, "UsageFault UNDEFINSTR: undefined instruction"),
    (17, "UsageFault INVSTATE: invalid state (Thumb bit clear in EPSR)"),
    (18, "UsageFault INVPC: invalid EXC_RETURN or PC load"),
    (19, "UsageFault NOCP: coprocessor (FPU) access while disabled"),
    (24, "UsageFault UNALIGNED: unaligned access (LDRD/STRD/LDM/STM, or with the trap on)"),
    (25, "UsageFault DIVBYZERO: divide by zero (trap enabled)"),
]

HFSR_BITS = [
    (1, "VECTTBL: bus fault reading the vector table"),
    (30, "FORCED: a configurable fault escalated to HardFault (see CFSR)"),
    (31, "DEBUGEVT: debug event"),
]

DFSR_BITS = [
    (0, "HALTED: halt request or step"),
    (1, "BKPT: breakpoint"),
    (2, "DWTTRAP: watchpoint"),
    (3, "VCATCH: vector catch"),
    (4, "EXTERNAL: external debug request"),
]

DEMCR_VECTOR_CATCH = [
    (0, "reset"), (4, "mm_err"), (5, "nocp_err"), (6, "chk_err"), (7, "state_err"),
    (8, "bus_err"), (9, "int_err"), (10, "hard_err"),
]

DHCSR_BITS = [
    (17, "halted"), (18, "sleeping"), (19, "LOCKUP"), (24, "retired instructions since last read"),
    (25, "reset since last read"),
]

RESET_REASON_BITS = [
    (1, "brownout (BORRSTF)"),
    (2, "reset pin (PINRSTF)"),
    (3, "power on / power down (PORRSTF)"),
    (4, "software (SFTRSTF)"),
    (5, "independent watchdog (IWDGRSTF)"),
    (6, "window watchdog (WWDGRSTF)"),
    (7, "low power (LPWRRSTF)"),
]

MEADOW_FLAG_BITS = [
    (8, "restart initiated by host command"),
    (9, "route trace messages to host"),
    (10, "route trace messages to UART"),
    (11, "user requested Mono enable"),
    (12, "Mono lockup on last run"),
    (13, "Mono debugging start"),
    (14, "route profiler bins to UART"),
    (15, "last start was a reboot"),
]

FAULT_PHASE_BITS = [
    (0, "component errored"), (1, "phase 1 started"), (2, "phase 1 completed"),
    (3, "phase 2 started"), (4, "phase 2 completed"), (5, "file error"),
]

def _bits(value, table):
    return [text for bit, text in table if value & (1 << bit)]

def _print_bits(label, value, table):
    names = _bits(value, table)
    print("%-6s 0x%08x  %s" % (label, value, ", ".join(names) if names else "-"))

def _write_u32(address, value):
    gdb.selected_inferior().write_memory(address, int(value).to_bytes(4, "little"))

def _backup_register(number):
    return nx_read_u32(RTC_BKR0 + 4 * number)

###########################################################################
#
#   nx_alive
#
###########################################################################

class MeadowAlive(gdb.Command):
    '''
    (Meadow) Is the core running?

    Usage: nx_alive [seconds]       (default 2)

    Prints the DHCSR state (halted, sleeping, lockup) and the system tick count,
    resumes the core for the given time, halts it again and prints the tick count
    and PC.  About 1000 ticks per second means the scheduler tick is running; no
    change means the core is stuck with interrupts off, or in lockup.  The core is
    left halted, as it was.
    '''

    def __init__(self):
        super(MeadowAlive, self).__init__("nx_alive", gdb.COMMAND_STATUS)

    def sample(self, label):
        gdb.execute("maintenance flush register-cache", to_string=True)
        dhcsr = nx_read_u32(DHCSR)
        ticks = int(nx_symbol_value("g_system_ticks"))
        pc = nx_reg(gdb.newest_frame(), "pc")
        name = nx_symbol_name(pc) or "??"
        state = _bits(dhcsr, DHCSR_BITS)
        print("%-6s ticks %10d  pc 0x%08x %-28s DHCSR 0x%08x %s" % (label, ticks, pc, name, dhcsr,
                                                                  ", ".join(state)))
        return ticks, dhcsr

    def invoke(self, arg, from_tty):
        seconds = float(arg) if arg.strip() else 2.0
        before, dhcsr = self.sample("before")
        if dhcsr & (1 << 19):
            print("The core is in LOCKUP (a fault while handling a fault, or in a fault handler).")
        gdb.execute("monitor resume", to_string=True)
        time.sleep(seconds)
        gdb.execute("monitor halt", to_string=True)
        after, dhcsr = self.sample("after")
        rate = (after - before) / seconds
        print("%d ticks in %.1f s (%.0f per second, 1000 expected)" % (after - before, seconds, rate))
        if after == before:
            print("NOT RUNNING: the tick count did not change.")
        elif rate < 500:
            print("The tick is running slowly; interrupts may be blocked for long periods.")

MeadowAlive()

###########################################################################
#
#   nx_reset
#
###########################################################################

class MeadowReset(gdb.Command):
    '''
    (Meadow) Reset reason, reset counters and the Meadow battery-backed flags.

    BK30R holds the reset reason the bootloader copied from RCC_CSR (top byte, the
    MEADOW_OS_RESET_* values in meadow_os.h; 0x14 = reset pin + software).  BK31R
    holds the Meadow flags and the syslog level, BK29R the fault logging status
    of the last fault (cleared once the OS has reported it).
    '''

    def __init__(self):
        super(MeadowReset, self).__init__("nx_reset", gdb.COMMAND_STATUS)

    def invoke(self, arg, from_tty):
        reason_register = _backup_register(BBR_RESET_REASON)
        reason = (reason_register >> 24) & 0xff
        print("Reset reason   BK30R 0x%08x -> code 0x%02x: %s" %
              (reason_register, reason, ", ".join(_bits(reason, RESET_REASON_BITS)) or "none recorded"))
        csr = nx_read_u32(RCC_CSR)
        print("RCC_CSR        0x%08x -> flags 0x%02x: %s" %
              (csr, csr >> 24, ", ".join(_bits(csr >> 24, RESET_REASON_BITS)) or "cleared"))
        resets = nx_symbol_value("_reset_cycle_count")
        power = nx_symbol_value("_power_cycle_count")
        if (resets is not None) and (power is not None):
            print("Counters       reset count %d, power cycle count %d" % (int(resets), int(power)))
        flags = _backup_register(BBR_MEADOW_FLAGS)
        print("Meadow flags   BK31R 0x%08x -> syslog level %d; %s" %
              (flags, flags & 0xff, ", ".join(_bits(flags, MEADOW_FLAG_BITS)) or "no flags"))
        fault = _backup_register(BBR_FAULT_SOURCE)
        if fault == 0:
            print("Fault status   BK29R 0x00000000 (none pending)")
        else:
            print("Fault status   BK29R 0x%08x -> OS: %s; runtime: %s" %
                  (fault, ", ".join(_bits(fault & 0xff, FAULT_PHASE_BITS)) or "-",
                   ", ".join(_bits((fault >> 8) & 0xff, FAULT_PHASE_BITS)) or "-"))

MeadowReset()

###########################################################################
#
#   nx_fault
#
###########################################################################

class MeadowFault(gdb.Command):
    '''
    (Meadow) Fault report: fault status registers decoded, why the core halted, the
    interrupted context and its backtrace.

    Use it when the core has stopped in a fault handler (arm_hardfault, arm_busfault,
    ...) or on a fault vector caught by "monitor cortex_m vector_catch".
    '''

    def __init__(self):
        super(MeadowFault, self).__init__("nx_fault", gdb.COMMAND_STATUS)

    def invoke(self, arg, from_tty):
        ipsr = nx_in_exception()
        print("Exception      %s (IPSR %d)" % (nx_exception_name(ipsr), ipsr))
        cfsr = nx_read_u32(SCB_CFSR)
        hfsr = nx_read_u32(SCB_HFSR)
        _print_bits("CFSR", cfsr, CFSR_BITS)
        _print_bits("HFSR", hfsr, HFSR_BITS)
        if cfsr & (1 << 7):
            print("MMFAR  0x%08x  %s" % (nx_read_u32(SCB_MMFAR), nx_symbol_name(nx_read_u32(SCB_MMFAR)) or ""))
        if cfsr & (1 << 15):
            print("BFAR   0x%08x  %s" % (nx_read_u32(SCB_BFAR), nx_symbol_name(nx_read_u32(SCB_BFAR)) or ""))
        print("AFSR   0x%08x" % nx_read_u32(SCB_AFSR))
        _print_bits("DFSR", nx_read_u32(SCB_DFSR), DFSR_BITS)
        demcr = nx_read_u32(DEMCR)
        catches = [name for bit, name in DEMCR_VECTOR_CATCH if demcr & (1 << bit)]
        print("Vector catch   %s" % (", ".join(catches) if catches else "off"))
        shcsr = nx_read_u32(SCB_SHCSR)
        enabled = [name for bit, name in ((16, "MemManage"), (17, "BusFault"), (18, "UsageFault"))
                   if shcsr & (1 << bit)]
        print("SHCSR  0x%08x  enabled: %s (the others escalate to HardFault)" %
              (shcsr, ", ".join(enabled) if enabled else "none"))
        if ipsr == 0:
            print("\nThe core is in thread mode, so there is no interrupted context.")
            return
        context = nx_interrupted_context()
        if context is None:
            print("\nNo interrupted context found.")
            return
        regs = context["regs"]
        print("\nInterrupted context (%s at 0x%08x):" % (context["source"], context["address"]))
        for name in ("pc", "lr"):
            print("  %-3s 0x%08x  %s" % (name.upper(), regs[name], nx_symbol_name(regs[name] & ~1) or "??"))
        nx_print_regs(regs, NX_CORE_REGS)
        print("\nBacktrace:")
        gdb.execute("nx_bt nomanaged")

MeadowFault()

###########################################################################
#
#   nx_mpu
#
###########################################################################

MPU_AP = {
    0: "no access",
    1: "priv RW, user none",
    2: "priv RW, user RO",
    3: "RW",
    5: "priv RO, user none",
    6: "RO",
    7: "RO",
}

def _memory_ranges():
    '''(start, end, name) for the memory areas MPU regions are named after.'''
    ranges = []
    for start_name, end_name, label in (("_stext", "_etext", "kernel text"),
                                        ("_sdata", "_ebss", "kernel data/bss")):
        start = nx_eval("(unsigned long)&%s" % start_name)
        end = nx_eval("(unsigned long)&%s" % end_name)
        if (start is not None) and (end is not None):
            ranges.append((int(start), int(end), label))
    userspace = nx_symbol_value("userspace")
    if userspace is not None:
        ranges.append((int(userspace["us_textstart"]), int(userspace["us_textend"]), "user text"))
        ranges.append((int(userspace["us_datastart"]), int(userspace["us_bssend"]), "user data/bss"))
    for variable, label in (("g_kmmheap", "kernel heap"), ("g_mmheap", "user heap")):
        heap = nx_symbol_value(variable)
        if (heap is not None) and (int(heap) != 0):
            heap = heap.dereference()
            for region in range(int(heap["mm_nregions"])):
                ranges.append((int(heap["mm_heapstart"][region]), int(heap["mm_heapend"][region]),
                               "%s region %d" % (label, region)))
    ranges += [
        (0x00000000, 0x00010000, "ITCM RAM"),
        (0x08000000, 0x08200000, "internal flash"),
        (0x20000000, 0x20020000, "DTCM SRAM"),
        (0x20020000, 0x2007C000, "SRAM1"),
        (0x2007C000, 0x20080000, "SRAM2"),
        (0x40000000, 0x60000000, "peripherals"),
        (0x90000000, 0xA0000000, "QSPI flash"),
        (0xC0000000, 0xC2000000, "SDRAM"),
        (0xE0000000, 0xE0100000, "system (PPB)"),
    ]
    return ranges

class MeadowMpu(gdb.Command):
    '''
    (Meadow) Show the MPU regions: base, size, access, attributes, and the memory
    areas each region covers.

    Reading a region requires writing its number to MPU_RNR; the original value is
    restored afterwards.
    '''

    def __init__(self):
        super(MeadowMpu, self).__init__("nx_mpu", gdb.COMMAND_STATUS)

    def invoke(self, arg, from_tty):
        count = (nx_read_u32(MPU_TYPE) >> 8) & 0xff
        ctrl = nx_read_u32(MPU_CTRL)
        print("MPU: %d regions, CTRL 0x%x (%s, %s, %s)" % (
            count, ctrl, "enabled" if ctrl & 1 else "DISABLED",
            "on in faults (HFNMIENA)" if ctrl & 2 else "off in faults",
            "default map for privileged code (PRIVDEFENA)" if ctrl & 4 else "no default map"))
        ranges = _memory_ranges()
        original = nx_read_u32(MPU_RNR)
        rows = []
        try:
            for region in range(count):
                _write_u32(MPU_RNR, region)
                rows.append((region, nx_read_u32(MPU_RBAR), nx_read_u32(MPU_RASR)))
        finally:
            _write_u32(MPU_RNR, original)
        for region, rbar, rasr in rows:
            if not (rasr & 1):
                continue
            size = 1 << (((rasr >> 1) & 0x1f) + 1)
            base = rbar & ~0x1f
            end = base + size
            srd = (rasr >> 8) & 0xff
            attributes = "TEX%d C%d B%d S%d" % ((rasr >> 19) & 7, (rasr >> 17) & 1, (rasr >> 16) & 1,
                                                (rasr >> 18) & 1)
            access = MPU_AP.get((rasr >> 24) & 7, "reserved")
            xn = " XN" if rasr & (1 << 28) else ""
            covered = [name for start, stop, name in ranges if (start < end) and (stop > base)]
            print("%2d 0x%08x-0x%08x %8s %-20s %s%s%s  %s" % (
                region, base, end - 1, self.size_text(size), access, attributes, xn,
                " SRD 0x%02x" % srd if srd else "", ", ".join(covered[:3])))

    @staticmethod
    def size_text(size):
        if size >= (1 << 20):
            return "%dM" % (size >> 20)
        if size >= (1 << 10):
            return "%dK" % (size >> 10)
        return "%d" % size

MeadowMpu()

###########################################################################
#
#   nx_irqs
#
###########################################################################

class MeadowIrqs(gdb.Command):
    '''
    (Meadow) NVIC state of the interrupts.

    Usage: nx_irqs          enabled, pending or active interrupts
           nx_irqs all      every interrupt with a handler attached

    Shows the priority, the NuttX handler from g_irqvector and the interrupt
    name from STM32F777.svd (when Svd.py is loaded).
    '''

    def __init__(self):
        super(MeadowIrqs, self).__init__("nx_irqs", gdb.COMMAND_STATUS)

    def invoke(self, arg, from_tty):
        show_all = (arg.strip() == "all")
        vector = nx_symbol_value("g_irqvector")
        count = vector.type.range()[1] + 1 if vector is not None else 16 + 240
        external = count - 16
        words = (external + 31) // 32
        enabled = [nx_read_u32(NVIC_ISER + 4 * word) for word in range(words)]
        pending = [nx_read_u32(NVIC_ISPR + 4 * word) for word in range(words)]
        active = [nx_read_u32(NVIC_IABR + 4 * word) for word in range(words)]
        priorities = bytes(gdb.selected_inferior().read_memory(NVIC_IPR, external))
        shpr = bytes(gdb.selected_inferior().read_memory(SCB_SHPR1, 12))
        shcsr = nx_read_u32(SCB_SHCSR)
        print("Current exception: %s" % nx_exception_name(nx_in_exception()))
        print("%4s %-24s %-4s %-4s %-4s %4s  %s" % ("IRQ", "Name", "EN", "PEND", "ACT", "PRI", "Handler"))
        for number in (4, 5, 6, 11, 12, 14, 15):
            handler = self.handler(vector, number)
            print("%4s %-24s %-4s %-4s %-4s %4d  %s" % (
                "-", nx_exception_name(number), "", "", "", shpr[number - 4], handler))
        interrupt_name = globals().get("svd_interrupt_name")
        for irq in range(external):
            word, bit = divmod(irq, 32)
            is_enabled = bool(enabled[word] & (1 << bit))
            is_pending = bool(pending[word] & (1 << bit))
            is_active = bool(active[word] & (1 << bit))
            handler = self.handler(vector, irq + 16)
            if not (is_enabled or is_pending or is_active or (show_all and handler != "irq_unexpected_isr")):
                continue
            name = (interrupt_name(irq) if interrupt_name else None) or ""
            print("%4d %-24s %-4s %-4s %-4s %4d  %s" % (
                irq, name, "yes" if is_enabled else "", "yes" if is_pending else "",
                "yes" if is_active else "", priorities[irq], handler))
        print("SHCSR active/pending: %s" % ", ".join(
            name for bit, name in ((0, "MemManage active"), (1, "BusFault active"), (3, "UsageFault active"),
                                   (7, "SVCall active"), (8, "DebugMonitor active"), (10, "PendSV active"),
                                   (11, "SysTick active"), (12, "UsageFault pending"),
                                   (13, "MemManage pending"), (14, "BusFault pending"), (15, "SVCall pending"))
            if shcsr & (1 << bit)) or "-")

    @staticmethod
    def handler(vector, number):
        if (vector is None) or (number > vector.type.range()[1]):
            return "?"
        address = int(vector[number]["handler"])
        if address == 0:
            return "-"
        return nx_symbol_name(address & ~1) or "0x%08x" % address

MeadowIrqs()

###########################################################################
#
#   espcp_status
#
###########################################################################

ESPCP_FUNCTION_ENUMS = {
    "espcp_esp32_interfaces_wi_fi": "enum espcp_wi_fi_function",
    "espcp_esp32_interfaces_wired_ethernet": "enum espcp_wi_fi_function",
    "espcp_esp32_interfaces_system": "enum espcp_system_function",
    "espcp_esp32_interfaces_transport": "enum espcp_transport_function",
    "espcp_esp32_interfaces_blue_tooth": "enum espcp_bluetooth_function",
}

def _enum_name(type_name, value, prefix=""):
    '''Name of value in an enum from the debug information, or the number.'''
    try:
        for field in gdb.lookup_type(type_name).fields():
            if field.enumval == value:
                return field.name[len(prefix):] if field.name.startswith(prefix) else field.name
    except gdb.error:
        pass
    return "%d" % value

def _espcp_static(name):
    '''A static variable of the espcp code (kernel), or None.'''
    return nx_symbol_value(name)

def _queue_state(file_value, tasks):
    '''"n of max messages, waiting: ..." for a struct file of a message queue.'''
    inode = file_value["f_inode"]
    if int(inode) == 0:
        return "not open"
    msgq = inode.dereference()["i_private"].cast(gdb.lookup_type("struct mqueue_inode_s").pointer())
    if int(msgq) == 0:
        return "no queue"
    queue = msgq.dereference()
    waiting = [name for name, waitobj in tasks if waitobj == int(msgq)]
    try:
        name = inode.dereference()["i_name"].address.cast(gdb.lookup_type("char").pointer()).string()
    except gdb.error:
        name = "?"
    return "%-18s %d of %d messages%s" % (name, int(queue["nmsgs"]), int(queue["maxmsgs"]),
                                          ", waited on by " + ", ".join(waiting) if waiting else "")

def _espcp_message_text(message, tasks):
    '''One line describing an espcp_message_t pointer.'''
    message = message.cast(gdb.lookup_type("espcp_message_t").pointer()).dereference()
    interface = _enum_name("enum espcp_esp32_interfaces", int(message["interface"]))
    function_enum = ESPCP_FUNCTION_ENUMS.get(interface)
    function = _enum_name(function_enum, int(message["function"])) if function_enum else "%d" % int(message["function"])
    text = "id %u %s %s.%s status %s" % (
        int(message["message_id"]),
        _enum_name("enum espcp_message_types", int(message["message_type"]), "espcp_message_types_"),
        interface.replace("espcp_esp32_interfaces_", ""),
        function.replace("espcp_wi_fi_function_", "").replace("espcp_system_function_", "")
                .replace("espcp_transport_function_", "").replace("espcp_bluetooth_function_", ""),
        _enum_name("enum espcp_status_codes", int(message["status_code"]), "espcp_status_codes_"))
    semaphore = int(message["semaphore"])
    waiting = [name for name, waitobj in tasks if semaphore and (waitobj == semaphore)]
    if waiting:
        text += ", waited on by " + ", ".join(waiting)
    return text

class EspcpStatus(gdb.Command):
    '''
    (Meadow) State of the ESP32 coprocessor link (F7MeadowOS/espcp).

    Shows the mode and threads, the queues and who waits on them, the requests
    waiting for a response from the ESP32 (and the tasks blocked on them), and the
    error counters.  A request that stays in the waiting list across several
    samples while its task is still blocked points at a lost response.
    '''

    def __init__(self):
        super(EspcpStatus, self).__init__("espcp_status", gdb.COMMAND_STATUS)

    def invoke(self, arg, from_tty):
        configuration = _espcp_static("g_espcp_configuration")
        if (configuration is None) or (int(configuration) == 0):
            raise gdb.GdbError("g_espcp_configuration is not set (espcp not started?).")
        tasks = [("%s(%d)" % (tcb.dereference()["name"].string(), int(tcb.dereference()["pid"])),
                  int(tcb.dereference()["waitobj"])) for tcb in nx_tcbs()]
        config = configuration.dereference()
        print("Mode %s; main thread %s (pid %d); event thread %s (pid %d); ESP32 not responding: %s; "
              "expecting reset: %s" % (
                  str(config["current_mode"]).replace("espcp_mode_", ""),
                  "running" if bool(config["thread_running"]) else "stopped", int(config["thread"]),
                  "running" if bool(config["incoming_event_handler_thread_running"]) else "stopped",
                  int(config["incoming_event_thread"]),
                  "YES" if bool(config["esp_not_responding"]) else "no",
                  "yes" if bool(config["expecting_reset"]) else "no"))
        for label, field in (("request queue", "request_queue"), ("managed event queue", "managed_event_queue"),
                             ("incoming event queue", "incoming_event_queue")):
            print("  %-22s %s" % (label, _queue_state(config[field], tasks)))
        for label, name in (("dispatcher queue", "g_message_queue"),
                            ("non-blocking requests", "g_request_queue_nonblocking")):
            value = _espcp_static(name)
            if value is not None:
                print("  %-22s %s" % (label, _queue_state(value, tasks)))
        counters = []
        for label, name in (("last message id", "g_last_message_id"),
                            ("responses pending send", "g_send_responses_pending"),
                            ("get-message failures", "g_get_message_failures"),
                            ("response timeouts", "g_espcp_response_timeouts"),
                            ("events with payloads", "_events_with_payloads_count")):
            value = _espcp_static(name)
            if value is not None:
                counters.append("%s %d" % (label, int(value)))
        for label, name in (("SPI ready timed out", "g_spi_ready_timed_out"),
                            ("core dump in progress", "g_core_dump_in_progress")):
            value = _espcp_static(name)
            if (value is not None) and bool(value):
                counters.append(label)
        print("Counters: %s" % ", ".join(counters))
        #
        #   g_request_response_message is a permanent message, reused every time the
        #   dispatcher asks the ESP32 for a response; its id is the last one used.
        #
        current = _espcp_static("g_request_response_message")
        if (current is not None) and (int(current) != 0):
            message_id = int(current.dereference()["message_id"])
            print("Last send-response request: id 0x%08x" % message_id)
        waiting = _espcp_static("g_messages_waiting_for_a_response")
        lists = []
        if (waiting is not None) and (int(waiting) != 0):
            lists.append(("waiting for a response", waiting.dereference()))
        lists.append(("pending response (configuration)", config["messages_pending_response"]))
        for label, linked_list in lists:
            item = linked_list["head"]
            lines = []
            while (int(item) != 0) and (len(lines) < 64):
                lines.append(_espcp_message_text(item.dereference()["data"], tasks))
                item = item.dereference()["child"]
            print("Messages %s: %d" % (label, len(lines)))
            for line in lines:
                print("  %s" % line)

EspcpStatus()
