#ifdef __CYGWIN__
#  define SYMBOL(s) _##s
#  define GLOBAL .global
#  define SECTION .data
	.macro GLOBAL ep
	.global	SYMBOL(\ep)
	.type	SYMBOL(\ep), "object"
	.endm
	.macro SIZE ep
	.endm
#elif defined(__ELF__)
#  define SYMBOL(s) s
#  define SECTION .data
	.macro GLOBAL ep
	.global	SYMBOL(\ep)
	.type	SYMBOL(\ep), "object"
	.endm
	.macro SIZE ep
	.size	SYMBOL(\ep), . - SYMBOL(\ep)
	.endm
#else
#  define SYMBOL(s) _##s
#  define SECTION .section __DATA,__data
	.macro GLOBAL ep
	.private_extern SYMBOL(\ep)
	.globl 	SYMBOL(\ep)
	.endm
	.macro SIZE ep
	.endm
#endif

#if __SIZEOF_POINTER__ == 8
	.macro globalEntry index, ep
	.quad	.l\index
	.quad	\ep
	.endm
# define ALIGN 8
#else
	.macro globalEntry index, ep
	.long	.l\index
	.long	\ep
	.endm
# define ALIGN 4
#endif
#ifdef __ARM_ARCH_ISA_THUMB2
# ifdef __ARM_ARCH_7M__
	.arch armv7e-m
# elif defined ___ARM_ARCH 8
	.arch armv8-m.base
#endif
#ifdef __ARM_ASM_SYNTAX_UNIFIED__
	.syntax	unified
#endif
	.thumb
#endif
	.data
	.align	ALIGN
	GLOBAL	globalNames

