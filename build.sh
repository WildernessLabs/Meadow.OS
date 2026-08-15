#!/bin/bash -ex

#set -e
scriptdir="$( cd "$(dirname "$0")" ; pwd -P )"

. $scriptdir/scripts/common_methods.sh
. $scriptdir/scripts/version_methods.sh
. $scriptdir/scripts/configuration_tweaks.sh

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
ENABLE_NSH=false

for i in "$@"
do
case $i in
    -h|--help)
    HELP=true
    ;;
    -v|--verbose)
    VERBOSE=true
    NUTTX_VERBOSE="V=1"
    BOOTLOADER_OPTIONS+="--verbose "
    ;;
    -f|--force)
    FORCE=true
    BOOTLOADER_OPTIONS+="--force "
    ;;
    -c|--clean)
    CLEAN=true
    BOOTLOADER_OPTIONS+="--clean "
    ;;
    --wlclean)
    WLCLEAN=true
    BOOTLOADER_OPTIONS+="--wlclean "
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
    BOOTLOADER_OPTIONS+="--debug "
    ;;
    -mfd|--makefiledebugging)
    MAKE_OPTIONS="--debug VERBOSE=1"
    BOOTLOADER_OPTIONS+="--makefiledebugging "
    ;;
    --esd)
    ENABLE_STACK_DUMP=true
    ;;
    --nsh)
    ENABLE_NSH=true
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
  echo "  --debug                      Enable the NuttX debug features (debug output to SYSLOG)"
  echo "  --esd                        Enable stack dumps to be sent to USART1 (COM1)"
  echo "  --config=mono|netcore        Select Mono or .NET Core builds (default Mono)"
  echo "  -mfd|--makefiledebugging     Turn on debug options for make"
  echo "  -u|--unittests=*             Build the specified unit tests into the system"
  echo "  --nsh                        Enable NSH (NuttX Shell) on USART6 (COM6)"
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
#   First step, configure the system and make any changes by tweaking the
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
    printf "Cleaning NuttX..."
    run_command "make -C $scriptdir/nuttx clean -j8"
    check_command_status
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

NUTTX_CONFIG_FILE=$scriptdir/nuttx/.config
if $FORCE; then
    if [ -r "$scriptdir/nuttx/.config" ]; then
        printf "Cleaning NuttX (already configured)..."
        run_command "make -C $scriptdir/nuttx distclean -j8 $MAKE_OPTIONS"
        check_command_status
    fi
fi

if [ ! -r "$scriptdir/nuttx/.config" ]; then
    printf "Configuring NuttX..."
    run_command "$scriptdir/nuttx/tools/configure.sh $NUTTX_CONFIG"
    check_command_status

    printf "Building NuttX context..."
    run_command "make -C $scriptdir/nuttx context"
    check_command_status
fi

#
# Set the initial system date.
#
BUILD_YEAR=$(date +%Y)
BUILD_MONTH=$(date +%-m)
BUILD_DAY=$(date +%-d)
CURRENT_START_YEAR=$(ktweak --state START_YEAR)
CURRENT_START_MONTH=$(ktweak --state START_MONTH)
CURRENT_START_DAY=$(ktweak --state START_DAY)
if [ "$CURRENT_START_YEAR" != "$BUILD_YEAR" ]; then 
    ktweak --set-val START_YEAR $BUILD_YEAR
fi

if [ "$CURRENT_START_MONTH" != "$BUILD_MONTH" ]; then 
    ktweak --set-val START_MONTH $BUILD_MONTH
fi

