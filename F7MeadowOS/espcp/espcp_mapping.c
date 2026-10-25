/****************************************************************************
 * espcp_mapping.c
 *
 *   Convert values between the host (NuttX 12) and the wire format used with
 *   the ESP32 coprocessor (see espcp_mapping.h).
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

#include <syslog.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <meadow/meadow_debug_helpers.h>

#include "espcp_mapping.h"

struct espcp_mapping_s
{
    int32_t esp_value;
    int32_t host_value;
};
typedef struct espcp_mapping_s espcp_mapping_t;

//
//  ESP32 errno values and the host equivalents.
//
static const espcp_mapping_t espcp_mapping_errno[] =
{
    {ESPCP_EPERM, EPERM},
    {ESPCP_ENOENT, ENOENT},
    {ESPCP_ESRCH, ESRCH},
    {ESPCP_EINTR, EINTR},
    {ESPCP_EIO, EIO},
    {ESPCP_ENXIO, ENXIO},
    {ESPCP_E2BIG, E2BIG},
    {ESPCP_ENOEXEC, ENOEXEC},
    {ESPCP_EBADF, EBADF},
    {ESPCP_ECHILD, ECHILD},
    {ESPCP_EAGAIN, EAGAIN},
    {ESPCP_ENOMEM, ENOMEM},
    {ESPCP_EACCES, EACCES},
    {ESPCP_EFAULT, EFAULT},
    {ESPCP_EBUSY, EBUSY},
    {ESPCP_EEXIST, EEXIST},
    {ESPCP_EXDEV, EXDEV},
    {ESPCP_ENODEV, ENODEV},
    {ESPCP_ENOTDIR, ENOTDIR},
    {ESPCP_EISDIR, EISDIR},
    {ESPCP_EINVAL, EINVAL},
    {ESPCP_ENFILE, ENFILE},
    {ESPCP_EMFILE, EMFILE},
    {ESPCP_ENOTTY, ENOTTY},
    {ESPCP_ETXTBSY, ETXTBSY},
    {ESPCP_EFBIG, EFBIG},
    {ESPCP_ENOSPC, ENOSPC},
    {ESPCP_ESPIPE, ESPIPE},
    {ESPCP_EROFS, EROFS},
    {ESPCP_EMLINK, EMLINK},
    {ESPCP_EPIPE, EPIPE},
    {ESPCP_EDOM, EDOM},
    {ESPCP_ERANGE, ERANGE},
    {ESPCP_ENOMSG, ENOMSG},
    {ESPCP_EIDRM, EIDRM},
    {ESPCP_EDEADLK, EDEADLK},
    {ESPCP_ENOLCK, ENOLCK},
    {ESPCP_ENOSTR, ENOSTR},
    {ESPCP_ENODATA, ENODATA},
    {ESPCP_ETIME, ETIME},
    {ESPCP_ENOSR, ENOSR},
    {ESPCP_ENONET, ENONET},
    {ESPCP_ENOLINK, ENOLINK},
    {ESPCP_EPROTO, EPROTO},
    {ESPCP_EMULTIHOP, EMULTIHOP},
    {ESPCP_EBADMSG, EBADMSG},
    {ESPCP_EFTYPE, EFTYPE},
    {ESPCP_ENOTEMPTY, ENOTEMPTY},
    {ESPCP_ENAMETOOLONG, ENAMETOOLONG},
    {ESPCP_ELOOP, ELOOP},
    {ESPCP_EOPNOTSUPP, EOPNOTSUPP},
    {ESPCP_EPFNOSUPPORT, EPFNOSUPPORT},
    {ESPCP_ECONNRESET, ECONNRESET},
    {ESPCP_ENOBUFS, ENOBUFS},
    {ESPCP_EAFNOSUPPORT, EAFNOSUPPORT},
    {ESPCP_EPROTOTYPE, EPROTOTYPE},
    {ESPCP_ENOTSOCK, ENOTSOCK},
    {ESPCP_ENOPROTOOPT, ENOPROTOOPT},
    {ESPCP_ESHUTDOWN, ESHUTDOWN},
    {ESPCP_ECONNREFUSED, ECONNREFUSED},
    {ESPCP_EADDRINUSE, EADDRINUSE},
    {ESPCP_ECONNABORTED, ECONNABORTED},
    {ESPCP_ENETUNREACH, ENETUNREACH},
    {ESPCP_ENETDOWN, ENETDOWN},
    {ESPCP_ETIMEDOUT, ETIMEDOUT},
    {ESPCP_EHOSTDOWN, EHOSTDOWN},
    {ESPCP_EHOSTUNREACH, EHOSTUNREACH},
    {ESPCP_EINPROGRESS, EINPROGRESS},
    {ESPCP_EALREADY, EALREADY},
    {ESPCP_EDESTADDRREQ, EDESTADDRREQ},
    {ESPCP_EMSGSIZE, EMSGSIZE},
    {ESPCP_EPROTONOSUPPORT, EPROTONOSUPPORT},
    {ESPCP_EADDRNOTAVAIL, EADDRNOTAVAIL},
    {ESPCP_ENETRESET, ENETRESET},
    {ESPCP_EISCONN, EISCONN},
    {ESPCP_ENOTCONN, ENOTCONN},
    {ESPCP_ETOOMANYREFS, ETOOMANYREFS},
    {ESPCP_EDQUOT, EDQUOT},
    {ESPCP_ESTALE, ESTALE},
    {ESPCP_ENOTSUP, ENOTSUP},
    {ESPCP_EILSEQ, EILSEQ},
    {ESPCP_EOVERFLOW, EOVERFLOW},
    {ESPCP_ECANCELED, ECANCELED},
    {ESPCP_ENOTRECOVERABLE, ENOTRECOVERABLE},
    {ESPCP_EOWNERDEAD, EOWNERDEAD},
};

//
//  ESP32 socket option levels and the host equivalents.
//
static const espcp_mapping_t espcp_mapping_level[] =
{
    {ESPCP_SOL_SOCKET, SOL_SOCKET},
    {ESPCP_SOL_IP, IPPROTO_IP},
    {ESPCP_SOL_TCP, IPPROTO_TCP},
    {ESPCP_SOL_UDP, IPPROTO_UDP},
};

#define ESPCP_MAPPING_COUNT(table) (sizeof(table) / sizeof(table[0]))

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
int32_t espcp_mapping_errno_from_esp(int32_t value)
{
    if (value == 0)
    {
        return(0);
    }
    for (size_t index = 0; index < ESPCP_MAPPING_COUNT(espcp_mapping_errno); index++)
    {
        if (espcp_mapping_errno[index].esp_value == value)
        {
            return(espcp_mapping_errno[index].host_value);
        }
    }
    syslog(LOG_ERR, "Unknown ESP32 errno %d, using EIO\n", (int) value);
    return(EIO);
}

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
int espcp_mapping_level_to_esp(int level, int32_t *result)
{
    for (size_t index = 0; index < ESPCP_MAPPING_COUNT(espcp_mapping_level); index++)
    {
        if (espcp_mapping_level[index].host_value == level)
        {
            *result = espcp_mapping_level[index].esp_value;
            return(0);
        }
    }
    syslog(LOG_ERR, "Unsupported socket option level %d\n", level);
    return(-ENOPROTOOPT);
}
