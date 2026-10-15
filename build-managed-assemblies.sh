#!/bin/bash
#
#   Build the managed assemblies (the "meadow_assemblies" package) that Meadow
#   apps are trimmed against and deployed with.
#
#   The set is:
#     - System.Private.CoreLib.dll, the Mono CoreLib built from the runtime
#       fork (MEADOW_RUNTIME_DIR, pinned by MONO_RUNTIME_COMMIT);
#     - the framework assemblies of the runtime's .NET SDK (shared framework),
#       which are platform-independent IL;
#     - the networking and cryptography assemblies rebuilt from the runtime's
#       sources for Linux, because the SDK's copies are built for the host OS
#       and use its native APIs instead of the OpenSSL/Unix P/Invokes that
#       Meadow.OS implements;
#     - ILLink.Descriptors.xml, the list of CoreLib types the native runtime
#       loads by name, which lets the Meadow CLI trim CoreLib.
#   Assemblies that are never used on Meadow are removed.
#
#   Output: meadow_assemblies/ in this repository.
#
scriptdir="$( cd "$(dirname "$0")" ; pwd -P )"

. $scriptdir/scripts/common_methods.sh
. $scriptdir/scripts/runtime_methods.sh

check_if_interactive

VERBOSE=false
CLEAN=false
DEBUG=false
KEEP_PDBS=false
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
    -k|--keeppdbs)
    KEEP_PDBS=true
    ;;
    *)
    printf "Unknown option: $i\n"
    exit 1
    ;;
esac
done

if $HELP; then
  echo "Usage: build-managed-assemblies.sh [options]"
  echo ""
  echo "Builds the managed assemblies (meadow_assemblies) for Meadow apps."
  echo ""
  echo "Options:"
  echo "  -h|--help      Show this help message"
  echo "  -v|--verbose   Show the build output"
  echo "  -c|--clean     Delete the output and the runtime's obj/ cache first"
  echo "  -d|--debug     Build CoreLib in the Debug configuration (default: Release)"
  echo "  -k|--keeppdbs  Keep the PDB files"
  echo ""
  echo "Environment:"
  echo "  MEADOW_RUNTIME_DIR             Runtime checkout (default: ../runtime beside this repository)"
  echo "  MEADOW_RUNTIME_ALLOW_UNPINNED  Set to 1 to build a checkout that is not at the"
  echo "                                 MONO_RUNTIME_COMMIT commit, or has local changes"
  exit 0
fi

fail() {
  printf "${red}ERROR:${reset} %s\n" "$1" >&2
  exit 1
}

#
#   Run a build step, showing its output only with --verbose or on failure.
#
run_step() {
  local log=$OUTPUT_DIR.log
  if $VERBOSE; then
    "$@"
  else
    "$@" >> "$log" 2>&1
  fi
  if [ $? -ne 0 ]; then
    if ! $VERBOSE; then
      tail -30 "$log" >&2
    fi
    fail "Failed: $*"
  fi
}

locate_runtime_checkout

if $DEBUG; then
  BUILD_CONFIG=Debug
else
  BUILD_CONFIG=Release
fi

OUTPUT_DIR=$scriptdir/meadow_assemblies
if $CLEAN; then
  rm -rf "$OUTPUT_DIR" "$OUTPUT_DIR.log"
  #
  #   Stale package paths in the runtime's obj/ cache can survive a restore
  #   and produce misleading "type does not exist" errors.
  #
  rm -rf "$RUNTIME_DIR/artifacts/obj"
fi
rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"
rm -f "$OUTPUT_DIR.log"

#
#   Step 1: CoreLib.  The runtime's own build script provisions the SDK pinned
#   by its global.json and restores the packages the build needs.  Event pipe
#   is disabled in the native runtime, so FeaturePerfTracing must be off.
#
printf "Building System.Private.CoreLib ($BUILD_CONFIG)..."
run_step bash -c "cd \"$RUNTIME_DIR\" && ./build.sh -subset mono.corelib -arch arm -os linux -c $BUILD_CONFIG /p:FeaturePerfTracing=false"
printf " ${green}done${reset}\n"

CORELIB_DIR=$RUNTIME_DIR/artifacts/bin/mono/linux.arm.$BUILD_CONFIG
if [ ! -f "$CORELIB_DIR/System.Private.CoreLib.dll" ]; then
  fail "The CoreLib build produced no $CORELIB_DIR/System.Private.CoreLib.dll"
