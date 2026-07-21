#!/bin/bash -ex

#set -e
scriptdir="$( cd "$(dirname "$0")" ; pwd -P )"

. $scriptdir/scripts/common_methods.sh
. $scriptdir/scripts/version_methods.sh

set_os_name
check_if_interactive

#
# Setup some of the variables used by this script.
#
VERBOSE=true
FORCE=false
CLEAN=false
MONO=false
CONFIGURE_ONLY=false
CONFIG=meadow_os
NETCORE=false
WLCLEAN=false
DEBUG=false
DEBUG_BL_CDC=false
DEBUG_BL_UART=false
HELP=false
ENABLE_STACK_DUMP=false
MAKE_OPTIONS="MEADOW_OS=1"
UNIT_TESTS=
BOOTLOADER_OPTIONS=
NUTTX_VERBOSE=

for i in "$@"
do
case $i in
    -h|--help)
    HELP=true
    ;;
    -v|--verbose)
    VERBOSE=true
    NUTTX_VERBOSE="V=1"
    BOOTLOADER_OPTION+="--verbose "
    ;;
    -f|--force)
    FORCE=true
    BOOTLOADER_OPTIONS+="--force "
    ;;
    -c|--clean)
    CLEAN=true
    BOOTLOADER_OPTIONs+="--clean "
    ;;
    --wlclean)
    WLCLEAN=true
    BOOTLOADER_OPTIONs+="--wlclean "
    ;;
    -m|--mono)
    MONO=true
    ;;
    --netcore)
    NETCORE=true
    ;;
    --configure)
    CONFIGURE_ONLY=true
    ;;
    --debug)
    DEBUG=true
    BOOTLOADER_OPTIONs+="--debug "
    ;;
    -mfd|--makefiledebugging)
    MAKE_OPTIONS="--debug VERBOSE=1"
    BOOTLOADER_OPTIONs+="--makefiledebugging "
    ;;
    --esd)
    ENABLE_STACK_DUMP=true
    ;;
    --dbc|--debug-bl-cdc)
    DEBUG_BL_CDC=true
    ;;
    --dbu|--debug-bl-uart)
    DEBUG_BL_UART=true
    ;;
    --config=*)
    CONFIG=$(echo $i | cut -f2 -d=)
    ;;
    -u=*|--unittests=*)
    UNIT_TESTS="${i#*=}"
    ;;
    *)
    echo "${0##*/} - Unknown option $i"
    exit 1
    ;;
esac
done

if [ "$HELP" = true ]; then
  echo "Usage: ${0##*/} [options]"
  echo " "
  echo "Options:"
  echo "  -h|--help                    Show this help message"
  echo "  -v|--verbose                 Show verbose output"
  echo "  -f|--force                   Force build"
  echo "  -c|--clean                   Clean build"
  echo "  --wlclean                    Clean the Wilderness Labs object files"
  echo "  -m|--mono                    Build with Mono"
  echo "  --netcore                    Build with .NET Core"
  echo "  --configure                  Configure the build"
  echo "  --debug                      Build with debug symbols"
  echo "  --esd                        Enable stack dumps to be sent to USART1 (COM1)"
  echo "  --config=mono|netcore        Select Mono or .NET Core builds (default Mono)"
  echo "  -mfd|--makefiledebugging     Turn on debug options for make"
  echo "  -u|--unittests=*             Build the specified unit tests into the system"
  exit 0
fi

if [[ -z "$MEADOW_ADDITIONAL_MAKE_OPTIONS" ]]; then
  MEADOW_ADDITIONAL_MAKE_OPTIONS="-j8"
fi

#
#   Generate build info
#
generate_build_info

#
# Setup toolchain
#
case "$(uname -s)" in
    Darwin)
      export PATH=$scriptdir/toolchain/macos:$PATH
      ;;
    Linux)
      export PATH=$scriptdir/toolchain/linux:$PATH
      ;;
    CYGWIN*|MINGW32*|MSYS*|MINGW*)
      export PATH=$scriptdir/toolchain/windows:$PATH
      ;;
    *)
      printf "Toolchain setup not implemented yet for this OS.\n"
      exit 0
      ;;
