/****************************************************************************
 * sched/instrument/stack_record.c
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

#include <nuttx/instrument.h>
#include <sys/param.h>

<<<<<<<< HEAD:nuttx/sched/instrument/stack_record.c
#include "sched/sched.h"
========
#include <sys/types.h>

#include <debug.h>
#include <assert.h>

#include "chip.h"

#include "nrf_modem.h"
#include "nrf_modem_at.h"

#include "nrf91_modem.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_ARCH_TRUSTZONE_NONSECURE
#  error NRF91 modem works only in non-secure environment
#endif

#ifndef CONFIG_NRF91_LFCLK_XTAL
#  error NRF91 modem requires using LFXO as the LFCLK source
#endif
>>>>>>>> 9e8753d6256619a7bfa72f4596eb1e481f1c97a2:nuttx/arch/arm/src/nrf91/nrf91_modem.c

#ifndef CONFIG_NRF91_MODEM_LTEM
#  define CONFIG_NRF91_MODEM_LTEM 0
#endif

#ifndef CONFIG_NRF91_MODEM_NBIOT
#  define CONFIG_NRF91_MODEM_NBIOT 0
#endif

#ifndef CONFIG_NRF91_MODEM_GNSS
#  define CONFIG_NRF91_MODEM_GNSS 0
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void stack_record_enter(FAR void *this_fn, FAR void *call_site,
                               FAR void *arg) noinstrument_function
                               nooptimiziation_function;
static void stack_record_leave(FAR void *this_fn, FAR void *call_site,
                               FAR void *arg) noinstrument_function
                               nooptimiziation_function;

/****************************************************************************
 * Public Data
 ****************************************************************************/

struct instrument_s g_stack_record =
{
  .enter = stack_record_enter,
  .leave = stack_record_leave
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: stack_statistics
 ****************************************************************************/

static void stack_record_enter(FAR void *this_fn, FAR void *call_site,
                               FAR void *arg)
{
  FAR struct tcb_s *tcb = running_task();
  FAR void *sp = &tcb;
  size_t i;

<<<<<<<< HEAD:nuttx/sched/instrument/stack_record.c
  if (tcb == NULL || sp < tcb->stack_base_ptr ||
      sp >= tcb->stack_base_ptr + tcb->adj_stack_size)
    {
      return;
    }

  if (tcb->level < CONFIG_SCHED_STACK_RECORD)
    {
      tcb->stackrecord_sp[tcb->level] = sp;
      tcb->stackrecord_pc[tcb->level++] = this_fn;
    }
  else if (tcb->caller_deepest < ++tcb->level)
    {
      tcb->caller_deepest = tcb->level;
    }

  if (tcb->sp_deepest == NULL || sp < tcb->sp_deepest)
    {
      tcb->level_deepest = MIN(tcb->level, CONFIG_SCHED_STACK_RECORD);
      for (i = 0; i < tcb->level_deepest; i++)
        {
          tcb->stackrecord_pc_deepest[i] = tcb->stackrecord_pc[i];
          tcb->stackrecord_sp_deepest[i] = tcb->stackrecord_sp[i];
        }

      tcb->sp_deepest = sp;
    }
}

/****************************************************************************
 * Name: stackrecord_leave
 ****************************************************************************/

static void stack_record_leave(FAR void *this_fn, FAR void *call_site,
                               FAR void *arg)
{
  FAR struct tcb_s *tcb = running_task();
  FAR void *sp = &tcb;

  if (tcb == NULL || tcb->level == 0 || sp < tcb->stack_base_ptr ||
      sp >= tcb->stack_base_ptr + tcb->adj_stack_size)
    {
      return;
    }

  tcb->level--;
========
/****************************************************************************
 * Name: nrf91_modem_config
 ****************************************************************************/

static int nrf91_modem_config(void)
{
  int ret;

  /* Configure modem system mode */

  ret = nrf_modem_at_printf("AT%%XSYSTEMMODE=%d,%d,%d,%d",
                            CONFIG_NRF91_MODEM_LTEM,
                            CONFIG_NRF91_MODEM_NBIOT,
                            CONFIG_NRF91_MODEM_GNSS,
                            CONFIG_NRF91_MODEM_PREFERENCE);
  if (ret < 0)
    {
      goto errout;
    }

errout:
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nrf91_modem_initialize
 ****************************************************************************/

int nrf91_modem_initialize(void)
{
  int ret = OK;

  /* Initialize modem */

  ret = nrf_modem_init(&g_init_params);
  if (ret < 0)
    {
      nerr("nrf_modem_init failed %d\n", ret);
      goto errout;
    }

  /* Initial modem configuration */

  ret = nrf91_modem_config();
  if (ret < 0)
    {
      nerr("nrf91_modem_config failed %d\n", ret);
      goto errout;
    }

  /* Initial modem configuration */

  ret = nrf91_modem_config();
  if (ret < 0)
    {
      nerr("nrf91_modem_config failed %d\n", ret);
      goto errout;
    }

  /* Board-specific modem configuration */

  ret = nrf91_modem_board_init();
  if (ret < 0)
    {
      nerr("nrf91_modem_board_init failed %d\n", ret);
      goto errout;
    }

errout:
  return ret;
>>>>>>>> 9e8753d6256619a7bfa72f4596eb1e481f1c97a2:nuttx/arch/arm/src/nrf91/nrf91_modem.c
}