SYMBOL(globalNames):
.l0:	.string	"__adddf3"
.l1:	.string	"__aeabi_d2ulz"
.l2:	.string	"__aeabi_dadd"
.l3:	.string	"__aeabi_drsub"
.l4:	.string	"__aeabi_dsub"
.l5:	.string	"__aeabi_f2d"
.l6:	.string	"__aeabi_i2d"
.l7:	.string	"__aeabi_idiv0"
.l8:	.string	"__aeabi_l2d"
.l9:	.string	"__aeabi_ldiv0"
.l10:	.string	"__aeabi_ldivmod"
.l11:	.string	"__aeabi_ui2d"
.l12:	.string	"__aeabi_ul2d"
.l13:	.string	"__aeabi_uldivmod"
.l14:	.string	"__assert"
.l15:	.string	"__dtoa_engine"
.l16:	.string	"__errno"
.l17:	.string	"__exidx_end"
.l18:	.string	"__exidx_start"
.l19:	.string	"__extendsfdf2"
.l20:	.string	"__fixunsdfdi"
.l21:	.string	"__floatdidf"
.l22:	.string	"__floatsidf"
.l23:	.string	"__floatundidf"
.l24:	.string	"__floatunsidf"
.l25:	.string	"__subdf3"
.l26:	.string	"__udivmoddi4"
.l27:	.string	"__ultoa_invert"
.l28:	.string	"_assert"
.l29:	.string	"_ebss"
.l30:	.string	"_edata"
.l31:	.string	"_einit"
.l32:	.string	"_eronly"
.l33:	.string	"_etext"
.l34:	.string	"_exit"
.l35:	.string	"_sbss"
.l36:	.string	"_sdata"
.l37:	.string	"_sinit"
.l38:	.string	"_stext"
.l39:	.string	"abort"
.l40:	.string	"accept4"
.l41:	.string	"atoi"
.l42:	.string	"bind"
.l43:	.string	"calloc"
.l44:	.string	"clock"
.l45:	.string	"close"
.l46:	.string	"connect"
.l47:	.string	"dns_bind"
.l48:	.string	"dns_breaklock"
.l49:	.string	"dns_clear_answer"
.l50:	.string	"dns_find_answer"
.l51:	.string	"dns_foreach_nameserver"
.l52:	.string	"dns_is_queryfamily"
.l53:	.string	"dns_lock"
.l54:	.string	"dns_query"
.l55:	.string	"dns_restorelock"
.l56:	.string	"dns_save_answer"
.l57:	.string	"dns_unlock"
.l58:	.string	"espcp_encode_event_data"
.l59:	.string	"espcp_encode_uint32"
.l60:	.string	"fclose"
.l61:	.string	"fcntl"
.l62:	.string	"fdopen"
.l63:	.string	"ferror"
.l64:	.string	"fflush"
.l65:	.string	"fgetc"
.l66:	.string	"fgetc_unlocked"
.l67:	.string	"fgets"
.l68:	.string	"fgets_unlocked"
.l69:	.string	"flockfile"
.l70:	.string	"fopen"
.l71:	.string	"fprintf"
.l72:	.string	"fputc"
.l73:	.string	"fputc_unlocked"
.l74:	.string	"fputs"
.l75:	.string	"fputs_unlocked"
.l76:	.string	"fread"
.l77:	.string	"fread_unlocked"
.l78:	.string	"free"
.l79:	.string	"fseek"
.l80:	.string	"fseeko"
.l81:	.string	"fstat"
.l82:	.string	"fsync"
.l83:	.string	"ftell"
.l84:	.string	"ftello"
.l85:	.string	"funlockfile"
.l86:	.string	"fwrite"
.l87:	.string	"fwrite_unlocked"
.l88:	.string	"getenv"
.l89:	.string	"gethostentbyname_r"
.l90:	.string	"getpid"
.l91:	.string	"getrandom"
.l92:	.string	"getsockname"
.l93:	.string	"getsockopt"
.l94:	.string	"gettimeofday"
.l95:	.string	"gmtime_r"
.l96:	.string	"inet_ntop"
.l97:	.string	"inet_pton"
.l98:	.string	"ioctl"
.l99:	.string	"isprint"
.l100:	.string	"isspace"
.l101:	.string	"listen"
.l102:	.string	"lseek"
.l103:	.string	"malloc"
.l104:	.string	"meadow_cloud_provision"
.l105:	.string	"meadow_cloud_release_private_key"
.l106:	.string	"meadow_cloud_retrieve_private_key"
.l107:	.string	"meadow_hw_version_get"
.l108:	.string	"meadow_os_config_free_resources"
.l109:	.string	"meadow_os_deep_copy_config"
.l110:	.string	"meadow_os_espcp_enter_programming_mode"
.l111:	.string	"meadow_os_espcp_monitor_process_line"
.l112:	.string	"meadow_os_espcp_reset"
.l113:	.string	"meadow_os_reset_board"
.l114:	.string	"memchr"
.l115:	.string	"memcmp"
.l116:	.string	"memcpy"
.l117:	.string	"memmove"
.l118:	.string	"memset"
.l119:	.string	"mkdir"
.l120:	.string	"mount"
.l121:	.string	"open"
.l122:	.string	"parse_hostfile"
.l123:	.string	"perror"
.l124:	.string	"pipe2"
.l125:	.string	"poll"
.l126:	.string	"pthread_cancel"
.l127:	.string	"pthread_exit"
.l128:	.string	"pthread_join"
.l129:	.string	"read"
.l130:	.string	"realloc"
.l131:	.string	"recv"
.l132:	.string	"recvfrom"
.l133:	.string	"remove"
.l134:	.string	"rename"
.l135:	.string	"rmdir"
.l136:	.string	"select"
.l137:	.string	"sem_destroy"
.l138:	.string	"sem_init"
.l139:	.string	"sem_post"
.l140:	.string	"sem_setprotocol"
.l141:	.string	"sem_wait"
.l142:	.string	"send"
.l143:	.string	"setlogmask"
.l144:	.string	"setsockopt"
.l145:	.string	"shutdown"
.l146:	.string	"sigaddset"
.l147:	.string	"sigemptyset"
.l148:	.string	"signal"
.l149:	.string	"sigprocmask"
.l150:	.string	"sleep"
.l151:	.string	"snprintf"
.l152:	.string	"socket"
.l153:	.string	"stat"
.l154:	.string	"statfs"
.l155:	.string	"strchr"
.l156:	.string	"strcmp"
.l157:	.string	"strcpy"
.l158:	.string	"strcspn"
.l159:	.string	"strerror"
.l160:	.string	"strlcat"
.l161:	.string	"strlcpy"
.l162:	.string	"strlen"
.l163:	.string	"strncmp"
.l164:	.string	"strncpy"
.l165:	.string	"strnlen"
.l166:	.string	"strrchr"
.l167:	.string	"strspn"
.l168:	.string	"strtok_r"
.l169:	.string	"strtol"
.l170:	.string	"strtoul"
.l171:	.string	"syslog"
.l172:	.string	"time"
.l173:	.string	"tls_cleanup_pop"
.l174:	.string	"tls_cleanup_popall"
.l175:	.string	"tls_cleanup_push"
.l176:	.string	"tls_destruct"
.l177:	.string	"tls_get_info"
.l178:	.string	"tolower"
.l179:	.string	"unlink"
.l180:	.string	"usleep"
.l181:	.string	"vfprintf"
.l182:	.string	"vsnprintf"
.l183:	.string	"vsyslog"
.l184:	.string	"waitpid"
.l185:	.string	"work_cancel"
.l186:	.string	"work_queue"
.l187:	.string	"write"
.l188:	.string	"zalloc"
	SIZE	globalNames

	.align	ALIGN
	GLOBAL	nglobals