esac

###############################################################################
#
#   First step, configure the systemand make any changes by tweaking the
#   configuration.
#

DEFCONFIG_FILE=$scriptdir/nuttx/configs/stm32f777zit6-meadow/meadow_os/defconfig
NUTTX_CONFIG="stm32f777zit6-meadow:$CONFIG"

# NUTTX_CONFIG_FILE=$scriptdir/nuttx/.config
# if $FORCE; then
#     if [ -r "$scriptdir/nuttx/.config" ]; then
#         printf "Cleaning NuttX (already configured)..."
#         run_command "make -C $scriptdir/nuttx distclean -j8 $MAKE_OPTIONS"
#         check_command_status
#     fi
# fi

# if [ ! -r "$scriptdir/nuttx/.config" ]; then
#     printf "Configuring NuttX...\n"
#     run_command "$scriptdir/nuttx/tools/configure.sh $NUTTX_CONFIG"

#     run_command "make -C $scriptdir/nuttx context"
#     check_command_status
# fi

#
# Added the ability to clean only the code created by Wilderness Labs
#
# This option allows for a clean of only the frequently edited files which
# reduces the compilation time.
#
if $WLCLEAN || $CLEAN; then
    find $scriptdir/apps/examples -name "*.o" -type f -exec rm {} \;
    find $scriptdir/apps/system -name "*.o" -type f -exec rm {} \;
    find $scriptdir/F7MeadowOS -name "*.o" -type f -exec rm {} \;
    run_command "make -C $scriptdir/nuttx clean -j8"
    run_command "make $MEADOW_ADDITIONAL_MAKE_OPTIONS -C $scriptdir/bootloader/Debug clean"
    #
    # Sometimes the libapps.a file can become corrupt due to a previously failed
    # build.  If this happens, remove it so that it can be rebuilt.
    #
    find $scriptdir -name libapps.a -exec rm {} \;
fi

#
# Force the version number to update if it has changed.
#
rm -f $scriptdir/F7MeadowOS/hcom_nx/hcom_nx_config_manager.o
rm -f $scriptdir/F7MeadowOS/hcom_nx/diag/hcom_nx_trace_msg_proc.o
rm -f $scriptdir/nuttx/*.bin
rm -f $scriptdir/nuttx/*.elf
rm -f $scriptdir/nuttx/*.hex

#
# apps/database/sqlite/Makefile downloads and extracts its own SQLite source
# tree (apps/database/sqlite/sqlite/) via a "context::" recipe that is only
# safe to run once: it always re-downloads and re-extracts unless
# sqlite/.git already exists, then does "mv sqlite-vesion-* sqlite". If
# sqlite/ already has content (from an earlier run, or even from an earlier
# "context" invocation within this *same* build - context is a prerequisite
# of both pass1 and pass2, and kconfig-tweak's edit to .config appears to
# invalidate the apps context stamp, so it can run more than once per
# build), that mv either silently nests the fresh extraction inside the
# existing sqlite/ as sqlite/sqlite-vesion-*/, or - if a nested leftover
# from a previous such collision is already there - fails outright with
# "Directory not empty".
#
# Clear any stale sqlite.zip / sqlite-vesion-* leftovers first. Then, if the
# SQLite source isn't already present, download and extract it here,
# ourselves, exactly once. Finally, unconditionally ensure a placeholder
# sqlite/.git directory exists - whether or not a download just happened
# above, since the source may already have been present from an earlier
# run. The Makefile's own download step only ever runs when sqlite/.git is
# absent, so once this marker exists it permanently skips its own fragile
# download/extract/mv logic - no matter how many times "context" gets
# invoked within this build or on any future one.
#
rm -f $scriptdir/apps/database/sqlite/sqlite.zip
find $scriptdir/apps/database/sqlite -name "sqlite-vesion-*" -type d -prune -exec rm -rf {} \;

