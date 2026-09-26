#
#   Functions that will modify the configuration of the NuttX build for Meadow.
#

#
# Apply a single kconfig-tweak to the NuttX configuration named by
# NUTTX_CONFIG_FILE.  Every function in this file goes through here.
#
# Both guards below exist because kconfig-tweak handles these two cases badly:
#
#   - Called with an empty --file it dumps its entire usage text and exits 1,
#     which stops the build but says nothing about what actually went wrong.
#   - Called with a --file that does not exist it writes a warning to stderr,
#     CREATES that file containing just the one symbol, and exits 0.  A build.sh
#     that ran these tweaks before NuttX was configured would therefore build up
#     a bogus .config a line at a time, and - because .config now exists -
#     configure.sh would never run to replace it.
#
# The second case is silent and destructive, so check up front instead.
#
ktweak() {
    if [ -z "$NUTTX_CONFIG_FILE" ]; then
        printf "ktweak: NUTTX_CONFIG_FILE is not set, cannot apply '%s'.\n" "$*" >&2
        printf "ktweak: it must be assigned before any configuration tweak runs.\n" >&2
        exit 1
    fi

    if [ ! -f "$NUTTX_CONFIG_FILE" ]; then
        printf "ktweak: '%s' does not exist, cannot apply '%s'.\n" \
               "$NUTTX_CONFIG_FILE" "$*" >&2
        printf "ktweak: NuttX must be configured before any configuration tweak runs.\n" >&2
        exit 1
    fi

    kconfig-tweak --file "$NUTTX_CONFIG_FILE" "$@"
}

