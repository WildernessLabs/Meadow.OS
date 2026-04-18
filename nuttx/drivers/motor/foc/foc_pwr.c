/****************************************************************************
 * drivers/motor/foc/foc_pwr.c
 * Power-stage FOC logic
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

#include <nuttx/motor/foc/foc_pwr.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_ARCH_FAMILY_IMXRT117x

/****************************************************************************
 * Name: imxrt_periphclk_configure
 *
 * Description:
 *   Configure a peripheral clock by modifying the appropriate field in the
 *   appropriate LPCG register.
 *
 * Input Parameters:
 *   index   - The index of the field to be modified
 *   value   - The new value of the field
 *
 * Returned Value:
 *  None
 *
 ****************************************************************************/

void imxrt_periphclk_configure(unsigned int index, unsigned int value)
{
  uint32_t regval;
  regval = getreg32(IMXRT_CCM_LPCG_DIR(index));

  if ((value & CCM_LPCG_DIR_ON) != (regval & CCM_LPCG_DIR_ON))
    {
      if (value == CCM_CG_OFF)
        {
          regval &= ~CCM_LPCG_DIR_ON;
        }
      else
        {
          regval |= CCM_LPCG_DIR_ON;
        }

      putreg32(regval, IMXRT_CCM_LPCG_DIR(index));

      ARM_DSB();
      ARM_ISB();

      /* Ensure the clock setting is written and active before we return */

      while ((getreg32(IMXRT_CCM_LPCG_STAT0(index)) & CCM_LPCG_STAT0_ON)
             != (value & CCM_LPCG_STAT0_ON));
    }
}

#else

/****************************************************************************
 * Name: focpwr_initialize
 *
 * Description:
 *   Initialize the FOC common power-stage data
 *
 * Input Parameters:
 *   pwr   - An instance of the FOC power-stage device
 *   devno - An instance number
 *   dev   - An instance of the FOC device
 *   ops   - power stage ops
 *
 ****************************************************************************/

int focpwr_initialize(FAR struct focpwr_dev_s *pwr,
                      int devno,
                      FAR struct foc_dev_s *dev,
                      FAR struct focpwr_ops_s *ops)
{
  DEBUGASSERT(ops->setup);
  DEBUGASSERT(ops->shutdown);
  DEBUGASSERT(ops->calibration);
  DEBUGASSERT(ops->ioctl);

  pwr->dev   = dev;
  pwr->devno = devno;
  pwr->ops   = ops;

  /* Connet to FOC device */

  dev->pwr = pwr;

  return OK;
}

#endif