SQLITE_PKG_DIR=$scriptdir/apps/database/sqlite
SQLITE_SRC_DIR=$SQLITE_PKG_DIR/sqlite
if [ ! -f "$SQLITE_SRC_DIR/configure" ]; then
    rm -rf "$SQLITE_SRC_DIR"
    SQLITE_VERSION=$(grep -A2 '^config LIB_SQLITE_VERSION$' "$SQLITE_PKG_DIR/Kconfig" | grep 'default' | sed -E 's/.*"([^"]*)".*/\1/')
    if [ -r "$scriptdir/nuttx/.config" ]; then
        CONFIGURED_SQLITE_VERSION=$(grep '^CONFIG_LIB_SQLITE_VERSION=' "$scriptdir/nuttx/.config" | sed -E 's/^CONFIG_LIB_SQLITE_VERSION="(.*)"$/\1/')
        if [ -n "$CONFIGURED_SQLITE_VERSION" ]; then
            SQLITE_VERSION=$CONFIGURED_SQLITE_VERSION
        fi
    fi
    printf "Downloading SQLite $SQLITE_VERSION...\n"
    ( cd "$SQLITE_PKG_DIR" && \
      curl -L "https://codeload.github.com/sqlite/sqlite/zip/refs/tags/vesion-$SQLITE_VERSION" -o sqlite.zip && \
      unzip -q -o sqlite.zip && \
      mv "sqlite-vesion-$SQLITE_VERSION" sqlite && \
      rm -f sqlite.zip )
    check_command_status
fi
mkdir -p "$SQLITE_SRC_DIR/.git"

NUTTX_CONFIG_FILE=$scriptdir/nuttx/.config
if $FORCE; then
    if [ -r "$scriptdir/nuttx/.config" ]; then
        printf "Cleaning NuttX (already configured)..."
        run_command "make -C $scriptdir/nuttx distclean -j8 $MAKE_OPTIONS"
        check_command_status
    fi
fi

if [ ! -r "$scriptdir/nuttx/.config" ]; then
    printf "Configuring NuttX...\n"
    run_command "$scriptdir/nuttx/tools/configure.sh $NUTTX_CONFIG"

    run_command "make -C $scriptdir/nuttx context"
    check_command_status
fi

