/****************************************************************************
 * meadow_watchdog.c
 *
 *   Copyright (C) 2024 Wilderness Labs. All rights reserved.
 *   Author:  Wilderness Labs
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/*
 * Software watchdog timers for the Meadow platform.
 *
 * A watchdog is armed around each of the usrsock operations that can block
 * indefinitely if the ESP32 coprocessor stops responding.  When one expires
 * an exception is raised into the managed environment naming the operation
 * that wedged.  No hardware watchdog is involved; these are wd_start()
 * timers.
 *
 * Expiry runs from the timer interrupt (CONFIG_SCHED_TICKLESS is not set,
 * so this is the SysTick handler) in the context of whatever thread happened
 * to be interrupted, and is subject to all ISR restrictions.  Raising the
 * exception allocates from the heap and takes semaphores, neither of which
 * is legal there, so meadow_watchdog_expired() does nothing but queue
 * work on LPWORK and all of that happens on the worker thread instead.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include "meadow_watchdog.h"

#include <inttypes.h>
#include <stdbool.h>

#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/wqueue.h>

#include "../espcp/espcp_shared_enums.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/*
 * A handle packs the slot index into the low bits and the generation count of
 * that slot into the rest.  The generation is bumped every time a slot is
 * released, which makes a handle single use: a caller that was still wedged
 * when its watchdog fired cannot cancel the timer of whoever holds the slot
 * now.
 */
#define MEADOW_WATCHDOG_SLOT_BITS   8
#define MEADOW_WATCHDOG_SLOT_MASK   ((1 << MEADOW_WATCHDOG_SLOT_BITS) - 1)

#define MEADOW_WATCHDOG_HANDLE(slot, generation) \
    ((meadow_watchdog_t) (((generation) << MEADOW_WATCHDOG_SLOT_BITS) | (slot)))
#define MEADOW_WATCHDOG_HANDLE_SLOT(handle) ((handle) & MEADOW_WATCHDOG_SLOT_MASK)
#define MEADOW_WATCHDOG_HANDLE_GENERATION(handle) \
    ((uint16_t) ((handle) >> MEADOW_WATCHDOG_SLOT_BITS))

#if CONFIG_MEADOW_WATCHDOG_SLOTS > (MEADOW_WATCHDOG_SLOT_MASK + 1)
#  error "CONFIG_MEADOW_WATCHDOG_SLOTS does not fit in a watchdog handle"
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/*
 * The struct wdog_s cannot live on the caller's stack.  wd_start() links it
 * into the kernel's active timer list, so any path that returned without
 * cancelling would leave that list pointing into a dead stack frame, which
 * wd_timer() would later write through.
 */
