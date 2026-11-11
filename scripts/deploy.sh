#!/bin/bash
#
#   Deploy the current build (OS and Mono runtime together) to a Meadow attached by USB and an
#   ST-Link.  Run from anywhere after build.sh; used by the CLion "Deploy" run configuration.
#
#     1. Disable the runtime while the old OS is still running.
#     2. Flash the OS over SWD (flash.sh).
#     3. Reset over SWD and wait for the Meadow USB port.
#     4. Write the runtime from this build (-f; without it the CLI writes the released runtime).
#        An erase time-out is retried once after disabling the runtime again.
#     5. Delete the staged runtime file, enable the runtime and show the device information.
#
#   Requires MEADOW_OPENOCD_DIR and the meadow CLI on PATH.
#
scriptdir="$( cd "$(dirname "$0")" ; pwd -P )"
rootdir="$( cd "$scriptdir/.." ; pwd -P )"
runtime="$rootdir/nuttx/Meadow.OS.Runtime.bin"

if [ -z "$MEADOW_OPENOCD_DIR" ]; then
  echo "MEADOW_OPENOCD_DIR is not set."
  exit 1
fi

if [ ! -f "$rootdir/nuttx/Meadow.OS.bin" ] || [ ! -f "$runtime" ]; then
  echo "Build outputs are missing; run build.sh first."
  exit 1
fi

#
#   Stop a debug OpenOCD started by scripts/openocd_server.sh; any other server holding the
#   probe has to be stopped by hand.
#
"$scriptdir/openocd_server.sh" stop
if lsof -nP -iTCP:4242 -sTCP:LISTEN > /dev/null 2>&1; then
  echo "An OpenOCD server is running on port 4242; stop it so the probe is free."
  exit 1
fi

#
#   Run a command with a time limit (macOS has no timeout command).
#
limit() {
  local seconds=$1
  shift
  perl -e 'alarm shift; exec @ARGV' "$seconds" "$@"
}

wait_for_meadow() {
  local i
  for i in $(seq 1 60); do
    if meadow port list 2>/dev/null | grep -q "usbmodem"; then
      return 0
    fi
    sleep 1
  done
  return 1
}

cd "$rootdir" || exit 1

echo "== Disabling the runtime"
limit 60 meadow runtime disable

echo "== Flashing the OS"
if ! limit 300 ./flash.sh; then
  echo "flash.sh failed."
  exit 1
fi

echo "== Resetting the board"
limit 60 "$MEADOW_OPENOCD_DIR/src/openocd" -s "$MEADOW_OPENOCD_DIR/tcl" -f interface/stlink.cfg \
  -f target/stm32f7x.cfg -c "init; reset run; sleep 500; shutdown"
if ! wait_for_meadow; then
  echo "The Meadow USB port did not appear after the reset."
  exit 1
fi
sleep 2

echo "== Writing the runtime"
if ! limit 900 meadow firmware write runtime -f "$runtime"; then
  echo "Runtime write failed; disabling the runtime and trying again."
  limit 60 meadow runtime disable
  if ! limit 900 meadow firmware write runtime -f "$runtime"; then
    echo "Runtime write failed again."
    exit 1
  fi
fi

echo "== Enabling the runtime"
limit 60 meadow file delete Meadow.OS.Runtime.bin
limit 60 meadow runtime enable
limit 60 meadow device info
