#!/bin/bash

#set -e
scriptdir="$( cd "$(dirname "$0")" ; pwd -P )"

# printf "debug>-scriptdir: ( $scriptdir ).\n"

. $scriptdir/scripts/common_methods.sh
. $scriptdir/scripts/debug_methods.sh

set_os_name
check_if_interactive

set_openocd_variables

stutil="$scriptdir/stlink/build/Release/src/gdbserver/st-util"

VERBOSE=false
FORCE=false
OCD=true
MI=
QEMU=
GDB_SERVER_PORT=4242
LLDB=
ESP=

for i in "$@"
do
case $i in
    -v|--verbose)
    VERBOSE=true
    ;;
    -f|--force)
    FORCE=true
    ;;
    -s|-server|--server)
    SERVER=true
    ;;
    -stlink|--stlink)
    OCD=false
    ;;
    -ocd|--ocd|-openocd|--openocd)
    OCD=true
    ;;
    --mi)
    MI=--interpreter=mi
    ;;
    --qemu)
    QEMU=true
    ;;
    --lldb)
    LLDB='lldb --'
    ;;
    --gdb)
    GDB='-S -gdb tcp::$GDB_SERVER_PORT'
    ;;
    --esp)
    ESP=true
    ;;
    *)
    # unknown option
    ;;
esac
done

#
# Launch QEMU debug server if in QEMU mode.
#
QEMU_BIN_ARM="qemu/build/arm-softmmu/qemu-system-arm"
QEMU_BIN_XTENSA="qemu-esp32/build/xtensa-softmmu/qemu-system-xtensa"

function launchQEMUXtensa {
  if [ ! -r "$scriptdir/$QEMU_BIN_XTENSA" ]; then
    printf "${red}Error:${reset} QEMU could not be found at: $QEMU_BIN_XTENSA$\n"
    exit 0
  fi

  printf "QEMU server is now up.\n"

  QEMU_BOOT_BIN=$scriptdir/Meadow-ESP32/Source/MeadowComms/MeadowComms-qemu.bin
  $LLDB $scriptdir/$QEMU_BIN_XTENSA \
    -machine esp32,accel=tcg -nographic \
    -drive file=$QEMU_BOOT_BIN,if=mtd,format=raw \
    -chardev stdio,mux=on,id=terminal \
    -serial chardev:terminal \
    -monitor chardev:terminal \
    -d guest_errors,unimp
}

function launchQEMUArm {
  if [ ! -r "$scriptdir/$QEMU_BIN_ARM" ]; then
    printf "${red}Error:${reset} QEMU could not be found at: $QEMU_BIN_ARM$\n"
    exit 0
  fi

  printf "QEMU server is now up.\n"

  FLASH_FILE=$scriptdir/qemu/meadow_qspi_flash.raw
  FLASH_SIZE=32 # TODO: Read from NuttX .config

  if [ ! -r "$FLASH_FILE" ]; then
    printf "Creating RAW filesystem for QSPI flash block device"
    dd if=/dev/zero of=$FLASH_FILE bs=1m count=$FLASH_SIZE
  fi

  QEMU_BOOT_BIN=$scriptdir/nuttx/nuttx.bin
  QEMU_BOOT_ARGS='-bios $QEMU_BOOT_BIN'

  if [ -r "$scriptdir/nuttx/Meadow.OS.bin" ]; then
    QEMU_BOOT_BIN= $scriptdir/nuttx/Meadow.OS.bin
  fi

  $LLDB $scriptdir/$QEMU_BIN_ARM \
    -machine meadow,accel=tcg -nographic \
    -device loader,file=$QEMU_BOOT_BIN \
    -chardev stdio,mux=on,id=terminal \
    -serial chardev:terminal \
    -chardev socket,id=hcom,port=1234,host=0.0.0.0,server,nowait \
    -serial chardev:hcom \
    -monitor chardev:terminal \
    -drive file=$FLASH_FILE,format=raw,if=mtd,id=qspiflash \
    -S -gdb tcp::$GDB_SERVER_PORT \
    -d guest_errors,unimp
}

if [ "$QEMU" = true ] && [ "$SERVER" = true ]; then
  if [ "$ESP" = true ]; then
    launchQEMUXtensa
  else
    launchQEMUArm
  fi
  exit 0
fi

# printf "debug>Launching GDB server\n"

#
# Launch the GDB debug server if passed the --server flag.
#

