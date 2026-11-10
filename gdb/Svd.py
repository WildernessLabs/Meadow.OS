#
#   Peripheral registers from the CMSIS-SVD description of the STM32F777
#   (STM32F777.svd in this directory).
#
#   Commands:
#
#       svd                         List the peripherals.
#       svd <PERIPHERAL>            Read every register of a peripheral.
#       svd <PERIPHERAL> <REGISTER> Read one register and decode its fields.
#
#   Registers are only read, never written.  Write-only registers are skipped,
#   and so are data registers whose read has a side effect (reading a USART RDR,
#   SPI or QUADSPI DR pops a FIFO or clears a status flag) unless the register
#   is named on the command line.
#
#   The parsed description is also used by Meadow.py (nx_irqs) for IRQ names.
#
import os
import xml.etree.ElementTree as ElementTree

import gdb

def _svd_find():
    '''
    Path of STM32F777.svd: next to this script, in the current directory (GDB is
    started in gdb/ by debug.sh) or in gdb/ next to the loaded nuttx directory.
    __file__ is only reliable when this file is sourced directly by GDB.
    '''
    candidates = []
    if "__file__" in globals():
        candidates.append(os.path.dirname(os.path.abspath(__file__)))
    candidates.append(os.getcwd())
    filename = gdb.current_progspace().filename
    if filename:
        candidates.append(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(filename))), "gdb"))
    for directory in candidates:
        path = os.path.join(directory, "STM32F777.svd")
        if os.path.isfile(path):
            return path
    return os.path.join(candidates[0], "STM32F777.svd")

SVD_PATH = _svd_find()

#
#   Registers whose read changes the peripheral's state.
#
SVD_SIDE_EFFECT_READS = ("DR", "RDR", "RXDR", "FIFO")

def _svd_int(text, default=None):
    if text is None:
        return default
    text = text.strip().lower()
    if text.startswith("#"):
        return int(text[1:].replace("x", "0"), 2)
    return int(text, 0)

class SvdField(object):
    def __init__(self, name, offset, width, description, access):
        self.name = name
        self.offset = offset
        self.width = width
        self.description = description
        self.access = access

    def extract(self, value):
        return (value >> self.offset) & ((1 << self.width) - 1)

class SvdRegister(object):
    def __init__(self, name, offset, size, access, description, fields):
        self.name = name
        self.offset = offset
        self.size = size
        self.access = access
        self.description = description
        self.fields = fields

    @property
    def readable(self):
        return self.access != "write-only"

    @property
    def side_effect_read(self):
        return self.name in SVD_SIDE_EFFECT_READS

class SvdPeripheral(object):
    def __init__(self, name, base, description, registers):
        self.name = name
        self.base = base
        self.description = description
        self.registers = registers

    def register(self, name):
        for register in self.registers:
            if register.name.upper() == name.upper():
                return register
        return None

class SvdDevice(object):
    '''The peripherals and interrupts of an SVD file.'''

    def __init__(self, path):
        root = ElementTree.parse(path).getroot()
        default_size = _svd_int(root.findtext("size"), 32)
        default_access = root.findtext("access") or "read-write"
        elements = dict((element.findtext("name"), element) for element in root.iter("peripheral"))
        self.peripherals = {}
        self.interrupts = {}
        for name, element in elements.items():
            source = element
            base_name = element.get("derivedFrom")
            if base_name and (base_name in elements) and (element.find("registers") is None):
                source = elements[base_name]
            description = (element.findtext("description") or source.findtext("description") or "").strip()
            registers = self._registers(source, default_size, default_access)
            peripheral = SvdPeripheral(name, _svd_int(element.findtext("baseAddress")), " ".join(description.split()),
                                       registers)
            self.peripherals[name.upper()] = peripheral
            for interrupt in element.findall("interrupt"):
                self.interrupts[_svd_int(interrupt.findtext("value"))] = interrupt.findtext("name")

    @staticmethod
    def _registers(element, default_size, default_access):
        size = _svd_int(element.findtext("size"), default_size)
        access = element.findtext("access") or default_access
        result = []
        registers = element.find("registers")
        if registers is None:
            return result
        for register in registers.iter("register"):
            fields = []
            for field in register.iter("field"):
                if field.findtext("bitOffset") is not None:
                    offset = _svd_int(field.findtext("bitOffset"))
                    width = _svd_int(field.findtext("bitWidth"), 1)
                elif field.findtext("lsb") is not None:
                    offset = _svd_int(field.findtext("lsb"))
                    width = _svd_int(field.findtext("msb")) - offset + 1
                else:
                    msb, lsb = field.findtext("bitRange").strip("[]").split(":")
                    offset = int(lsb)
                    width = int(msb) - offset + 1
                fields.append(SvdField(field.findtext("name"), offset, width,
                                       " ".join((field.findtext("description") or "").split()),
                                       field.findtext("access")))
            fields.sort(key=lambda field: field.offset, reverse=True)
            result.append(SvdRegister(register.findtext("name"), _svd_int(register.findtext("addressOffset")),
                                      _svd_int(register.findtext("size"), size),
                                      register.findtext("access") or access,
                                      " ".join((register.findtext("description") or "").split()), fields))
        result.sort(key=lambda register: register.offset)
        return result

