#!/bin/bash
#
#   Build the .NET Mono runtime archives for Meadow.OS.
#
#   The runtime is built from a fork of dotnet/runtime that lives outside this
#   repository.  Its location comes from MEADOW_RUNTIME_DIR, or ../runtime
#   (relative to this script's directory) when MEADOW_RUNTIME_DIR is not set.
#   The checkout must be at the commit recorded in MONO_RUNTIME_COMMIT.
#
#   The runtime is compiled against this repository's NuttX headers, so NuttX
#   must be configured first (./build.sh --configure).  The archives are
#   written to monoruntime/<release|debug>/ in this repository.
#
scriptdir="$( cd "$(dirname "$0")" ; pwd -P )"

. $scriptdir/scripts/common_methods.sh

check_if_interactive

VERBOSE=false
CLEAN=false
DEBUG=false
HELP=false

for i in "$@"
do
case $i in
    -h|--help)
    HELP=true
    ;;
    -v|--verbose)
    VERBOSE=true
    ;;
    -c|--clean)
    CLEAN=true
    ;;
    -d|--debug)
    DEBUG=true
    ;;
    *)
    printf "Unknown option: $i\n"
    exit 1
    ;;
esac
done

if $HELP; then
  echo "Usage: build-mono.sh [options]"
  echo ""
  echo "Builds the .NET Mono runtime archives for NuttX (Cortex-M7)."
  echo ""
  echo "Options:"
  echo "  -h|--help      Show this help message"
  echo "  -v|--verbose   Show the runtime build output"
  echo "  -c|--clean     Delete the runtime build directory first"
  echo "  -d|--debug     Build the Debug configuration (default: Release)"
  echo ""
  echo "Environment:"
  echo "  MEADOW_RUNTIME_DIR             Runtime checkout (default: ../runtime beside this repository)"
  echo "  MEADOW_RUNTIME_ALLOW_UNPINNED  Set to 1 to build a checkout that is not at the"
  echo "                                 MONO_RUNTIME_COMMIT commit, or has local changes"
  echo "                                 (for runtime development only)"
  exit 0
fi

fail() {
  printf "${red}ERROR:${reset} %s\n" "$1" >&2
  exit 1
}

#
#   The compiler must be GCC 15, the same compiler as the rest of Meadow.OS.
#
if ! command -v arm-none-eabi-gcc &> /dev/null; then
  fail "arm-none-eabi-gcc is not on the PATH."
fi
GCC_VERSION=$(arm-none-eabi-gcc -dumpversion)
if [ "${GCC_VERSION%%.*}" != "15" ]; then
  fail "arm-none-eabi-gcc $GCC_VERSION found; the runtime is built with GCC 15."
fi

#
#   Locate the runtime checkout and check it against MONO_RUNTIME_COMMIT.
#
RUNTIME_DIR="${MEADOW_RUNTIME_DIR:-$scriptdir/../runtime}"
if [ ! -f "$RUNTIME_DIR/src/mono/build-nuttx.sh" ]; then
  fail "No runtime checkout at $RUNTIME_DIR.  Set MEADOW_RUNTIME_DIR to the WildernessLabs/runtime checkout."
fi
RUNTIME_DIR="$( cd "$RUNTIME_DIR" ; pwd -P )"

PIN_FILE=$scriptdir/MONO_RUNTIME_COMMIT
PIN_COMMIT=$(sed -n 's/^commit=//p' "$PIN_FILE")
PIN_TREE=$(sed -n 's/^tree=//p' "$PIN_FILE")
if [ -z "$PIN_COMMIT" ] || [ -z "$PIN_TREE" ]; then
  fail "Cannot read the commit and tree from $PIN_FILE."
fi

RUNTIME_COMMIT=$(git -C "$RUNTIME_DIR" rev-parse HEAD 2> /dev/null)
RUNTIME_TREE=$(git -C "$RUNTIME_DIR" rev-parse "HEAD^{tree}" 2> /dev/null)
RUNTIME_CHANGES=$(git -C "$RUNTIME_DIR" status --porcelain --untracked-files=no 2> /dev/null)

printf "Runtime:   %s\n" "$RUNTIME_DIR"
printf "Commit:    %s\n" "$RUNTIME_COMMIT"