#
#   Work out if any tests have been requested and turn them on in the build.
#
BUILD_TESTS=false
if [ ! -z "$UNIT_TESTS" ]; then
    unittests=$(echo $UNIT_TESTS | tr "," "\n")
    for test in $unittests
    do
        case $test in
            esp)
            echo "ESP tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable ESP_TESTS
            BUILD_TESTS=true
            ;;
            bt)
            echo "Bluetooth tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable BLUETOOTH_TESTS
            BUILD_TESTS=true
            ;;
            sqlite)
            echo "SQLite tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable EXAMPLES_SQLITE_TESTS
            BUILD_TESTS=true
            ;;
            snprintf)
            echo "snprintf tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable SNPRINTF_TESTS
            BUILD_TESTS=true
            ;;
            gpio)
            echo "GPIO tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable GPIO_TESTS
            BUILD_TESTS=true
            ;;
            overload)
            echo "MCU Overload tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable MCU_OVERLOAD_TESTS
            BUILD_TESTS=true
            ;;
            bbr)
            echo "Battery Backed Register tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable BBR_TESTS
            BUILD_TESTS=true
            ;;
            chat)
            echo "Chat tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable CHAT_TESTS
            BUILD_TESTS=true
            ;;
            ethernet)
            echo "Ethernet tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable ETHERNET_TESTS
            BUILD_TESTS=true
            ;;
            bg77)
            echo "BG77 modem tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable BG77_TESTS
            BUILD_TESTS=true
            ;;
            iso8601)
            echo "ISO8601 parsing tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable ISO8601_TESTS
            BUILD_TESTS=true
            ;;
            power)
            echo "Power management tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable POWER_MANAGEMENT_TESTS
            BUILD_TESTS=true
            ;;
            sdcard)
            echo "SD card tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable SD_CARD_TESTS
            BUILD_TESTS=true
            ;;
            tensorflow)
            echo "Tensorflow tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable TENSORFLOW_TESTS
            BUILD_TESTS=true
            ;;
            misc)
            echo "Miscellaneous tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable QUICK_MISC_TESTS
            BUILD_TESTS=true
            ;;
            dirmgmt)
            echo "Directory management tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable DIR_MGMT_TESTS
            BUILD_TESTS=true
            ;;
            adc)
            echo "Analog to Digital conversion tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable ADC_TESTS
            BUILD_TESTS=true
            ;;
            dac)
            echo "Digital to Analog conversion tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable DAC_TESTS
            BUILD_TESTS=true
            ;;
            spidma)
            echo "SPI DMA tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable SPI_DMA_TESTS
            BUILD_TESTS=true
            ;;
            rotenc)
            echo "Rotary Encoder tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable ROTARY_ENCODER_TESTS
            BUILD_TESTS=true
            ;;
            gpioCounter)
            echo "GPIO Counter tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable GPIO_COUNTER_TESTS
            BUILD_TESTS=true
            ;;
            sotest)
            echo "sotest dlopen/dlsym tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable SOTEST_TESTS
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable EXAMPLES_SOTEST
            kconfig-tweak --file $NUTTX_CONFIG_FILE --set-str EXAMPLES_SOTEST_BINDIR "/meadow0"
            BUILD_TESTS=true
            ;;
            os)
            echo "Operating system tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable MEADOW_OS_TESTS
            BUILD_TESTS=true
            ;;
            usrwork)
            echo "Verify USRWORK functions as expected on apps side."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable USR_WORK_TESTS
            BUILD_TESTS=true
            ;;
            traverse_ku)
            echo "KU traverse channel tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable TRAVERSE_KU_TESTS
            BUILD_TESTS=true
            ;;
            mint)
            echo "Meadow interrupt tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable MEADOW_INTERRUPT_TESTS
            BUILD_TESTS=true
            ;;
            all)
            echo "All tests requested."
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable ALL_MEADOW_TESTS
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable EXAMPLES_SOTEST
            kconfig-tweak --file $NUTTX_CONFIG_FILE --set-str EXAMPLES_SOTEST_BINDIR "/meadow0"
            kconfig-tweak --file $NUTTX_CONFIG_FILE --enable EXAMPLES_SQLITE_TESTS
            BUILD_TESTS=true
            ;;
            *)
            printf "Unknown unit test $test."
            exit 1
            ;;
        esac
    done
fi
if $BUILD_TESTS; then
  kconfig-tweak --file $NUTTX_CONFIG_FILE --enable KERNEL_TESTS_SYSCALL
fi

if $ENABLE_STACK_DUMP; then
  #
  # This is used to turn off RAMLOG and enables stack dumps to be sent to USART1 (COM1).
  #
  printf "\n\n********** Enabling stack dump to USART1 (COM1).  This will disable RAMLOG. **********\n\n"
  kconfig-tweak --file $NUTTX_CONFIG_FILE --enable DEV_CONSOLE
  kconfig-tweak --file $NUTTX_CONFIG_FILE --enable SERIAL_CONSOLE
  kconfig-tweak --file $NUTTX_CONFIG_FILE --enable USART1_SERIAL_CONSOLE
  kconfig-tweak --file $NUTTX_CONFIG_FILE --enable SYSLOG_WRITE
  kconfig-tweak --file $NUTTX_CONFIG_FILE --enable SYSLOG_SERIAL_CONSOLE
  kconfig-tweak --file $NUTTX_CONFIG_FILE --enable SYSLOG_CONSOLE

  kconfig-tweak --file $NUTTX_CONFIG_FILE --undefine NO_SERIAL_CONSOLE
  kconfig-tweak --file $NUTTX_CONFIG_FILE --undefine RAMLOG
  kconfig-tweak --file $NUTTX_CONFIG_FILE --undefine RAMLOG_BUFSIZE
  kconfig-tweak --file $NUTTX_CONFIG_FILE --undefine RAMLOG_NPOLLWAITERS
  kconfig-tweak --file $NUTTX_CONFIG_FILE --undefine RAMLOG_SYSLOG

  kconfig-tweak --file $NUTTX_CONFIG_FILE --enable STACK_COLORATION
