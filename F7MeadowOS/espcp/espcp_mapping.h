/****************************************************************************
 * espcp_mapping.h
 *
 *   Values used on the wire between Meadow.OS and the ESP32 coprocessor.
 *
 *   The ESP32 firmware serves both Meadow.OS versions, so the wire carries the
 *   values of the original NuttX 7 based OS: errno numbers as NuttX 7 (and
 *   newlib) defines them, and the NuttX 7 socket option levels.  NuttX 12
 *   renumbered both, so the values below are converted to and from the host
 *   values by espcp_mapping.c.  Never send a host errno or level to the ESP32
 *   without converting it.
 *
 *   Copyright (C) 2026 Wilderness Labs. All rights reserved.
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

#pragma once

#include <stdint.h>

//
//  errno values sent by the ESP32 (NuttX 7 / newlib numbering).
//
#define ESPCP_EPERM           1
#define ESPCP_ENOENT          2
#define ESPCP_ESRCH           3
#define ESPCP_EINTR           4
#define ESPCP_EIO             5
#define ESPCP_ENXIO           6
#define ESPCP_E2BIG           7
#define ESPCP_ENOEXEC         8
#define ESPCP_EBADF           9
#define ESPCP_ECHILD          10
#define ESPCP_EAGAIN          11
#define ESPCP_ENOMEM          12
#define ESPCP_EACCES          13
#define ESPCP_EFAULT          14
#define ESPCP_EBUSY           16
#define ESPCP_EEXIST          17
#define ESPCP_EXDEV           18
#define ESPCP_ENODEV          19
#define ESPCP_ENOTDIR         20
#define ESPCP_EISDIR          21
#define ESPCP_EINVAL          22
#define ESPCP_ENFILE          23
#define ESPCP_EMFILE          24
#define ESPCP_ENOTTY          25
#define ESPCP_ETXTBSY         26
#define ESPCP_EFBIG           27
#define ESPCP_ENOSPC          28
#define ESPCP_ESPIPE          29
#define ESPCP_EROFS           30
#define ESPCP_EMLINK          31
#define ESPCP_EPIPE           32
#define ESPCP_EDOM            33
#define ESPCP_ERANGE          34
#define ESPCP_ENOMSG          35
#define ESPCP_EIDRM           36
#define ESPCP_EDEADLK         45
#define ESPCP_ENOLCK          46
#define ESPCP_ENOSTR          60
#define ESPCP_ENODATA         61
#define ESPCP_ETIME           62
#define ESPCP_ENOSR           63
#define ESPCP_ENONET          64
#define ESPCP_ENOLINK         67
#define ESPCP_EPROTO          71
#define ESPCP_EMULTIHOP       74
#define ESPCP_EBADMSG         77
#define ESPCP_EFTYPE          79
#define ESPCP_ENOTEMPTY       90
#define ESPCP_ENAMETOOLONG    91
#define ESPCP_ELOOP           92
#define ESPCP_EOPNOTSUPP      95
#define ESPCP_EPFNOSUPPORT    96
#define ESPCP_ECONNRESET      104
#define ESPCP_ENOBUFS         105
#define ESPCP_EAFNOSUPPORT    106
#define ESPCP_EPROTOTYPE      107
#define ESPCP_ENOTSOCK        108
#define ESPCP_ENOPROTOOPT     109
#define ESPCP_ESHUTDOWN       110
#define ESPCP_ECONNREFUSED    111
#define ESPCP_EADDRINUSE      112
#define ESPCP_ECONNABORTED    113
#define ESPCP_ENETUNREACH     114
#define ESPCP_ENETDOWN        115
#define ESPCP_ETIMEDOUT       116
#define ESPCP_EHOSTDOWN       117
#define ESPCP_EHOSTUNREACH    118
#define ESPCP_EINPROGRESS     119
#define ESPCP_EALREADY        120
#define ESPCP_EDESTADDRREQ    121
#define ESPCP_EMSGSIZE        122
#define ESPCP_EPROTONOSUPPORT 123
#define ESPCP_EADDRNOTAVAIL   125
#define ESPCP_ENETRESET       126
#define ESPCP_EISCONN         127
#define ESPCP_ENOTCONN        128
#define ESPCP_ETOOMANYREFS    129
#define ESPCP_EDQUOT          132
#define ESPCP_ESTALE          133
#define ESPCP_ENOTSUP         134
#define ESPCP_EILSEQ          138
#define ESPCP_EOVERFLOW       139
#define ESPCP_ECANCELED       140
#define ESPCP_ENOTRECOVERABLE 141
#define ESPCP_EOWNERDEAD      142

//
//  Socket option levels (NuttX 7 numbering).  The ESP32 also accepts
//  IPPROTO_TCP (6) and IPPROTO_UDP (17) as levels, as NuttX 7 did.
//
#define ESPCP_SOL_SOCKET    0
#define ESPCP_SOL_IP        1
#define ESPCP_SOL_TCP       3
#define ESPCP_SOL_UDP       4

/****************************************************************************
 * Name: espcp_mapping_errno_from_esp
 *
 * Description:
 *  Convert an errno value received from the ESP32 to the host errno value.
 *
 * Input Parameters:
 *  value - errno value from the ESP32 (wire numbering), 0 for no error.
 *
 * Returned Value:
 *  Host errno value, 0 if value is 0, or EIO if value is not known.
 *
 ****************************************************************************/
int32_t espcp_mapping_errno_from_esp(int32_t value);

/****************************************************************************
 * Name: espcp_mapping_level_to_esp
 *
 * Description:
 *  Convert a host socket option level to the value sent to the ESP32.
 *
 * Input Parameters:
 *  level - Host socket option level (SOL_SOCKET, IPPROTO_IP, IPPROTO_TCP or
 *          IPPROTO_UDP).
 *  result - Wire value of the level.
 *
 * Returned Value:
 *  0 on success, -ENOPROTOOPT if the level is not supported.
 *
 ****************************************************************************/
int espcp_mapping_level_to_esp(int level, int32_t *result);
