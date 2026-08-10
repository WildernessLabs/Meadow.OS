/****************************************************************************
 * libs/libc/misc/lib_geterrno.c
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

#include <errno.h>

/* get_errno()/set_errno() are normally the macros defined in errno.h. Undo
 * that here so the names below define real, addressable functions instead.
 */

#undef get_errno
#undef set_errno

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: get_errno and set_errno
 *
 * Description:
 *   get_errno() and set_errno() are normally macros (see include/errno.h)
 *   that expand inline to *__errno(). Code that is not compiled against
 *   this tree - such as a dynamically loaded ELF shared object - cannot
 *   see that macro and instead links against get_errno/set_errno by name.
 *   These out-of-line versions give the dynamic loader's symbol table a
 *   real, addressable function to resolve those references against.
 *
 ****************************************************************************/

int get_errno(void)
{
  return *__errno();
}

void set_errno(int errcode)
{
  *__errno() = errcode;
}