PINNED=true
if [ "$RUNTIME_COMMIT" != "$PIN_COMMIT" ] || [ "$RUNTIME_TREE" != "$PIN_TREE" ]; then
  PINNED=false
  printf "${red}The runtime checkout is not at the MONO_RUNTIME_COMMIT commit %s.${reset}\n" "$PIN_COMMIT"
fi
if [ -n "$RUNTIME_CHANGES" ]; then
  PINNED=false
  printf "${red}The runtime checkout has local changes.${reset}\n"
fi
if ! $PINNED; then
  if [ "${MEADOW_RUNTIME_ALLOW_UNPINNED:-0}" != "1" ]; then
    fail "Check out the pinned runtime commit, or set MEADOW_RUNTIME_ALLOW_UNPINNED=1 while developing the runtime."
  fi
  printf "${red}WARNING:${reset} building an unpinned runtime (MEADOW_RUNTIME_ALLOW_UNPINNED=1).\n"
fi

#
#   The runtime is compiled against the NuttX headers of this repository.
#
NUTTX_INCLUDE_DIR=$scriptdir/nuttx/include
if [ ! -f "$NUTTX_INCLUDE_DIR/nuttx/config.h" ]; then
  fail "NuttX is not configured.  Run ./build.sh --configure first."
fi

if $DEBUG; then
  BUILD_TYPE=Debug
else
  BUILD_TYPE=Release
fi
BUILD_DIR=$scriptdir/monoruntime/$(echo $BUILD_TYPE | tr '[:upper:]' '[:lower:]')

#
#   The build stamp records what the archives were built from.  When any of it
#   changes - the runtime commit or local changes, the compiler, or the NuttX
#   configuration - the build directory is discarded so that CMake configures
#   again from scratch.
#
STAMP_FILE=$BUILD_DIR/meadow-build-stamp
STAMP="runtime_commit=$RUNTIME_COMMIT
runtime_tree=$RUNTIME_TREE
runtime_changes=$(git -C "$RUNTIME_DIR" diff HEAD | shasum | cut -d' ' -f1)
gcc=$GCC_VERSION
nuttx_config=$(shasum "$NUTTX_INCLUDE_DIR/nuttx/config.h" | cut -d' ' -f1)
build_type=$BUILD_TYPE"

if [ -d "$BUILD_DIR" ] && ! $CLEAN; then
  if [ "$(cat "$STAMP_FILE" 2> /dev/null)" != "$STAMP" ]; then
    printf "The runtime, compiler or NuttX configuration has changed; rebuilding from scratch.\n"
    CLEAN=true
  fi
fi
if $CLEAN && [ -d "$BUILD_DIR" ]; then
  rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"

printf "Building the Mono runtime ($BUILD_TYPE) in $BUILD_DIR..."
if $VERBOSE; then
  printf "\n"
  NUTTX_INCLUDE_DIR=$NUTTX_INCLUDE_DIR MONO_BUILD_DIR=$BUILD_DIR "$RUNTIME_DIR/src/mono/build-nuttx.sh" $BUILD_TYPE
else
  NUTTX_INCLUDE_DIR=$NUTTX_INCLUDE_DIR MONO_BUILD_DIR=$BUILD_DIR "$RUNTIME_DIR/src/mono/build-nuttx.sh" $BUILD_TYPE &> "$BUILD_DIR/build.log"
fi
check_command_status

#
#   Check that every archive the OS link needs was produced.
#
ARCHIVES="mono/mini/libmonosgen-2.0.a
mono/mini/libmono-component-marshal-ilgen-static.a
mono/mini/libmono-component-hot_reload-stub-static.a
mono/mini/libmono-component-debugger-stub-static.a
mono/mini/libmono-component-diagnostics_tracing-stub-static.a
mono/minipal/minipal/libminipal.a
mono/minipal/minipal/libaotminipal.a
mono/sgen/libsgen_objects.a
system_native/libSystem.Native.a"

for archive in $ARCHIVES; do
  if [ ! -f "$BUILD_DIR/$archive" ]; then
    fail "Expected archive not found: $BUILD_DIR/$archive"
  fi
  printf "  %6s KB  %s\n" $(( $(wc -c < "$BUILD_DIR/$archive") / 1024 )) "$archive"
done

echo "$STAMP" > "$STAMP_FILE"
exit 0
