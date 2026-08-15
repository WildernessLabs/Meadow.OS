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
    ktweak --enable READLINE_HAVE_EXTMATCH
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
#   Enable the dlopen/dlsym tests built into NuttX.
#
enable_sotests() {
    printf "\n\n********** Enabling dlopen/dlsym tests. **********\n\n"

    ktweak --enable SOTEST_TESTS
    ktweak --enable EXAMPLES_SOTEST
    ktweak --set-str EXAMPLES_SOTEST_BINDIR "/meadow0"
}