SYMBOL(nglobals):
	.word	189
	SIZE	nglobals

	.align	ALIGN
	GLOBAL	global_table
SYMBOL(global_table):
	globalEntry 0, __adddf3
	globalEntry 1, __aeabi_d2ulz
	globalEntry 2, __aeabi_dadd
	globalEntry 3, __aeabi_drsub
	globalEntry 4, __aeabi_dsub
	globalEntry 5, __aeabi_f2d
	globalEntry 6, __aeabi_i2d
	globalEntry 7, __aeabi_idiv0
	globalEntry 8, __aeabi_l2d
	globalEntry 9, __aeabi_ldiv0
	globalEntry 10, __aeabi_ldivmod
	globalEntry 11, __aeabi_ui2d
	globalEntry 12, __aeabi_ul2d
	globalEntry 13, __aeabi_uldivmod
	globalEntry 14, __assert
	globalEntry 15, __dtoa_engine
	globalEntry 16, __errno
	globalEntry 17, __exidx_end
	globalEntry 18, __exidx_start
	globalEntry 19, __extendsfdf2
	globalEntry 20, __fixunsdfdi
	globalEntry 21, __floatdidf
	globalEntry 22, __floatsidf
	globalEntry 23, __floatundidf
	globalEntry 24, __floatunsidf
	globalEntry 25, __subdf3
	globalEntry 26, __udivmoddi4
	globalEntry 27, __ultoa_invert
	globalEntry 28, _assert
	globalEntry 29, _ebss
	globalEntry 30, _edata
	globalEntry 31, _einit
	globalEntry 32, _eronly
	globalEntry 33, _etext
	globalEntry 34, _exit
	globalEntry 35, _sbss
	globalEntry 36, _sdata
	globalEntry 37, _sinit
	globalEntry 38, _stext
	globalEntry 39, abort
	globalEntry 40, accept4
	globalEntry 41, atoi
	globalEntry 42, bind
	globalEntry 43, calloc
	globalEntry 44, clock
	globalEntry 45, close
	globalEntry 46, connect
	globalEntry 47, dns_bind
	globalEntry 48, dns_breaklock
	globalEntry 49, dns_clear_answer
	globalEntry 50, dns_find_answer
	globalEntry 51, dns_foreach_nameserver
	globalEntry 52, dns_is_queryfamily
	globalEntry 53, dns_lock
	globalEntry 54, dns_query
	globalEntry 55, dns_restorelock
	globalEntry 56, dns_save_answer
	globalEntry 57, dns_unlock
	globalEntry 58, espcp_encode_event_data
	globalEntry 59, espcp_encode_uint32
	globalEntry 60, fclose
	globalEntry 61, fcntl
	globalEntry 62, fdopen
	globalEntry 63, ferror
	globalEntry 64, fflush
	globalEntry 65, fgetc
	globalEntry 66, fgetc_unlocked
	globalEntry 67, fgets
	globalEntry 68, fgets_unlocked
	globalEntry 69, flockfile
	globalEntry 70, fopen
	globalEntry 71, fprintf
	globalEntry 72, fputc
	globalEntry 73, fputc_unlocked
	globalEntry 74, fputs
	globalEntry 75, fputs_unlocked
	globalEntry 76, fread
	globalEntry 77, fread_unlocked
	globalEntry 78, free
	globalEntry 79, fseek
	globalEntry 80, fseeko
	globalEntry 81, fstat
	globalEntry 82, fsync
	globalEntry 83, ftell
	globalEntry 84, ftello
	globalEntry 85, funlockfile
	globalEntry 86, fwrite
	globalEntry 87, fwrite_unlocked
	globalEntry 88, getenv
	globalEntry 89, gethostentbyname_r
	globalEntry 90, getpid
	globalEntry 91, getrandom
	globalEntry 92, getsockname
	globalEntry 93, getsockopt
	globalEntry 94, gettimeofday
	globalEntry 95, gmtime_r
	globalEntry 96, inet_ntop
	globalEntry 97, inet_pton
	globalEntry 98, ioctl
	globalEntry 99, isprint
	globalEntry 100, isspace
	globalEntry 101, listen
	globalEntry 102, lseek
	globalEntry 103, malloc
	globalEntry 104, meadow_cloud_provision
	globalEntry 105, meadow_cloud_release_private_key
	globalEntry 106, meadow_cloud_retrieve_private_key
	globalEntry 107, meadow_hw_version_get
	globalEntry 108, meadow_os_config_free_resources
	globalEntry 109, meadow_os_deep_copy_config
	globalEntry 110, meadow_os_espcp_enter_programming_mode
	globalEntry 111, meadow_os_espcp_monitor_process_line
	globalEntry 112, meadow_os_espcp_reset
	globalEntry 113, meadow_os_reset_board
	globalEntry 114, memchr
	globalEntry 115, memcmp
	globalEntry 116, memcpy
	globalEntry 117, memmove
	globalEntry 118, memset
	globalEntry 119, mkdir
	globalEntry 120, mount
	globalEntry 121, open
	globalEntry 122, parse_hostfile
	globalEntry 123, perror
	globalEntry 124, pipe2
	globalEntry 125, poll
	globalEntry 126, pthread_cancel
	globalEntry 127, pthread_exit
	globalEntry 128, pthread_join
	globalEntry 129, read
	globalEntry 130, realloc
	globalEntry 131, recv
	globalEntry 132, recvfrom
	globalEntry 133, remove
	globalEntry 134, rename
	globalEntry 135, rmdir
	globalEntry 136, select
	globalEntry 137, sem_destroy
	globalEntry 138, sem_init
	globalEntry 139, sem_post
	globalEntry 140, sem_setprotocol
	globalEntry 141, sem_wait
	globalEntry 142, send
	globalEntry 143, setlogmask
	globalEntry 144, setsockopt
	globalEntry 145, shutdown
	globalEntry 146, sigaddset
	globalEntry 147, sigemptyset
	globalEntry 148, signal
	globalEntry 149, sigprocmask
	globalEntry 150, sleep
	globalEntry 151, snprintf
	globalEntry 152, socket
	globalEntry 153, stat
	globalEntry 154, statfs
	globalEntry 155, strchr
	globalEntry 156, strcmp
	globalEntry 157, strcpy
	globalEntry 158, strcspn
	globalEntry 159, strerror
	globalEntry 160, strlcat
	globalEntry 161, strlcpy
	globalEntry 162, strlen
	globalEntry 163, strncmp
	globalEntry 164, strncpy
	globalEntry 165, strnlen
	globalEntry 166, strrchr
	globalEntry 167, strspn
	globalEntry 168, strtok_r
	globalEntry 169, strtol
	globalEntry 170, strtoul
	globalEntry 171, syslog
	globalEntry 172, time
	globalEntry 173, tls_cleanup_pop
	globalEntry 174, tls_cleanup_popall
	globalEntry 175, tls_cleanup_push
	globalEntry 176, tls_destruct
	globalEntry 177, tls_get_info
	globalEntry 178, tolower
	globalEntry 179, unlink
	globalEntry 180, usleep
	globalEntry 181, vfprintf
	globalEntry 182, vsnprintf
	globalEntry 183, vsyslog
	globalEntry 184, waitpid
	globalEntry 185, work_cancel
	globalEntry 186, work_queue
	globalEntry 187, write
	globalEntry 188, zalloc
	SIZE	global_table