fi

if $CONFIGURE_ONLY; then
  exit 0
fi

###############################################################################
#
#   Now we can build the system.
#

#
#   Build the bootloader
#
$scriptdir/build-bootloader.sh $BOOTLOADER_OPTIONS
if [ $? -ne 0 ]; then
    exit 1
fi

printf "Building NuttX (kernel pass)...\n"
# Build mksyscall first due to issues with concurrency and makefile dependencies
run_command "make -C $scriptdir/nuttx/tools $NUTTX_VERBOSE $MAKE_OPTIONS -f Makefile.host mksyscall"
run_command "make -C $scriptdir/nuttx $NUTTX_VERBOSE $MEADOW_ADDITIONAL_MAKE_OPTIONS $MAKE_OPTIONS pass2"
check_command_status

#   Now that the kernel is configured, we can build the userspace libs

#
#   Build mbedTLS
#
$scriptdir/build-mbedtls.sh "$@"
if [ $? -ne 0 ]; then
    exit 1
fi



if ! grep -q "CONFIG_BUILD_FLAT=y" $scriptdir/nuttx/.config; then
  printf "Building NuttX (user pass)..."
  if $NETCORE; then
    export ENABLE_NETCORE=1
  fi
  run_command "make -C $scriptdir/nuttx $NUTTX_VERBOSE $MEADOW_ADDITIONAL_MAKE_OPTIONS $MAKE_OPTIONS pass1"
  check_command_status
fi

#   Complete the build
run_command "make -C $scriptdir/nuttx $NUTTX_VERBOSE $MEADOW_ADDITIONAL_MAKE_OPTIONS $MAKE_OPTIONS nuttx.elf"
check_command_status

#
#   Package Meadow.OS
#
if ! grep -q "CONFIG_BUILD_FLAT=y" $scriptdir/nuttx/.config; then
  MEADOW_OS_BIN=$scriptdir/nuttx/Meadow.OS.Update.bin
  MEADOW_BL_BIN=$scriptdir/bootloader/Debug/Meadow.BL.bin
  MEADOW_OS_BL_BIN=$scriptdir/nuttx/Meadow.OS.bin
  dd if=/dev/zero bs=1024 count=1792 of=${MEADOW_OS_BIN} 2> /dev/null
  dd if=$scriptdir/nuttx/nuttx.bin bs=1024 of=${MEADOW_OS_BIN} conv=notrunc 2> /dev/null
  dd if=$scriptdir/nuttx/nuttx_user.bin bs=512 skip=1 seek=1 count=2047 of=${MEADOW_OS_BIN} conv=notrunc 2> /dev/null
 
  # Generate CRC and overwrite to last 4 bytes of binary
  srec_cat ${MEADOW_OS_BIN} -Binary -crop 0x00000000 0x001BFFFC -STM32 0x001BFFFC -o ${MEADOW_OS_BIN} -Binary

  # Merge Meadow.BL binary with Meadow.OS binary
  srec_cat ${MEADOW_BL_BIN} -Binary ${MEADOW_OS_BIN} -Binary -offset 0x00040000 -o ${MEADOW_OS_BL_BIN} -Binary

  MEADOW_OS_RUNTIME_BIN=$scriptdir/nuttx/Meadow.OS.Runtime.bin
  dd if=/dev/zero bs=1024 count=3072 of=${MEADOW_OS_RUNTIME_BIN} 2> /dev/null
  dd if=$scriptdir/nuttx/nuttx_user.bin bs=1024 skip=3014400 seek=0 count=3072 of=${MEADOW_OS_RUNTIME_BIN} conv=notrunc 2> /dev/null
fi

restore_versioned_files

print_build_summary