_svd_device = None

def svd_device():
    '''The parsed SVD file (parsed on first use), or None if it is missing.'''
    global _svd_device
    if (_svd_device is None) and os.path.isfile(SVD_PATH):
        _svd_device = SvdDevice(SVD_PATH)
    return _svd_device

def svd_interrupt_name(irq):
    '''Name of external interrupt number irq (0 = first after the system exceptions), or None.'''
    device = svd_device()
    if device is None:
        return None
    return device.interrupts.get(irq)

def svd_read(address, size):
    data = bytes(gdb.selected_inferior().read_memory(address, size // 8))
    return int.from_bytes(data, "little")

class SvdCommand(gdb.Command):
    '''
    Read peripheral registers using the STM32F777 SVD description.

    Usage: svd                          list the peripherals
           svd <PERIPHERAL>             read every register of a peripheral
           svd <PERIPHERAL> <REGISTER>  read one register and decode its fields

    Write-only registers are not read, nor are data registers whose read has a
    side effect (DR, RDR, RXDR, FIFO) unless they are named.
    '''

    def __init__(self):
        super(SvdCommand, self).__init__("svd", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        device = svd_device()
        if device is None:
            raise gdb.GdbError("SVD file not found: %s" % SVD_PATH)
        words = arg.split()
        if len(words) == 0:
            for name in sorted(device.peripherals):
                peripheral = device.peripherals[name]
                print("%-12s 0x%08x  %s" % (peripheral.name, peripheral.base, peripheral.description))
            return
        peripheral = device.peripherals.get(words[0].upper())
        if peripheral is None:
            raise gdb.GdbError("No peripheral %s; run svd for the list." % words[0])
        if len(words) == 1:
            print("%s at 0x%08x: %s" % (peripheral.name, peripheral.base, peripheral.description))
            for register in peripheral.registers:
                address = peripheral.base + register.offset
                if not register.readable:
                    print("  %-12s 0x%08x  (write-only)" % (register.name, address))
                elif register.side_effect_read:
                    print("  %-12s 0x%08x  (not read: reading it has side effects; name it to read it)" %
                          (register.name, address))
                else:
                    try:
                        value = svd_read(address, register.size)
                        print("  %-12s 0x%08x = 0x%0*x  %s" % (register.name, address, register.size // 4, value,
                                                               register.description))
                    except gdb.MemoryError as e:
                        print("  %-12s 0x%08x  %s" % (register.name, address, e))
            return
        register = peripheral.register(words[1])
        if register is None:
            raise gdb.GdbError("No register %s in %s." % (words[1], peripheral.name))
        address = peripheral.base + register.offset
        if not register.readable:
            raise gdb.GdbError("%s.%s is write-only." % (peripheral.name, register.name))
        value = svd_read(address, register.size)
        print("%s.%s at 0x%08x = 0x%0*x  %s" % (peripheral.name, register.name, address, register.size // 4, value,
                                               register.description))
        for field in register.fields:
            if field.width == 1:
                bits = "[%d]" % field.offset
            else:
                bits = "[%d:%d]" % (field.offset + field.width - 1, field.offset)
            print("  %-12s %-8s = 0x%x  %s" % (field.name, bits, field.extract(value), field.description))

SvdCommand()