if [ "$CURRENT_START_DAY" != "$BUILD_DAY" ]; then
    ktweak --set-val START_DAY $BUILD_DAY
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
            ktweak --enable ESP_TESTS
            BUILD_TESTS=true
            ;;
            bt)
            echo "Bluetooth tests requested."
            ktweak --enable BLUETOOTH_TESTS
            BUILD_TESTS=true
            ;;
            sqlite)
            echo "SQLite tests requested."
            ktweak --enable EXAMPLES_SQLITE_TESTS
            BUILD_TESTS=true
            ;;
            snprintf)
            echo "snprintf tests requested."
            ktweak --enable SNPRINTF_TESTS
            BUILD_TESTS=true
            ;;
            gpio)
            echo "GPIO tests requested."
            ktweak --enable GPIO_TESTS
            BUILD_TESTS=true
            ;;
            overload)
            echo "MCU Overload tests requested."
            ktweak --enable MCU_OVERLOAD_TESTS
            BUILD_TESTS=true
            ;;
            bbr)
            echo "Battery Backed Register tests requested."
            ktweak --enable BBR_TESTS
            BUILD_TESTS=true
            ;;
            chat)
            echo "Chat tests requested."
            ktweak --enable CHAT_TESTS
            BUILD_TESTS=true
            ;;
            ethernet)
            echo "Ethernet tests requested."
            ktweak --enable ETHERNET_TESTS
            BUILD_TESTS=true
            ;;
            bg77)
            echo "BG77 modem tests requested."
            ktweak --enable BG77_TESTS
            BUILD_TESTS=true
            ;;
            iso8601)
            echo "ISO8601 parsing tests requested."
            ktweak --enable ISO8601_TESTS
            BUILD_TESTS=true
            ;;
            power)
            echo "Power management tests requested."
            ktweak --enable POWER_MANAGEMENT_TESTS
            BUILD_TESTS=true
            ;;
            sdcard)
            echo "SD card tests requested."
            ktweak --enable SD_CARD_TESTS
            BUILD_TESTS=true
            ;;
            tensorflow)
            echo "Tensorflow tests requested."
            ktweak --enable TENSORFLOW_TESTS
            BUILD_TESTS=true
            ;;
            misc)
            echo "Miscellaneous tests requested."
            ktweak --enable QUICK_MISC_TESTS
            BUILD_TESTS=true
            ;;
            dirmgmt)
            echo "Directory management tests requested."
            ktweak --enable DIR_MGMT_TESTS
            BUILD_TESTS=true
            ;;
            adc)
            echo "Analog to Digital conversion tests requested."
            ktweak --enable ADC_TESTS
            BUILD_TESTS=true
            ;;
            dac)
            echo "Digital to Analog conversion tests requested."
            ktweak --enable DAC_TESTS
            BUILD_TESTS=true
            ;;
            spidma)
            echo "SPI DMA tests requested."
            ktweak --enable SPI_DMA_TESTS
            BUILD_TESTS=true
            ;;
            pwm)
            echo "PWM tests requested."
            ktweak --enable PWM_TESTS
            BUILD_TESTS=true
            ;;
            i2c)
            echo "I2C bus scan tests requested."
            ktweak --enable I2C_TESTS
            BUILD_TESTS=true
            ;;
            rotenc)
            echo "Rotary Encoder tests requested."
            ktweak --enable ROTARY_ENCODER_TESTS
            BUILD_TESTS=true
            ;;
            gpiocount)
            echo "GPIO Counter tests requested."
            ktweak --enable GPIO_COUNTER_TESTS
            BUILD_TESTS=true
            ;;
            sotest)
            echo "sotest dlopen/dlsym tests requested."
            enable_sotests --tests
            BUILD_TESTS=true
            ;;
            os)
            echo "Operating system tests requested."
            ktweak --enable MEADOW_OS_TESTS
            BUILD_TESTS=true
            ;;
            usrwork)
            echo "Verify USRWORK functions as expected on apps side."
            ktweak --enable USR_WORK_TESTS
            BUILD_TESTS=true
            ;;
            traverse_ku)
            echo "KU traverse channel tests requested."
            ktweak --enable TRAVERSE_KU_TESTS
            BUILD_TESTS=true
            ;;
            measfreq)
            echo "Measure Frequency tests requested."
            ktweak --enable MEASURE_FREQUENCY_TESTS
            BUILD_TESTS=true
            ;;
            mint)
            echo "Meadow interrupt tests requested."
            ktweak --enable MEADOW_INTERRUPT_TESTS
            BUILD_TESTS=true
            ;;
            all)
            echo "All tests requested."
            ktweak --enable ALL_MEADOW_TESTS
            #
            # The sotest .so is built, but SOTEST_TESTS itself is left off - it
            # is one of the tests whose "|| CONFIG_ALL_MEADOW_TESTS" is
            # deliberately commented out in developer_tests.c, and enabling it
            # here pulls the ELF export table into the link (see
            # enable_sotests).  Use "-u=sotest" to run it.
            #
            enable_sotests
            ktweak --enable EXAMPLES_SQLITE_TESTS
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
  ktweak --enable KERNEL_TESTS_SYSCALL
fi

if $ENABLE_STACK_DUMP; then
  enable_stack_dump
fi

if $ENABLE_NSH; then
  enable_nsh
fi

if $DEBUG; then
  enable_debug_features
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