#
# Enable NSH (NuttX Shell) on USART6 (COM6).
#
# This applies the same set of options that Documentation/SettingupMeadowNSH.md
# describes as a defconfig block, but does so with kconfig-tweak so that NSH stays
# an opt-in build flag instead of a permanent part of the board configuration.
#
# kconfig-tweak edits .config as text - it does not run olddefconfig - so every
# symbol Kconfig would have filled in by default has to be written out explicitly
# here.  Note also that this board sets CONFIG_DEFAULT_SMALL=y, which makes every
# NSH_DISABLE_* and FS_PROCFS_EXCLUDE_* symbol default to 'y'; those are disabled
# explicitly below so the shell keeps its commands and /proc keeps its contents.
#
enable_nsh() {
    printf "\n\n********** Enabling NSH (NuttX Shell) on USART6 (COM6). **********\n\n"

    #
    # Required for exec_builtin()/nsh_builtin() to find *any* registered
    # builtin app by name at the nsh> prompt (calib_udelay, pppd, renew, hcom,
    # sh, a nested nsh -- anything that isn't one of NSH's own internal
    # commands like ls/cat/help). Without both of these, such commands report
    # "command not found" even though they're compiled in and registered --
    # NSH's own internal commands and its own top-level launch are unaffected
    # (nsh itself is started via a direct task_create() call, not through this
    # lookup), which is why "help" can work fine while everything else can't.
    #
    ktweak --enable BUILTIN
    ktweak --enable NSH_BUILTIN_APPS

    #
    # NOTE: CONFIG_FDCLONE_STDIO is NOT set here -- it lives permanently in the
    # base defconfig instead, since it's not actually NSH-specific. Every
    # task_create()/task_spawn() in the system (not just NSH's own builtin
    # commands) goes through group_setuptaskfiles(), including
    # hcom_mono_control.c's launch of the Mono runtime itself, whose own
    # comment documents relying on exactly this behavior ("A task will only
    # share stdin, stderr and stdout with the threads running hcom"). See the
    # base defconfig for the actual setting and Documentation/SettingupMeadowNSH.md
    # for the full explanation.
    #

    #
    # USART6 is the NSH console.  It is not the system console, so TTY_SIGINT has
    # to be requested explicitly for Ctrl+C to work (e.g. to get out of "top").
    #
    ktweak --enable STM32F7_USART6
    ktweak --enable USART6_SERIALDRIVER
    ktweak --set-val USART6_BAUD 115200
    ktweak --set-val USART6_BITS 8
    ktweak --set-val USART6_PARITY 0
    ktweak --set-val USART6_2STOP 0
    ktweak --set-val USART6_RXBUFSIZE 512
    ktweak --set-val USART6_TXBUFSIZE 512
    ktweak --enable TTY_SIGINT
    ktweak --set-val TTY_SIGINT_CHAR 0x03

    #
    # procfs.  "ps", "top", "uptime", "df", "mount" and "env" all read /proc, so
    # none of the FS_PROCFS_EXCLUDE_* options may be left at their DEFAULT_SMALL
    # default of 'y'.
    #
    ktweak --enable FS_PROCFS
    ktweak --enable FS_PROCFS_REGISTER
    for exclude in BLOCKS CPUINFO CPULOAD ENVIRON IOBINFO MEMDUMP MEMINFO MOUNT \
                   MOUNTS NET PARTITIONS PROCESS ROUTE TCBINFO UPTIME USAGE \
                   VERSION
    do
        ktweak --disable FS_PROCFS_EXCLUDE_$exclude
    done

    #
    # /proc/meminfo's progmem section is the one procfs feature that has to stay
    # off.  Kconfig offers it because ARCH_HAVE_PROGMEM is selected arch-wide, but
    # the STM32F7 implementation (stm32_flash.c) is only compiled when
    # CONFIG_STM32F7_PROGMEM=y, which this board does not set - enabling it fails
    # the link with undefined references to up_progmem_*.
    #
    ktweak --disable FS_PROCFS_INCLUDE_PROGMEM

    #
    # CPU load tracking, needed by "top".  SCHED_CPULOAD_* is a choice, so the
    # current selection (NONE) has to be cleared before SYSCLK is selected.
    #
    ktweak --disable SCHED_CPULOAD_NONE
    ktweak --enable SCHED_CPULOAD_SYSCLK
    ktweak --set-val SCHED_CPULOAD_TIMECONSTANT 2

    #
    # NSH library, its readline front end, and the defaults Kconfig would supply
    # for both.
    #
    ktweak --enable NSH_LIBRARY
    ktweak --enable NSH_CONSOLE
    ktweak --enable NSH_READLINE

    #
    # SYSTEM_READLINE is a menuconfig that NSH_LIBRARY/NSH_READLINE normally
    # pull in via Kconfig's own "select" -- but ktweak/kconfig-tweak edits
    # .config as text and never resolves "select" dependencies, so it has to
    # be requested explicitly here too.
    #
    ktweak --enable SYSTEM_READLINE
    ktweak --enable READLINE_HAVE_EXTMATCH

    #
    # readline() only removes a backspaced character from its own line
    # buffer; it never touches the terminal unless READLINE_ECHO is on, in
    # which case it actively sends a backspace + VT100 clear-to-EOL to erase
    # it on screen. Without this, backspace silently does nothing visible
    # over a terminal emulator with local echo turned off (as this project's
    # setup requires), even though normal typed characters still show up via
    # the serial driver's own termios echo. Defaults to 'y' upstream, but set
    # explicitly here since it has been accidentally turned off before.
    #
    ktweak --enable READLINE_ECHO
    ktweak --enable READLINE_CMD_HISTORY
    ktweak --set-val READLINE_CMD_HISTORY_LEN 4
    ktweak --set-val READLINE_CMD_HISTORY_LINELEN 64
    ktweak --set-val NSH_FILEIOSIZE 512
    ktweak --set-val NSH_MAXARGUMENTS 7
    ktweak --set-val NSH_NESTDEPTH 3
    ktweak --set-val NSH_PROMPT_MAX 256
    ktweak --set-val NSH_CODECS_BUFSIZE 128
    ktweak --set-val NSH_WGET_BUFF_SIZE 512
    ktweak --set-val NSH_MMCSDMINOR 0
    ktweak --set-val NSH_MMCSDSLOTNO 0
    ktweak --set-str NSH_PROC_MOUNTPOINT "/proc"
    ktweak --set-str NSH_PROMPT_STRING "nsh> "
    ktweak --set-str NSH_PROMPT_SUFFIX "> "
    ktweak --set-str NSH_PROMPT_ENV "PS1"
    ktweak --disable NSH_DISABLEBG

    #
    # Scripting, along with the if/then/else and loop constructs and the script
    # paths that only become visible once NSH_DISABLESCRIPT is off.
    #
    ktweak --disable NSH_DISABLESCRIPT
    ktweak --disable NSH_DISABLE_ITEF
    ktweak --disable NSH_DISABLE_LOOPS
    ktweak --set-str NSH_INITSCRIPT "init.d/rcS"
    ktweak --set-str NSH_SYSINITSCRIPT "init.d/rc.sysinit"
    ktweak --set-str NSH_SCRIPT_REDIRECT_PATH ""

    #
    # NSH must not bring the network up itself - Meadow owns network and cloud
    # management - so NSH_NETINIT (which defaults to 'y' whenever NET is enabled)
    # and the netinit helper it selects are both left off.
    #
    ktweak --disable NSH_NETINIT
    ktweak --disable NETUTILS_NETINIT

    #
    # "mkrd" needs the boardctl/ramdisk support that nshlib selects for it.
    #
    ktweak --enable BOARDCTL_MKRD
    ktweak --enable DRVR_MKRD

    #
    # Run NSH on USART6 rather than on the system console.  /dev/ttyS3 is what
    # USART6 resolves to with this board's current set of enabled UARTs; re-check
    # it if that set changes, since /dev/ttySx is numbered in UART-enable order.
    #
    ktweak --enable NSH_ALTCONDEV
    ktweak --set-str NSH_ALTSTDERR "/dev/ttyS3"
    ktweak --set-str NSH_ALTSTDIN "/dev/ttyS3"
    ktweak --set-str NSH_ALTSTDOUT "/dev/ttyS3"

    #
    # ifconfig and ifupdown are left disabled so NSH does not fight Meadow's own
    # network management; timedatectl needs kernel-internal symbols that are not
    # exposed to userspace in this build.
    #
    ktweak --enable NSH_DISABLE_IFCONFIG
    ktweak --enable NSH_DISABLE_IFUPDOWN
    ktweak --enable NSH_DISABLE_TIMEDATECTL

    #
    # Everything else stays available.  Without these, DEFAULT_SMALL=y would
    # disable the lot.
    #
    for command in ADDROUTE ARP BASENAME CAT CD CMP CP DATE DELROUTE DF DIRNAME \
                   DMESG ECHO ENV ERROR_PRINT EXEC EXIT EXPORT EXPR FDINFO FREE \
                   GET HEXDUMP KILL LOMTD LOSETUP LOSMART LS MB MH MKDIR MKFATFS \
                   MKFIFO MKRD MOUNT MV MW NSLOOKUP PIDOF PKILL PRINTF PS \
                   PSSIGMASK PUT PWD REBOOT RM RMDIR ROUTE SEMICOLON SET \
                   SHUTDOWN \
                   SLEEP SOURCE TEST TIME TOP TRUNCATE UMOUNT UNAME UNSET UPTIME \
                   USLEEP WAIT WATCH WGET XD
    do
        ktweak --disable NSH_DISABLE_$command
    done

    #
    # The nsh task itself.
    #
    ktweak --enable SYSTEM_NSH
    ktweak --set-val SYSTEM_NSH_PRIORITY 180
    ktweak --set-val SYSTEM_NSH_STACKSIZE 8192
    ktweak --set-str SYSTEM_NSH_PROGNAME "nsh"
}