struct meadow_watchdog_slot_s
{
    struct wdog_s             wdog;        /* Kernel timer; linked into g_wdactivelist */
    meadow_watchdog_methods_e method;      /* The operation being guarded */
    uint16_t                  generation;  /* Bumped every time the slot is released */
    bool                      inuse;       /* Slot has been handed out to a caller */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/*
 *  One entry per concurrently armed watchdog.
 */
static struct meadow_watchdog_slot_s g_meadow_watchdogs[CONFIG_MEADOW_WATCHDOG_SLOTS];

/* 
 * One work item per method, so that two watchdogs guarding different
 * operations cannot displace each other's expiry.  Re-queueing a work_s that
 * is already queued replaces it, which here can only discard a duplicate of
 * the exception already pending for that same operation.
 */
static struct work_s g_meadow_watchdog_work[MEADOW_WATCHDOG_METHOD_COUNT];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_watchdog_worker
 *
 * Description:
 *   Raise the exception for an expired watchdog.  Runs on the LPWORK thread,
 *   which has a real stack and none of the restrictions that apply to the
 *   expiry callback itself.
 *
 * Input Parameters:
 *   arg - The meadow_watchdog_methods_e value of the expired watchdog.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
static void meadow_watchdog_worker(void *arg)
{
    meadow_watchdog_methods_e method;
    espcp_status_codes_t status;

    method = (meadow_watchdog_methods_e) (uintptr_t) arg;
    syslog(LOG_ERR, "Detected a network deadlock in method %" PRIu32 ".  Propagating "
           "OS exception to the managed environment...\n", (uint32_t) method);
    switch (method)
    {
        case POLL_WATCHDOG:
            status = espcp_status_codes_poll_deadlock;
            break;
        case CLOSE_WATCHDOG:
            status = espcp_status_codes_close_deadlock;
            break;
        case SOCKET_WATCHDOG:
            status = espcp_status_codes_socket_deadlock;
            break;
        default:
            status = espcp_status_codes_network_deadlock;
            break;
    }
    meadow_os_raise_simple_exception((uint32_t) status);
}

/****************************************************************************
 * Name: meadow_watchdog_expired
 *
 * Description:
 *   Watchdog expiry callback.  This runs in interrupt context, so it does
 *   nothing but hand the method over to the low priority work queue.
 *
 * Input Parameters:
 *   arg - Index of the watchdog slot that expired, as passed to wd_start().
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
static void meadow_watchdog_expired(wdparm_t arg)
{
    unsigned int index = (unsigned int) arg;
    meadow_watchdog_methods_e method;
    irqstate_t flags;

    if (index >= CONFIG_MEADOW_WATCHDOG_SLOTS)
    {
        return;
    }
    //
    //  Release the slot here rather than leaving it to the caller.  The caller
    //  may be wedged for good - nothing unblocks espcp_queue_message() except
    //  a matching response from the ESP32 - and if the slot were held until it
    //  returned then a link that stays down would strand every slot in turn
    //  and the deadlock would stop being reported at all.  The handle the
    //  caller holds carries the old generation, so the deactivate it may still
    //  make once it unwinds is ignored.
    //
    flags = enter_critical_section();
    method = g_meadow_watchdogs[index].method;
    g_meadow_watchdogs[index].generation++;
    g_meadow_watchdogs[index].inuse = false;
    leave_critical_section(flags);

    if ((unsigned int) method >= MEADOW_WATCHDOG_METHOD_COUNT)
    {
        return;
    }
    work_queue(LPWORK, &g_meadow_watchdog_work[method],
               meadow_watchdog_worker, (void *) (uintptr_t) method, 0);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: meadow_watchdog_activate
 *
 * Description:
 *   Arm a watchdog timer for the given operation.
 *
 * Input Parameters:
 *   timeout - Timeout value in milliseconds.
 *   method  - The operation being guarded.
 *
 * Returned Value:
 *   A handle to be passed to meadow_watchdog_deactivate(), or
 *   MEADOW_WATCHDOG_NO_SLOT if no watchdog could be armed.  In the latter
 *   case the caller simply runs unguarded; the handle can still be passed to
 *   meadow_watchdog_deactivate().
 *
 ****************************************************************************/
meadow_watchdog_t meadow_watchdog_activate(uint32_t timeout, meadow_watchdog_methods_e method)
{
    irqstate_t flags;
    uint16_t generation = 0;
    int slot;
    int ret;

    if ((unsigned int) method >= MEADOW_WATCHDOG_METHOD_COUNT)
    {
        syslog(LOG_ERR, "Invalid watchdog method: %" PRIu32 "\n", (uint32_t) method);
        return (MEADOW_WATCHDOG_NO_SLOT);
    }
    //
    //  Claim a free slot.  This races the expiry callback, which releases the
    //  slot its watchdog was armed with, so both sides work under a critical
    //  section.
    //
    flags = enter_critical_section();
    for (slot = 0; slot < CONFIG_MEADOW_WATCHDOG_SLOTS; slot++)
    {
        if (!g_meadow_watchdogs[slot].inuse)
        {
            g_meadow_watchdogs[slot].inuse = true;
            g_meadow_watchdogs[slot].method = method;
            generation = g_meadow_watchdogs[slot].generation;
            break;
        }
    }
    leave_critical_section(flags);

    if (slot >= CONFIG_MEADOW_WATCHDOG_SLOTS)
    {
        syslog(LOG_WARNING, "No free watchdog slot for method %" PRIu32 ", the operation "
               "will run unguarded\n", (uint32_t) method);
        return (MEADOW_WATCHDOG_NO_SLOT);
    }
    //
    //  A free slot is never linked into the kernel timer list: it has either
    //  never been started, or it has been cancelled, or it has expired, and
    //  both wd_cancel() and expiry unlink the node.  wd_start() fills in the
    //  rest of the structure, so there is nothing to clear here - clearing it
    //  would be what corrupts the timer list if the slot were still linked.
    //
    ret = wd_start(&g_meadow_watchdogs[slot].wdog, MSEC2TICK(timeout),
                   meadow_watchdog_expired, (wdparm_t) slot);
    if (ret < 0)
    {
        syslog(LOG_ERR, "Failed to activate watchdog: %d\n", ret);
        flags = enter_critical_section();
        g_meadow_watchdogs[slot].generation++;
        g_meadow_watchdogs[slot].inuse = false;
        leave_critical_section(flags);

        return (MEADOW_WATCHDOG_NO_SLOT);
    }

    return (MEADOW_WATCHDOG_HANDLE(slot, generation));
}

/****************************************************************************
 * Name: meadow_watchdog_deactivate
 *
 * Description:
 *   Cancel a watchdog timer and release its slot.
 *
 * Input Parameters:
 *   watchdog - Handle returned by meadow_watchdog_activate().
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
void meadow_watchdog_deactivate(meadow_watchdog_t watchdog)
{
    struct meadow_watchdog_slot_s *slot;
    irqstate_t flags;
    int index;

    if (watchdog < 0)
    {
        return;
    }

    index = MEADOW_WATCHDOG_HANDLE_SLOT(watchdog);
    if (index >= CONFIG_MEADOW_WATCHDOG_SLOTS)
    {
        return;
    }

    slot = &g_meadow_watchdogs[index];
    flags = enter_critical_section();
    //
    //  A handle left over from an earlier use of this slot is ignored.  Its
    //  watchdog has already fired and released the slot, which may now belong
    //  to another caller whose timer must not be cancelled here.
    //
    if (slot->inuse && (MEADOW_WATCHDOG_HANDLE_GENERATION(watchdog) == slot->generation))
    {
        //
        //  An expired watchdog has already been unlinked by wd_timer(), and
        //  wd_cancel() would report -EINVAL for it.
        //
        if (WDOG_ISACTIVE(&slot->wdog))
        {
            int ret = wd_cancel(&slot->wdog);
            if (ret < 0)
            {
                syslog(LOG_ERR, "Failed to deactivate watchdog: %d\n", ret);
            }
        }

        slot->generation++;
        slot->inuse = false;
    }

    leave_critical_section(flags);
}
