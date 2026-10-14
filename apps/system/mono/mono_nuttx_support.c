/****************************************************************************
 * apps/system/mono/mono_nuttx_support.c
 *
 *   Copyright (C) 2026 Wilderness Labs. All rights reserved.
 *
 *   Symbols the Mono runtime archives need that neither the runtime nor the
 *   NuttX 12 user-space libraries provide, or that NuttX provides in a form
 *   that does not work in a protected build.
 *
 *   The NuttX 7 firmware this was ported from also carried errno-syncing
 *   --wrap wrappers, a global __errno(), an mmap/munmap replacement and stubs
 *   for getrlimit, gettid, posix_memalign, realpath, mkdtemp, lstat and
 *   others.  NuttX 12 provides all of those (errno is per-thread in user
 *   space, and file mmap() is served by CONFIG_FS_RAMMAP), so they are not
 *   needed and would collide with or shadow the libc versions.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <syslog.h>

#include <nuttx/cache.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Opaque Mono types used only by the prototypes below. */

typedef void MonoContext;
typedef void MonoThreadHandle;
typedef void MonoThreadUnwindState;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Mono native crash and signal handling
 *
 * These are normally provided by mini-posix.c, which is not built for
 * NuttX.  Faults are reported by NuttX's own fault handlers instead.
 ****************************************************************************/

void mono_dump_native_crash_info(const char *signal, MonoContext *mctx,
                                 void *info)
{
  (void)mctx;
  (void)info;
  syslog(LOG_ERR, "MONO CRASH: signal=%s\n", signal ? signal : "unknown");
}

void mono_post_native_crash_handler(const char *signal, MonoContext *mctx,
                                    void *info, int crash_chaining)
{
  (void)mctx;
  (void)info;
  syslog(LOG_ERR, "MONO POST-CRASH: signal=%s, chaining=%d\n",
         signal ? signal : "unknown", crash_chaining);
}

int mono_chain_signal(int signal, void *siginfo, void *ctx,
                      int chain_to_other)
{
  (void)signal;
  (void)siginfo;
  (void)ctx;
  (void)chain_to_other;
  return 0;
}

void mono_chain_signal_to_default_sigsegv_handler(void)
{
  syslog(LOG_ERR, "MONO: SIGSEGV chain requested (not supported on NuttX)\n");
}

void mono_runtime_install_handlers(void)
{
}

void mono_runtime_setup_stat_profiler(void)
{
}

void mono_init_native_crash_info(void)
{
}

int mono_thread_state_init_from_handle(MonoThreadUnwindState *tctx,
                                       MonoThreadHandle *info,
                                       void *sigctx)
{
  (void)tctx;
  (void)info;
  (void)sigctx;
  return 0;
}

/****************************************************************************
 * Name: up_invalidate_icache
 *
 * Description:
 *   Called by the JIT (mono_arch_flush_icache) after it writes code.  The
 *   cache maintenance registers are only accessible in privileged mode, so
 *   use the up_coherent_dcache() syscall, which cleans the D-cache over the
 *   range and then invalidates the whole I-cache.  Without this, stale
 *   I-cache lines produce non-deterministic UsageFaults (UNDEFINSTR) when
 *   new JIT code is executed.
 *
 ****************************************************************************/

void up_invalidate_icache(uintptr_t start, uintptr_t end)
{
  if (end > start)
    {
      up_coherent_dcache(start, end - start);
    }
}

/****************************************************************************
 * Name: __sync_val_compare_and_swap_8
 *
 * Description:
 *   The Cortex-M7 has no 64-bit exclusive load/store, so GCC calls this
 *   helper for 64-bit compare-and-swap.  NuttX provides a weak version
 *   (libs/libc/machine/arch_atomic.c) that masks interrupts, but masking
 *   interrupts has no effect in unprivileged thread mode, so in this
 *   protected build that version is not atomic.  Disable pre-emption
 *   instead; the runtime never runs this from an interrupt handler.
 *
 ****************************************************************************/

uint64_t __sync_val_compare_and_swap_8(volatile void *ptr, uint64_t oldval,
                                       uint64_t newval)
{
  volatile uint64_t *value = (volatile uint64_t *)ptr;
  uint64_t previous;

  sched_lock();
  previous = *value;
  if (previous == oldval)
    {
      *value = newval;
    }

  sched_unlock();
  return previous;
}

/****************************************************************************
 * Name: mprotect, posix_madvise
 *
 * Description:
 *   Declared by NuttX's <sys/mman.h> but not implemented.  Memory protection
 *   is by MPU region, not by page, so there is nothing to change; and there
 *   is no paging to advise.
 *
 ****************************************************************************/

int mprotect(void *addr, size_t len, int prot)
{
  (void)addr;
  (void)len;
  (void)prot;
  return 0;
}

int posix_madvise(void *addr, size_t len, int advice)
{
  (void)addr;
  (void)len;
  (void)advice;
  return 0;
}

/****************************************************************************
 * Name: readlink
 *
 * Description:
 *   NuttX only provides readlink() with CONFIG_PSEUDOFS_SOFTLINKS, which this
 *   build does not use.  There are no symbolic links, so every path is
 *   "not a link".
 *
 ****************************************************************************/

ssize_t readlink(const char *path, char *buf, size_t bufsize)
{
  (void)path;
  (void)buf;
  (void)bufsize;
  set_errno(EINVAL);
  return -1;
}