#
# calib_udelay: a one-off diagnostic tool used to measure the real
# CONFIG_BOARD_LOOPSPERMSEC value for a board -- not something every NSH
# session needs, so it's deliberately not part of enable_nsh(). Enable it
# on top of --nsh only when actually calibrating:
#   ktweak --enable EXAMPLES_CALIB_UDELAY
#   ktweak --set-val EXAMPLES_CALIB_UDELAY_NUM_MEASUREMENTS 3
#   ktweak --set-val EXAMPLES_CALIB_UDELAY_NUM_RESULTS 20
#   ktweak --set-val EXAMPLES_CALIB_UDELAY_DELAY_TEST_ITERATIONS 100000
# The three sub-options are declared under "if EXAMPLES_CALIB_UDELAY" in
# apps/examples/calib_udelay/Kconfig, so -- same kconfig-tweak-never-runs-
# olddefconfig reason as everywhere in enable_nsh() -- they must be set
# explicitly too. NUM_MEASUREMENTS/NUM_RESULTS happen to have matching
# #ifndef fallbacks in the source itself, but DELAY_TEST_ITERATIONS does not
# and fails the build outright ("undeclared") if left unset.
#

#
# Enable a second USB CDC/ACM port (Issue #457).
#
# Developer only - never set in the defconfig or CI.  The board enumerates as a
# USB composite device carrying two CDC/ACM functions over the one physical USB
# connection:
#
#   /dev/ttyACM0 (minor 0) - HCOM, unchanged
#   /dev/ttyACM1 (minor 1) - second port for developer use
#
# The two functions are built by board_composite0_connect() in the board's
# src/stm32_composite.c, and stm32_boot.c's board_init_usbdev() connects the
# composite device instead of the single CDC/ACM device when
# CONFIG_USBDEV_COMPOSITE is set.
#
# Meadow.CLI picks a port by USB VID alone and takes the first one it finds, so
# with two ports it may pick the wrong one.  Pin HCOM to the first port with
# "meadow config route <port>" when running this build.
#
# The vendor ID and the vendor/product strings match the single-port build,
# since Meadow.CLI matches on them (VID on Windows, the vendor string on Linux,
# the product string on macOS).  The product ID is deliberately different
# (0x0002 vs 0x0001): the serial number is shared with the single-port build, so
# a matching VID/PID would let Windows reuse the driver binding it cached for the
# single-port device instead of setting the composite device up as two ports.
#
# Same kconfig-tweak-never-runs-olddefconfig caveat as enable_nsh(): every
# symbol Kconfig would have defaulted is written out explicitly, and the
# non-composite CDC/ACM symbols (hidden by Kconfig once CDCACM_COMPOSITE is set)
# are removed so they do not linger in the generated config.h.  Build with
# --force to return to the single-port configuration afterwards.
#
enable_dualcdc() {
    printf "\n\n********** Enabling dual USB CDC/ACM (/dev/ttyACM0 + /dev/ttyACM1). **********\n\n"

    #
    # Composite device.  COMPOSITE_SERIALSTR is not set: it depends on
    # !BOARD_USBDEV_SERIALSTR, and this board supplies the serial number through
    # board_usbdev_serialstr() instead.
    #
    ktweak --enable USBDEV_COMPOSITE
    ktweak --enable COMPOSITE_IAD
    ktweak --set-val COMPOSITE_EP0MAXPACKET 64
    ktweak --set-val COMPOSITE_VENDORID 0x2E6A
    ktweak --set-str COMPOSITE_VENDORSTR "Wilderness Labs"
    ktweak --set-val COMPOSITE_PRODUCTID 0x0002
    ktweak --set-str COMPOSITE_PRODUCTSTR "Meadow F7 Micro"
    ktweak --set-str COMPOSITE_CONFIGSTR "NuttX COMPOSITE config"
    ktweak --set-val COMPOSITE_VERSIONNO 0x1010
    ktweak --disable COMPOSITE_MSFT_OS_DESCRIPTORS
    ktweak --set-val COMPOSITE_DEVICES 8

    #
    # CDC/ACM as a composite member, twice.
    #
    ktweak --enable CDCACM_COMPOSITE
    ktweak --enable BOARD_DUAL_CDCACM

    #
    # Only valid for a stand-alone CDC/ACM device.  In a composite device the
    # descriptors come from the COMPOSITE_* symbols above and the endpoint
    # numbers are assigned by board_composite0_connect().
    #
    for symbol in VENDORID PRODUCTID VENDORSTR PRODUCTSTR \
                  EP0MAXPACKET EPINTIN EPBULKOUT EPBULKIN
    do
        ktweak --undefine CDCACM_$symbol
    done
}

