/****************************************************************************
 * sched/wqueue/kwork_cancel.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <errno.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/queue.h>
#include <nuttx/wqueue.h>

#include "wqueue/wqueue.h"

#ifdef CONFIG_SCHED_WORKQUEUE

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int work_qcancel(FAR struct kwork_wqueue_s *wqueue, bool sync,
                        FAR struct work_s *work)
{
  irqstate_t flags;
  int ret = -ENOENT;

  if (wqueue == NULL || work == NULL)
    {
      return -EINVAL;
    }

  /* Cancelling the work is simply a matter of removing the work structure
   * from the work queue.  This must be done with interrupts disabled because
   * new work is typically added to the work queue from interrupt handlers.
   */

  flags = enter_critical_section();
  if (work->worker != NULL)
    {
      /* Remove the entry from the work queue and make sure that it is
       * marked as available (i.e., the worker field is nullified).
       */

      if (WDOG_ISACTIVE(&work->u.timer))
        {
          wd_cancel(&work->u.timer);
        }
      else
        {
          dq_rem((FAR dq_entry_t *)work, &wqueue->q);
        }

      work->worker = NULL;
      ret = OK;
    }
  else if (!up_interrupt_context() && !sched_idletask() && sync)
    {
      int wndx;

      for (wndx = 0; wndx < wqueue->nthreads; wndx++)
        {
          if (wqueue->worker[wndx].work == work &&
              wqueue->worker[wndx].pid != nxsched_gettid())
            {
              nxsem_wait_uninterruptible(&wqueue->worker[wndx].wait);
              ret = 1;
              break;
            }
        }
    }

  leave_critical_section(flags);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: work_cancel/work_cancel_wq
 *
 * Description:
 *   Cancel previously queued work.  This removes work from the work queue.
 *   After work has been cancelled, it may be requeued by calling
 *   work_queue() again.
 *
 * Input Parameters:
 *   qid    - The work queue ID (must be HPWORK or LPWORK)
 *   wqueue - The work queue handle
 *   work   - The previously queued work structure to cancel
 *
 * Returned Value:
 *   Zero on success, a negated errno on failure
 *
 *   -ENOENT - There is no such work queued.
 *   -EINVAL - An invalid work queue was specified
 *
 ****************************************************************************/

int work_cancel(int qid, FAR struct work_s *work)
{
  return work_qcancel(work_qid2wq(qid), false, work);
}

<<<<<<< HEAD
      return work_qcancel((FAR struct kwork_wqueue_s *)&g_hpwork,
                          -1, work);
    }
  else
#endif
#ifdef CONFIG_SCHED_LPWORK
  if (qid == LPWORK)
    {
      /* Cancel low priority work */

      return work_qcancel((FAR struct kwork_wqueue_s *)&g_lpwork,
                          -1, work);
    }
  else
#endif
    {
      return -EINVAL;
    }
=======
int work_cancel_wq(FAR struct kwork_wqueue_s *wqueue,
                   FAR struct work_s *work)
{
  return work_qcancel(wqueue, false, work);
>>>>>>> 5d8cdeaea847bcaf0b9145dd922e0a83045067ca
}

/****************************************************************************
 * Name: work_cancel_sync/work_cancel_sync_wq
 *
 * Description:
 *   Blocked cancel previously queued user-mode work.  This removes work
 *   from the user mode work queue.  After work has been cancelled, it may
 *   be requeued by calling work_queue() again.
 *
 * Input Parameters:
 *   qid    - The work queue ID (must be HPWORK or LPWORK)
 *   wqueue - The work queue handle
 *   work   - The previously queued work structure to cancel
 *
 * Returned Value:
 *   Zero means the work was successfully cancelled.
 *   One means the work was not cancelled because it is currently being
 *   processed by work thread, but wait for it to finish.
 *   A negated errno value is returned on any failure:
 *
 *   -ENOENT - There is no such work queued.
 *   -EINVAL - An invalid work queue was specified
 *
 ****************************************************************************/

int work_cancel_sync(int qid, FAR struct work_s *work)
{
  return work_qcancel(work_qid2wq(qid), true, work);
}

<<<<<<< HEAD
      return work_qcancel((FAR struct kwork_wqueue_s *)&g_hpwork,
                          CONFIG_SCHED_HPNTHREADS, work);
    }
  else
#endif
#ifdef CONFIG_SCHED_LPWORK
  if (qid == LPWORK)
    {
      /* Cancel low priority work */

      return work_qcancel((FAR struct kwork_wqueue_s *)&g_lpwork,
                          CONFIG_SCHED_LPNTHREADS, work);
    }
  else
#endif
    {
      return -EINVAL;
    }
=======
int work_cancel_sync_wq(FAR struct kwork_wqueue_s *wqueue,
                        FAR struct work_s *work)
{
  return work_qcancel(wqueue, true, work);
>>>>>>> 5d8cdeaea847bcaf0b9145dd922e0a83045067ca
}

#endif /* CONFIG_SCHED_WORKQUEUE */
