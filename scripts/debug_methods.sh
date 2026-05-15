#!/bin/bash

#
#   Set the OpenOCD related variable:
#       - openocd program to be used
#       - openocd scripts path
#
#   Assumes set_os_name has already been called.
#
set_openocd_variables() {
  if [[ "$OS" == "mac" ]]; then
    #
    # Custom version of nuttx aware openocd
    #
    if [ -z "$MEADOW_MACOS_OPENOCD_DIR" ]; then
      echo "Environment variable MEADOW_MACOS_OPENOCD_DIR is not set. Please set it to the path where Meadow OpenOCD is located."
      exit -1
    fi
    MEADOW_OPENOCD_COMMAND="${MEADOW_MACOS_OPENOCD_DIR}/src/openocd"
    MEADOW_OPENOCD_SCRIPT_DIR="${MEADOW_MACOS_OPENOCD_DIR}/tcl"
  elif [[ "$OS" == "linux" ]]; then
    MEADOW_OPENOCD_COMMAND="openocd"
    MEADOW_OPENOCD_SCRIPT_DIR="~/openSourceTools/openocd/tcl"
    # Copilot suggests the following
    # The Linux defaults here are hard-coded and user-machine specific
    #  ("/usr/bin/openocd" and a script dir under "/openSourceTools").
    #  Also, "" inside quotes will not expand in bash, so OpenOCD will
    #  receive a literal path starting with '~' and fail to find scripts.
    # Consider defaulting to just "openocd" (PATH lookup) and a standard
    #  scripts directory, and/or allowing
    #  MEADOW_OPENOCD_COMMAND/MEADOW_OPENOCD_SCRIPT_DIR to be overridden
    # via environment variables (using $HOME for home-relative paths).
    #
    #   MEADOW_OPENOCD_COMMAND="${MEADOW_OPENOCD_COMMAND:-openocd}"
    #  if [ -z "$MEADOW_OPENOCD_SCRIPT_DIR" ]; then
    #    MEADOW_OPENOCD_SCRIPT_DIR="${OPENOCD_SCRIPTS:-/usr/share/openocd/scripts}"
    #  fi
  else
    echo "Unsupported OS ${bold}$OS${reset}.\n"
    exit -1
  fi
}

#
#   Test code.
#
# scriptdir="$( cd "$(dirname "$0")" ; pwd -P )"
# . $scriptdir/common_methods.sh

# set_os_name

# set_openocd_variables

# echo "OpenOCD Command: $MEADOW_OPENOCD_COMMAND"
# echo "OpenOCD Script Dir: $MEADOW_OPENOCD_SCRIPT_DIR"cd 