if [ "$SERVER" = true ] ; then
  if [ "$OCD" = true ] ; then
    exec "$MEADOW_OPENOCD_COMMAND" "-s${MEADOW_OPENOCD_SCRIPT_DIR}" "-f$scriptdir/debug.cfg"
  else
    exec $stutil -v -m --semihosting
  fi
fi

#
#   Check if .gdbinit is available.
#

printf "Launched GDB\n"

if [ ! -r "$scriptdir/gdb/.gdbinit" ] || $FORCE; then
    printf "Missing .gdbinit GDB configuration file.\n"
    exit 1
fi

nc -z localhost $GDB_SERVER_PORT &> /dev/null
if [ $? -ne 0 ]; then
    printf "${red}Error:${reset} GDB debugger server is not running.\n"
    printf "Run this command in another terminal: ${bold}./debug.sh --server${reset}\n"
    exit 1
fi

# 
# Python scripting is in /Meadow/gdb
# 
cd $scriptdir/gdb

#
# Activate a virtual environment compatible with GDB's embedded Python.
#
if [[ "$OS" == "mac" ]]; then
  GDB_BIN=arm-none-eabi-gdb
else
  GDB_BIN=gdb-multiarch
fi

gdb_python_mm=$($GDB_BIN -q -nx -ex 'python import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")' -ex quit 2>/dev/null | tail -n 1 | tr -d '\r')
if [[ ! "$gdb_python_mm" =~ ^[0-9]+\.[0-9]+$ ]]; then
  gdb_python_mm=$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')
fi

venv_dir="$scriptdir/.venv"
if [ -x "$venv_dir/bin/python3" ]; then
  venv_python_mm=$($venv_dir/bin/python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')
  if [ "$venv_python_mm" != "$gdb_python_mm" ]; then
    printf "Rebuilding .venv for Python %s (current .venv is %s)\n" "$gdb_python_mm" "$venv_python_mm"
    rm -rf "$venv_dir"
  fi
fi

if [ ! -d "$venv_dir" ]; then
  py_cmd="python${gdb_python_mm}"
  if command -v "$py_cmd" > /dev/null 2>&1; then
    "$py_cmd" -m venv "$venv_dir"
  else
    printf "${red}Error:${reset} Required interpreter %s was not found in PATH.\n" "$py_cmd"
    printf "Install Python %s or update PATH, then run debug again.\n" "$gdb_python_mm"
    exit 1
  fi
fi

venv_python="$venv_dir/bin/python3"
"$venv_python" -m pip install --upgrade pip > /dev/null

missing_modules=$(
  "$venv_python" -c 'import importlib.util; modules=["numpy","matplotlib","debugpy","elftools"]; missing=[m for m in modules if importlib.util.find_spec(m) is None]; print(" ".join(missing))'
)
if [ -n "$missing_modules" ]; then
  # Try pinned deps first; if they are incompatible with this Python version,
  # fall back to unpinned runtime deps used by nxgdb.
  if ! "$venv_python" -m pip install -r "$scriptdir/nuttx/tools/pynuttx/requirements.txt"; then
    printf "Pinned pynuttx requirements failed for Python %s. Falling back to compatible latest packages.\n" "$gdb_python_mm"
  fi

  packages_to_install=()
  for module in $missing_modules; do
    case "$module" in
      elftools)
        packages_to_install+=("pyelftools")
        ;;
      *)
        packages_to_install+=("$module")
        ;;
    esac
  done

  if [ ${#packages_to_install[@]} -gt 0 ]; then
    "$venv_python" -m pip install "${packages_to_install[@]}"
  fi
fi

source "$venv_dir/bin/activate"

# GDB uses embedded Python; add venv site-packages explicitly so nxgdb imports work.
venv_site_packages=$("$venv_python" -c 'import site; print(site.getsitepackages()[0])')
if [ -d "$venv_site_packages" ]; then
  export PYTHONPATH="$venv_site_packages${PYTHONPATH:+:$PYTHONPATH}"
fi

#
#   Launch GDB with Python scripting configurations
#
if [[ "$OS" == "mac" ]]; then
  arm-none-eabi-gdb $MI -q -iex 'set auto-load safe-path /'
elif [[ "$OS" == "linux" ]]; then
  #
  # Launch custom versions from arm of arm-none-eabi-gdb having python baked in
  #
  # printf "Current Working Directory: '$( pwd )'. Launching gdb-multiarch -q -iex 'set auto-load safe-path'/\n"
  gdb-multiarch -q -iex 'set auto-load safe-path /'
else
  printf "Unsupported OS ${bold}$OS${reset}.\n"
fi

