#
#   GDB start-up file for CLion.  Copy it to .gdbinit in the workspace root (it is not committed
#   there):
#
#     cp tools/clion/workspace.gdbinit .gdbinit
#
#   GDB auto-loads it when started in the workspace root, as CLion does; ~/.gdbinit must allow
#   it with
#
#     add-auto-load-safe-path <workspace root>/.gdbinit
#
#   gdb-dashboard, if ~/.gdbinit loads it, is turned off: CLion shows its own views and the
#   dashboard would redraw into the GDB console at every stop.
#
python
try:
    gdb.execute("dashboard -enabled off", to_string=True)
except gdb.error:
    pass
end
cd gdb
source clion.gdbinit