fi

export DOTNET_ROOT=$RUNTIME_DIR/.dotnet
export DOTNET_MULTILEVEL_LOOKUP=0
export DOTNET_NOLOGO=1
export DOTNET_CLI_TELEMETRY_OPTOUT=1
DOTNET=$DOTNET_ROOT/dotnet

#
#   Step 2: the framework assemblies from the SDK's shared framework.
#
FRAMEWORK_ROOT=$DOTNET_ROOT/shared/Microsoft.NETCore.App
FRAMEWORK_VERSION=$(ls -1 "$FRAMEWORK_ROOT" 2> /dev/null | sort -V | tail -1)
if [ -z "$FRAMEWORK_VERSION" ]; then
  fail "No shared framework under $FRAMEWORK_ROOT"
fi
FRAMEWORK_DIR=$FRAMEWORK_ROOT/$FRAMEWORK_VERSION
printf "Framework: %s\n" "$FRAMEWORK_DIR"

for dll in "$FRAMEWORK_DIR"/*.dll; do
  if [ "$(basename "$dll")" != "System.Private.CoreLib.dll" ]; then
    cp "$dll" "$OUTPUT_DIR/"
  fi
done
cp "$CORELIB_DIR/System.Private.CoreLib.dll" "$OUTPUT_DIR/"
if $KEEP_PDBS; then
  cp "$CORELIB_DIR/System.Private.CoreLib.pdb" "$OUTPUT_DIR/" 2> /dev/null
  cp "$FRAMEWORK_DIR"/*.pdb "$OUTPUT_DIR/" 2> /dev/null
fi

DESCRIPTOR=$RUNTIME_DIR/src/mono/System.Private.CoreLib/src/ILLink/ILLink.Descriptors.xml
if [ ! -f "$DESCRIPTOR" ]; then
  fail "No CoreLib trimming descriptor at $DESCRIPTOR"
fi
cp "$DESCRIPTOR" "$OUTPUT_DIR/"

#
#   Step 3: rebuild the platform-specific networking and cryptography
#   assemblies for Linux.  The target framework suffix selects the Unix or
#   Linux sources of each library.
#
TFM=net$(echo "$FRAMEWORK_VERSION" | cut -d. -f1-2)
LINUX_BUILDS="System.Security.Cryptography:unix
System.Net.Security:linux
System.Net.Sockets:unix
System.Net.Primitives:unix
System.Net.NameResolution:unix
System.Net.NetworkInformation:linux
System.Net.Http:linux"

for entry in $LINUX_BUILDS; do
  name=${entry%%:*}
  platform=${entry##*:}
  project=$RUNTIME_DIR/src/libraries/$name/src/$name.csproj
  built=$RUNTIME_DIR/artifacts/bin/$name/Release/$TFM-$platform/$name.dll
  printf "Building %s (%s-%s)..." "$name" "$TFM" "$platform"
  run_step "$DOTNET" build "$project" -c Release -f "$TFM-$platform" -p:TargetArchitecture=arm
  if [ ! -f "$built" ]; then
    fail "The $name build produced no $built"
  fi
  cp "$built" "$OUTPUT_DIR/"
  printf " ${green}done${reset}\n"
done

#
#   Step 4: remove assemblies that are never used on Meadow.
#
for unused in System.Net.Quic \
              System.Runtime.Caching \
              System.CodeDom \
              System.Diagnostics.EventLog \
              System.DirectoryServices \
              System.DirectoryServices.Protocols \
              System.Management \
              System.ServiceProcess.ServiceController \
              System.Windows.Extensions; do
  rm -f "$OUTPUT_DIR/$unused.dll" "$OUTPUT_DIR/$unused.pdb"
done

#
#   Record what the package was built from.
#
cat > "$OUTPUT_DIR/build-info.txt" << EOF
runtime_commit=$RUNTIME_COMMIT
runtime_tree=$RUNTIME_TREE
framework=$FRAMEWORK_VERSION
corelib_configuration=$BUILD_CONFIG
EOF

printf "%d assemblies, %s in %s\n" \
  "$(ls -1 "$OUTPUT_DIR"/*.dll | wc -l)" \
  "$(du -sh "$OUTPUT_DIR" | cut -f1)" \
  "$OUTPUT_DIR"
exit 0