#
# Enable the NuttX debug features (debug output to SYSLOG).
#
# These are deliberately off in the board's defconfig - see
# https://github.com/WildernessLabs/Meadow.OS/issues/209, where turning
# DEBUG_FEATURES on was reported to crash the board.  Treat a --debug build as a
# diagnostic tool rather than something to ship.
#
# DEBUG_FEATURES is the master switch; the ERROR/WARN/INFO options select how
# much gets emitted.  The per-subsystem options (DEBUG_FS, DEBUG_NET, DEBUG_MM
# and ~30 more) all default to 'n' and are left alone - add them here if you need
# one.
#
enable_debug_features() {
    printf "\n\n********** Enabling debug features. **********\n\n"

    ktweak --enable DEBUG_FEATURES
    ktweak --enable DEBUG_ERROR
    ktweak --enable DEBUG_WARN
    ktweak --enable DEBUG_INFO

    #
    # Binary loader debug output, used when debugging dynamically loaded
    # libraries such as Tensorflow.  Very noisy, so left off by default -
    # uncomment when you need it.
    #
    # ktweak --enable DEBUG_BINFMT
    # ktweak --enable DEBUG_BINFMT_ERROR
    # ktweak --enable DEBUG_BINFMT_WARN
}

#
# Send stack dumps to USART1 (COM1) instead of the RAMLOG.
#
# Meadow normally has no serial console - the syslog goes to the RAMLOG so it can
# be read back over HCOM.  That is no use for a hard fault, where the board stops
# before anything can read the log out, so this swaps the RAMLOG for a real
# console on USART1 and adds stack colouration to make overflows visible.
#
enable_stack_dump() {
    printf "\n\n********** Enabling stack dump to USART1 (COM1).  This will disable RAMLOG. **********\n\n"

    ktweak --enable DEV_CONSOLE
    ktweak --enable SERIAL_CONSOLE
    ktweak --enable USART1_SERIAL_CONSOLE
    ktweak --enable SYSLOG_WRITE
    ktweak --enable SYSLOG_SERIAL_CONSOLE
    ktweak --enable SYSLOG_CONSOLE

    ktweak --undefine NO_SERIAL_CONSOLE
    ktweak --undefine RAMLOG
    ktweak --undefine RAMLOG_BUFSIZE
    ktweak --undefine RAMLOG_NPOLLWAITERS
    ktweak --undefine RAMLOG_SYSLOG

    ktweak --enable STACK_COLORATION
}

#
#   Enable the sotest shared object, and optionally the test that loads it.
#
#     enable_sotests            - build the .so into the image only
#     enable_sotests --tests    - also enable the dlopen/dlsym test itself
#
#   The two are separable because only the second one costs anything.  Building
#   the module just adds a standalone .so; SOTEST_TESTS is what compiles the
#   test's entry in developer_tests.c, and that is what drags the whole ELF
#   dynamic loader into the user-space link: the table entry pulls in
#   sotest_tests.o, which calls dlopen, which pulls in elf_symbols.o, which
#   needs elf_globals.o - the generated table of every symbol exported to
#   dlopen'd modules.  At that point every symbol that table names has to
#   resolve, including toolchain libm internals that only exist in some
#   arm-none-eabi builds, so "-u=all" asks for the module without the test.
#
enable_sotests() {
    printf "\n\n********** Enabling sotest dlopen/dlsym module. **********\n\n"

    ktweak --enable EXAMPLES_SOTEST
    ktweak --set-str EXAMPLES_SOTEST_BINDIR "/meadow0"

    if [ "$1" = "--tests" ]; then
        ktweak --enable SOTEST_TESTS
    fi
}