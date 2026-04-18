/****************************************************************************
 * libs/libc/inttypes/lib_imaxdiv.c
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
<<<<<<<< HEAD:nuttx/libs/libc/inttypes/lib_imaxdiv.c
#include <inttypes.h>
========

#include <debug.h>
#include <stdint.h>
>>>>>>>> 5d8cdeaea847bcaf0b9145dd922e0a83045067ca:nuttx/arch/risc-v/src/nuttsbi/sbi_mexception.c

/****************************************************************************
 * Public Functions
 ****************************************************************************/

<<<<<<<< HEAD:nuttx/libs/libc/inttypes/lib_imaxdiv.c
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom)
{
  imaxdiv_t f;

  f.quot = numer / denom;
  f.rem  = numer % denom;
  return f;
========
void sbi_mexception(uintreg_t mcause, uintreg_t *mepc, uintreg_t tval)
{
  sinfo("cauz=%"PRIxREG" epc=%p tval=0x%"PRIxREG"\n", mcause, mepc, tval);

  while (1)
    {
      __asm__ __volatile__("wfi");
    }
>>>>>>>> 5d8cdeaea847bcaf0b9145dd922e0a83045067ca:nuttx/arch/risc-v/src/nuttsbi/sbi_mexception.c
}
