#
#   GDB start-up commands for the CLion "GDB Remote Debug" configuration.
#
#   This is a cut-down copy of .gdbinit in this directory.  CLion connects to the OpenOCD server
#   itself, so this file does not run "target extended-remote" or any monitor commands (they need
#   a connection and would stop the rest of this file from running).
#
#   GDB must run this file with this directory as its working directory.  A local (uncommitted)
#   .gdbinit in the workspace root does that:
#
#     cd <workspace root>/gdb
#     source clion.gdbinit
#
#   and ~/.gdbinit must allow GDB to load it:
#
#     add-auto-load-safe-path <workspace root>/.gdbinit
#
#   Once CLion is connected, these can be entered in the GDB console if required:
#
#     mon gdb breakpoint_override hard
#     monitor cortex_m vector_catch all
#
set output-radix 16
set mem inaccessible-by-default off
set remote hardware-breakpoint-limit 8
set remote hardware-watchpoint-limit 4
set confirm off
set pagination off
#
#   OpenOCD's CRC and NuttX thread reads can take a few seconds over the ST-Link.
#
set remotetimeout 10

#
#   The NuttX Python scripts import packages from the workspace .venv (created by debug.sh).
#
python
import glob, os, sys
for p in glob.glob(os.path.abspath('../.venv/lib/python*/site-packages')):
    if p not in sys.path:
        sys.path.insert(0, p)
end

#
#   Symbols: the kernel, then the user image and bootloader when they exist.
#
file -readnow ../nuttx/nuttx.elf
shell if test -f ../bootloader/Debug/Meadow.BL.elf; then echo add-symbol-file -readnow ../bootloader/Debug/Meadow.BL.elf; fi > /tmp/meadow_clion_gdb
shell if test -f ../nuttx/nuttx_user.elf; then echo add-symbol-file -readnow ../nuttx/nuttx_user.elf; fi >> /tmp/meadow_clion_gdb
source /tmp/meadow_clion_gdb

define dmesg
  nx_dmesg
end

define enable_hardfault
  set *((uint32_t *) 0xe000edfc) |= 0x0000400
end

#
#   The scripts must be loaded after the symbols as they refer to NuttX symbols.
#
source Nuttx.py
source Nuttx_Tasks.py
source NuttxHeap.py
source Tracing.py
source ../nuttx/tools/pynuttx/gdbinit.py
