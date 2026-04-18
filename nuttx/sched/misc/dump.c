/****************************************************************************
 * sched/misc/dump.c
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

#include <nuttx/arch.h>
<<<<<<<< HEAD:nuttx/sched/misc/dump.c
#include <nuttx/fs/fs.h>
========
#include <nuttx/irq.h>
>>>>>>>> 9e8753d6256619a7bfa72f4596eb1e481f1c97a2:nuttx/arch/z16/src/common/z16_exit.c

#include <debug.h>

/****************************************************************************
<<<<<<<< HEAD:nuttx/sched/misc/dump.c
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_DUMP_ON_EXIT

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dumphandler
 *
 * Description:
 *   Dump the state of all tasks whenever on task exits.  This is debug
 *   instrumentation that was added to check file-related reference counting
 *   but could be useful again sometime in the future.
 *
 ****************************************************************************/

static void dumphandler(FAR struct tcb_s *tcb, FAR void *arg)
{
  FAR struct filelist *filelist;
  int i;
  int j;

  sinfo("  TCB=%p name=%s\n", tcb, tcb->name);
  sinfo("    priority=%d state=%d\n", tcb->sched_priority, tcb->task_state);

  filelist = &tcb->group->tg_filelist;
  for (i = 0; i < filelist->fl_rows; i++)
    {
      for (j = 0; j < CONFIG_NFILE_DESCRIPTORS_PER_BLOCK; j++)
        {
          struct inode *inode = filelist->fl_files[i][j].f_inode;
          if (inode)
            {
              sinfo("      fd=%d refcount=%d\n",
                    i * CONFIG_NFILE_DESCRIPTORS_PER_BLOCK + j,
                    inode->i_crefs);
            }
        }
    }
}

/****************************************************************************
========
>>>>>>>> 9e8753d6256619a7bfa72f4596eb1e481f1c97a2:nuttx/arch/z16/src/common/z16_exit.c
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsched_dumponexit
 *
 * Description:
 *   Dump the state of all tasks whenever on task exits.  This is debug
 *   instrumentation that was added to check file-related reference counting
 *   but could be useful again sometime in the future.
 *
 ****************************************************************************/

void nxsched_dumponexit(void)
{
<<<<<<<< HEAD:nuttx/sched/misc/dump.c
  sinfo("Other tasks:\n");
  nxsched_foreach(dumphandler, NULL);
========
  FAR struct tcb_s *tcb = this_task();

  /* Make sure that we are in a critical section with local interrupts.
   * The IRQ state will be restored when the next task is started.
   */

  enter_critical_section();

  nxsched_dumponexit();

  /* Destroy the task at the head of the ready to run list. */

  nxtask_exit();

  /* Now, perform the context switch to the new ready-to-run task at the
   * head of the list.
   */

  tcb = this_task();
  sinfo("New Active Task TCB=%p\n", tcb);

  /* Adjusts time slice for SCHED_RR & SCHED_SPORADIC cases */

  nxsched_resume_scheduler(tcb);

  /* Then switch contexts */

  RESTORE_USERCONTEXT(tcb);
>>>>>>>> 9e8753d6256619a7bfa72f4596eb1e481f1c97a2:nuttx/arch/z16/src/common/z16_exit.c
}

#endif /* CONFIG_DUMP_ON_EXIT */
