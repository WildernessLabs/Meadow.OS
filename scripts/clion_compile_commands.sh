#!/bin/bash
#
#   Create compile_commands.json in the workspace root for CLion (or any clangd-based editor).
#
#   A full clean build is run under bear (brew install bear) so that every source file is
#   compiled and recorded.  Any options are passed on to build.sh, for example:
#
#     scripts/clion_compile_commands.sh --unittests=esp
#
#   Source paths are then resolved through symbolic links (F7MeadowOSLink) so the entries name
#   the files under F7MeadowOS/ that are edited.
#
#   Run it again after a configuration change (build.sh options, defconfig, Kconfig).
#
scriptdir="$( cd "$(dirname "$0")" ; pwd -P )"
rootdir="$( cd "$scriptdir/.." ; pwd -P )"

if ! command -v bear > /dev/null 2>&1; then
  echo "bear is not installed (brew install bear)."
  exit 1
fi

cd "$rootdir" || exit 1

bear --output "$rootdir/compile_commands.raw.json" -- ./build.sh --clean --force "$@"
status=$?
if [ $status -ne 0 ]; then
  echo "Build failed (exit $status); compile_commands.json not updated."
  exit $status
fi

python3 -I - "$rootdir/compile_commands.raw.json" "$rootdir/compile_commands.json" << 'EOF'
import json
import os
import sys

with open(sys.argv[1]) as f:
    raw = json.load(f)

#
#   Entries for configure and CMake try-compile tests are dropped: their file or working
#   directory no longer exists, or they were run from a CMakeFiles directory.
#
entries = []
for e in raw:
    if (not os.path.isdir(e['directory'])) or ('/CMakeFiles/' in (e['directory'] + '/')):
        continue
    path = e['file']
    if not os.path.isabs(path):
        path = os.path.join(e['directory'], path)
    if not os.path.exists(path):
        continue
    real = os.path.realpath(path)
    #
    #   The source file argument must name the same path as 'file' or CLion treats it as a second
    #   input and cannot collect the compiler information.
    #
    args = e['arguments']
    for i in range(1, len(args)):
        a = args[i]
        if a.startswith('-') or (args[i - 1] in ('-o', '-MF', '-MT', '-MQ', '-include', '-I', '-isystem', '-x')):
            continue
        p = a if os.path.isabs(a) else os.path.join(e['directory'], a)
        if os.path.realpath(p) == real:
            args[i] = real
    e['file'] = real
    if 'output' in e and not os.path.isabs(e['output']):
        e['output'] = os.path.join(e['directory'], e['output'])
    entries.append(e)

with open(sys.argv[2], 'w') as f:
    json.dump(entries, f, indent=2)

print('compile_commands.json: %d entries' % len(entries))
EOF
