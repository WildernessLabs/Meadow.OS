/****************************************************************************
 * espcp_usrsock_sockif.c
 *
 *   Copyright (C) 2019-21 Wilderness Labs. All rights reserved.
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <inttypes.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <nuttx/net/net.h>
#include <nuttx/net/ioctl.h>
#include <poll.h>
#include <strings.h>
#include <time.h>
#include <sys/time.h>
#include <nuttx/arch.h>
#include <netdb.h>

#include "espcp_usrsock.h"
#include "espcp_common.h"
#include "espcp_coprocessor.h"
#include "generic_list.h"
#include "espcp_event_handlers.h"
#include "espcp_message_dispatcher.h"
#include "../hcom_nx/hcom_nx_config_manager.h"

// #define USE_MEADOW_DEBUG_HELPERS
#include <meadow/meadow_debug_helpers.h>
#include "../misc/meadow_watchdog.h"

/****************************************************************************
 * Definitions.
 ****************************************************************************/

/*
 *  The SPI bus on the ESP32 can only deal with a maximum of 4094 bytes so
 *  define a smaller buffer to account for overhead.
 */
#define MAXIMUM_READ_WRITE_BUFFER_SIZE 4000

/****************************************************************************
 * Local data structures.
 ****************************************************************************/

/**
 *  @brief Hold information about a poll request that is active on the ESP32.
 */
struct espcp_poll_request_list_item_s
{
    struct pollfd *fd;      /* Pointer to the pollfd structure of the original request. */
    struct espcp_usrsock_conn_s *conn;  /* Connection the poll is set up on (readiness cache). */
    int esp_sockfd;         /* ESP socket when the poll was set up; a late interrupt only
                             * updates the cache if the connection still has it. */
    uint32_t request_id;    /* ID of the message sent to the ESP32. */
};
typedef struct espcp_poll_request_list_item_s espcp_poll_request_list_item_t;

/**
 *  @brief Per-socket connection state stored in psock->s_conn.
 *  Holds the file descriptor returned by the ESP32 for this socket.
 */
struct espcp_usrsock_conn_s
{
    /**
     * @brief Common prologue of all connection structures must be the first element of this structure.
     */
    struct socket_conn_s common;

    /**
     * @brief ESP32 socket descriptor.
     */
    int esp_sockfd;

    /**
     * @brief ESPCP_SF_* bits: what the STM32 knows about the ESP32 socket's readiness, so
     *        that poll_setup can answer without a round trip to the ESP32.
     */
    uint8_t esp_state;
};

/*
 *  Readiness cache bits (espcp_usrsock_conn_s.esp_state).
 *
 *  .NET's socket engine on NuttX (WaitForSocketEventsInner in the runtime's
 *  pal_networking.c) takes events only from a 0-timeout poll of every socket,
 *  and a poll sent to the ESP32 cannot complete inside that.  So readiness is
 *  cached here: POLLOUT is answered locally unless writability is unknown
 *  (connect in progress, or the last send returned EAGAIN), and POLLIN, POLLHUP
 *  and POLLERR come from the ESP32's poll interrupts.  Ported from Legacy
 *  65e257319d5.
 */

#define ESPCP_SF_CONNECT_INPROGRESS (1 << 0)  /* connect() returned EINPROGRESS; cleared when SO_ERROR is read or data flows */
#define ESPCP_SF_SEND_EAGAIN        (1 << 1)  /* last send returned EAGAIN; POLLOUT must come from the ESP32 */
#define ESPCP_SF_RD_READY           (1 << 2)  /* ESP32 reported POLLIN; cleared when a receive returns EAGAIN */
#define ESPCP_SF_RD_HUP             (1 << 3)  /* ESP32 reported POLLHUP; kept until close */
#define ESPCP_SF_RD_ERR             (1 << 4)  /* ESP32 reported POLLERR; kept until close */
#define ESPCP_SF_WR_READY           (1 << 5)  /* ESP32 reported POLLOUT while writability was unknown (connect in
                                               * progress or send EAGAIN); cleared when that ends.  Without it a
                                               * connect completion only reaches the blocking poll, and .NET only
                                               * takes events from the 0-timeout one (Legacy f80001fc99c). */

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int espcp_usrsock_sockif_setup(struct socket *psock);

static sockcaps_t espcp_usrsock_sockif_sockcaps(struct socket *psock);

static void espcp_usrsock_sockif_addref(struct socket *psock);

/****************************************************************************
 * Public Data
 ****************************************************************************/

/**
 *  @brief Table of function pointers for the ESP32 networking methods.
 */
const struct sock_intf_s g_usrsock_sockif =
{
    .si_setup = espcp_usrsock_sockif_setup,
    .si_sockcaps = espcp_usrsock_sockif_sockcaps,
    .si_addref = espcp_usrsock_sockif_addref,
    .si_bind = espcp_usrsock_bind,
    .si_getsockname = espcp_usrsock_getsockname,
    .si_getpeername = espcp_usrsock_getpeername,
    .si_listen = espcp_usrsock_listen,
    .si_connect= espcp_usrsock_connect,
    .si_accept = espcp_usrsock_accept,
    .si_poll = espcp_usrsock_poll,
    .si_sendmsg = espcp_usrsock_sendmsg,
    .si_recvmsg = espcp_usrsock_recvmsg,
    .si_close = espcp_usrsock_close,
    .si_ioctl = espcp_usrsock_ioctl,
    .si_socketpair = NULL,                    /* Not implemented */
    .si_shutdown = espcp_usrsock_shutdown,
#ifdef CONFIG_NET_SOCKOPTS
    .si_getsockopt = espcp_usrsock_getsockopt,
    .si_setsockopt = espcp_usrsock_setsockopt,
#endif
#ifdef CONFIG_NET_SENDFILE
    .si_sendfile = NULL,                    /* Not implemented */
#endif
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/*
 *  Linked list holding the list of poll requests that are in progress
 *  on the ESP32.
 */
static gl_linked_list_t *_espcp_poll_requests = NULL;

/*
 *  Mutex used to control access to the poll request linked list.
 */
static sem_t _espcp_poll_requests_mutex;

/**
 * @brief Has the ESP32 been selected?  This variable is used to speed up the
 *        usrsock code by avoiding the need to check the configuration each time
 *        a socket is created.
 */
static bool _esp32_selected = false;

/****************************************************************************
 * Methods
 ****************************************************************************/

/****************************************************************************
 * Name: espcp_conn
 *
 * Description:
 *   Simplify the code accessing the espcp connection structure.
 *
 * Input Parameters:
 *  psock - Pointer to the socket structure.
 *
 * Returned Value:
 *  Pointer to the espcp connection structure for the socket.
 *
 ****************************************************************************/
static inline struct espcp_usrsock_conn_s *espcp_conn(const struct socket * const psock)
{
    return((struct espcp_usrsock_conn_s *) psock->s_conn);
}

/****************************************************************************
 * Name: espcp_sockfd
 *
 * Description:
 *   Simplify the code accessing the ESP32 socket descriptor.
 *
 * Input Parameters:
 *  psock - Pointer to the socket structure.
 *
 * Returned Value:
 *  ESP32 socket descriptor.
 *
 ****************************************************************************/
static inline int espcp_sockfd(const struct socket * const psock)
{
    return((struct espcp_usrsock_conn_s *) psock->s_conn)->esp_sockfd;
}

/****************************************************************************
 * Name: espcp_lock_poll_requests_queue
 *
 * Description:
 *   Lock the poll requests queue.
 *
 * Input Parameters:
 *  None.
 *
 * Returned Value:
 *  None.
 *
 ****************************************************************************/
static inline void espcp_lock_poll_requests_queue(void)
{
    //
    //  Ideally we would use the line below (commented out) but for now we will
    //  use the block of code following this in order to see if we are getting an interrupt
    //  signal when we are waiting for the semaphore.  This may be causing the poll
    //  timeout issue.
    //
    // while ((sem_wait(&_espcp_poll_requests_mutex) != 0) && (get_errno() == EINTR));
    while (sem_wait(&_espcp_poll_requests_mutex) != 0)
    {
        int error = get_errno();
        if (error != EINTR)
        {
            syslog(LOG_INFO, "Error %d waiting for poll requests mutex\n", error);
            break;
        }
        else
        {
            syslog(LOG_INFO, "Interrupted waiting for poll requests mutex, retrying mutex\n");
        }
    }
}

/****************************************************************************
 * Name: espcp_unlock_poll_requests_queue
 *
 * Description:
 *   Unlock the poll requests queue.
 *
 * Input Parameters:
 *  None.
 *
 * Returned Value:
 *  None.
 *
 ****************************************************************************/
static inline void espcp_unlock_poll_requests_queue(void)
{
    sem_post(&_espcp_poll_requests_mutex);
}

/****************************************************************************
 * Name: espcp_unlock_poll_requests_queue
 *
 * Description:
 *   Compare the specified key with the request ID in the item.
 *
 * Input Parameters:
 *  key    - Message ID to look for.
 *  item   - Poll request item being looked for.
 *
 * Returned Value:
 *  true if the key matches the request ID, false otherwise.
 *
 ****************************************************************************/
static bool espcp_usrsock_poll_request_compare_message_id(uint32_t key, void *item)
{
    return(key == ((espcp_poll_request_list_item_t *) item)->request_id);
}

/****************************************************************************
 * Name: espcp_usrsock_poll_request_compare_fd_pointer
 *
 * Description:
 *   Compare the specified key with the request ID in the item.
 *
 * Input Parameters:
 *  key    - Pointer to the fd structure being looked for.
 *  item   - Poll request item being looked for.
 *
 * Returned Value:
 *  true if the key matches the request ID, false otherwise.
 *
 ****************************************************************************/
static bool espcp_usrsock_poll_request_compare_fd_pointer(uint32_t key, void *item)
{
    return((int) key == ((espcp_poll_request_list_item_t *) item)->fd->fd);
}

/****************************************************************************
 * Name: espcp_sock_addr_to_sockaddr
 *
 * Description:
 *  Convert an espcp sock_addr structure into a Nuttx sockaddr structure.
 *
 * Parameters:
 *  destination - Pointer to a block of memory used to hold the sockaddr
 *                data.
 *  sockAddr - Pointer to the encoded espcp_sock_addr_t object holding the
 *             datafrom the ESP32.
 * 
 * Returns:
 *  None.
 *
 ****************************************************************************/
static int espcp_sock_addr_to_sockaddr(void *destination, uint8_t *source)
{
    int result = OK;
    if ((destination == NULL) || (source == NULL))
    {
        result = -EFAULT;
    }
    else
    {
        espcp_sock_addr_t *sai = espcp_extract_sock_addr(source);
        struct sockaddr_in *dest = (struct sockaddr_in *) destination;
        memset(dest, 0, sizeof(struct sockaddr_in));
        dest->sin_family = AF_INET;
        dest->sin_port = sai->port;
        dest->sin_addr.s_addr = sai->ip4_address;
    }
    return(result);
}

/****************************************************************************
 * Name: espcp_usrsock_sockif_init
 *
 * Description:
 *  Perform system wide usrsock initialisation for the ESP32 usrsock layer.
 *
 * Input Parameters:
 *  None.
 *
 * Returned Value:
 *  None.
 *
 ****************************************************************************/
void espcp_usrsock_init()
{
    /*
     *  Initialise the poll request linked list and the semaphore (mutex) used to
     *  restrict access to the list.
     */
    if (_espcp_poll_requests == NULL)
    {
        _espcp_poll_requests = gl_create_empty_linked_list();
        sem_init(&_espcp_poll_requests_mutex, 0, 1);
        sem_setprotocol(&_espcp_poll_requests_mutex, SEM_PRIO_NONE);
    }

    hcom_nx_config_lock();
    meadow_configuration_t *config = hcom_nx_config_get_pointer();
    if ((config != NULL) && (config->default_interface != NULL) &&
        (config->default_interface->interface_type == MEADOW_IFT_ESP32))
    {
        _esp32_selected = true;
    }
    hcom_nx_config_unlock();
}

/****************************************************************************
 * Name: espcp_usrsock_sockif_setup
 *
 * Description:
 *   Called for socket() to verify that the provided socket type and
 *   protocol are usable by this address family.  Perform any family-
 *   specific socket fields.
 *
 * Input Parameters:
 *   psock    - A pointer to a user allocated socket structure to be
 *              initialized.
 *   protocol - (see sys/socket.h)
 *
 * Returned Value:
 *   Zero (OK) is returned on success.  Otherwise, a negated errno value is
 *   returned.
 * 
 *   -ENOSYS: System is not configured for ESP32 networking.
 *
 ****************************************************************************/
static int espcp_usrsock_sockif_setup(struct socket *psock)
{
    if (!_esp32_selected)
    {
        return(-ENOSYS);
    }

    int domain = psock->s_domain;
    int type = psock->s_type;

    psock->s_type = PF_UNSPEC;
    psock->s_conn = NULL;

    /* Let the user socket logic handle the setup...
     *
     * A return value of zero means that the operation was
     * successfully handled by usrsock.  A negative value means that
     * an error occurred.  The special error value -ENETDOWN means
     * that usrsock daemon is not running.  The caller should attempt
     * to open socket with kernel networking stack in this case.
     */

    return (espcp_usrsock_socket(domain, type, psock->s_proto, psock));
}

/****************************************************************************
 * Name: espcp_usrsock_sockif_sockcaps
 *
 * Description:
 *   Return the bit encoded capabilities of this socket.
 *
 * Input Parameters:
 *   psock - Socket structure of the socket whose capabilities are being
 *           queried.
 *
 * Returned Value:
 *   The non-negative set of socket capabilities is returned.
 *
 ****************************************************************************/
static sockcaps_t espcp_usrsock_sockif_sockcaps(struct socket *psock)
{
    return SOCKCAP_NONBLOCKING;
}

/****************************************************************************
 * Name: espcp_usrsock_sockif_addref
 *
 * Description:
 *   Increment the reference count on the underlying connection structure.
 *
 * Input Parameters:
 *   psock - Socket structure of the socket whose reference count will be
 *           incremented.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
static void espcp_usrsock_sockif_addref(struct socket *psock)
{
    // struct usrsock_conn_s *conn;

    espcp_usrsock_not_implemented(__func__);

    // DEBUGASSERT(psock != NULL && psock->s_conn != NULL);

    // conn = psock->s_conn;
    // DEBUGASSERT(conn->crefs > 0 && conn->crefs < 255);
    // conn->crefs++;
}

/****************************************************************************
 * Name:  espcp_usrsock_accept
 *
 * Description:
 *   The usrsock_sockif_accept function is used with connection-based socket
 *   types (SOCK_STREAM, SOCK_SEQPACKET and SOCK_RDM). It extracts the first
 *   connection request on the queue of pending connections, creates a new
 *   connected socket with mostly the same properties as 'sockfd', and
 *   allocates a new socket descriptor for the socket, which is returned. The
 *   newly created socket is no longer in the listening state. The original
 *   socket 'sockfd' is unaffected by this call.  Per file descriptor flags
 *   are not inherited across an inet_accept.
 *
 *   The 'sockfd' argument is a socket descriptor that has been created with
 *   socket(), bound to a local address with bind(), and is listening for
 *   connections after a call to listen().
 *
 *   On return, the 'addr' structure is filled in with the address of the
 *   connecting entity. The 'addrlen' argument initially contains the size
 *   of the structure pointed to by 'addr'; on return it will contain the
 *   actual length of the address returned.
 *
 *   If no pending connections are present on the queue, and the socket is
 *   not marked as non-blocking, inet_accept blocks the caller until a
 *   connection is present. If the socket is marked non-blocking and no
 *   pending connections are present on the queue, inet_accept returns
 *   EAGAIN.
 *
 * Parameters:
 *   psock    Reference to the listening socket structure
 *   addr     Receives the address of the connecting client
 *   addrlen  Input: allocated size of 'addr', Return: returned size of 'addr'
 *   newsock  Location to return the accepted socket information.
 *
 * Returned Value:
 *   Returns 0 (OK) on success.  On failure, it returns a negated errno
 *   value.  See accept() for a description of the appropriate error value.
 *
 * Assumptions:
 *   The network is locked.
 *
 ****************************************************************************/
int espcp_usrsock_accept(struct socket *psock, struct sockaddr *addr, socklen_t *addrlen, struct socket *newsock, int flags)
{
    MEADOW_TRACE_INFORMATION("accept(%d, %p, %p, %p)\n", espcp_sockfd(psock), addr, addrlen, newsock);

    if (espcp_get_configuration()->esp_not_responding)
    {
        MEADOW_TRACE_DEBUG("accept - result ENETDOWN\n");
        return(-ENETDOWN);
    }

    int32_t result = -1;
    espcp_message_t *message = NULL;

    espcp_accept_request_t *request = (espcp_accept_request_t *) zalloc(sizeof(espcp_accept_request_t));
    if (request == NULL)
    {
        MEADOW_TRACE_DEBUG("accept - result ENOMEM\n");
        return(-ENOMEM);
    }
    request->socket_handle = espcp_sockfd(psock);

    int payload_length = espcp_accept_request_buffer_size(request);
    uint8_t *payload = (uint8_t *) zalloc(payload_length);
    if (payload == NULL)
    {
        free(request);
        result = -ENOMEM;
    }
    else
    {
        espcp_encode_accept_request(request, payload);
        free(request);

        message = espcp_create_message_on_heap(espcp_message_types_header, espcp_esp32_interfaces_wi_fi,
                                               espcp_wi_fi_function_accept, espcp_status_codes_completed_ok,
                                               espcp_get_next_message_id(), payload, payload_length);
        if (message == NULL)
        {
            free(payload);
            result = -ENOMEM;
        }
        else
        {
            if (espcp_queue_message(message, true) == espcp_status_codes_completed_ok)
            {
                switch (message->status_code)
                {
                    case espcp_status_codes_completed_ok:
                        {
                            espcp_accept_response_t *response = espcp_extract_accept_response(message->payload);
                            if (response == NULL)
                            {
                                result = -ENOMEM;
                            }
                            else
                            {
                                result = response->result;
                                if (result > 0)
                                {
                                    struct espcp_usrsock_conn_s *newconn =
                                        zalloc(sizeof(struct espcp_usrsock_conn_s));
                                    if (newconn == NULL)
                                    {
                                        result = -ENOMEM;
                                    }
                                    else
                                    {
                                        newconn->esp_sockfd = result;
                                        newsock->s_conn = newconn;
                                    }
                                    newsock->s_domain = psock->s_domain;
                                    newsock->s_type = psock->s_type;
                                    newsock->s_sockif = psock->s_sockif;
                                    espcp_sock_addr_t *sockAddr = espcp_extract_sock_addr(response->addr);
                                    if (sockAddr == NULL)
                                    {
                                        result = -ENOMEM;
                                    }
                                    else
                                    {
                                        if ((addr != NULL) && (addrlen != NULL))
                                        {
                                            struct sockaddr_in sai = {};
                                            sai.sin_family = sockAddr->family;
                                            memcpy(&sai.sin_addr, &sockAddr->ip4_address, sizeof(sai.sin_addr));
                                            sai.sin_port = sockAddr->port;
                                            int copyAmount = (sizeof(struct sockaddr_in) <= *addrlen) ? sizeof(struct sockaddr_in) : *addrlen;
                                            memcpy(addr, &sai, copyAmount);
                                            *addrlen = sizeof(sai);
                                        }
                                        free(sockAddr);
                                    }
                                }
                                else
                                {
                                    result = -response->response_errno;
                                }
                                free(response);
                            }
                        }
                        break;
                    case espcp_status_codes_thread_pool_is_full:
                    case espcp_status_codes_esp_out_of_memory:
                        result = -ENOMEM;
                        break;
                    default:
                        result = -1;
                        break;
                }
            }
        }
    }

    espcp_delete_message_and_payload(message);

    MEADOW_TRACE_INFORMATION("accept - socket %d, result %" PRId32 "\n", espcp_sockfd(psock), result);

    return (result);
}

/****************************************************************************
 * Name: espcp_usrsock_bind
 *
 * Description:
 *   usrsock_bind() gives the socket 'conn' the local address 'addr'. 'addr'
 *   is 'addrlen' bytes long. Traditionally, this is called "assigning a name
 *   to a socket." When a socket is created with socket, it exists in a name
 *   space (address family) but has no name assigned.
 *
 * Input Parameters:
 *   conn     usrsock socket connection structure
 *   addr     Socket local address
 *   addrlen  Length of 'addr'
 *
 * Returned Value:
 *   0 on success, a negated errno is returned on error.
 *
 *   EACCES
 *     The address is protected, and the user is not the superuser.
 *   EADDRINUSE
 *     The given address is already in use.
 *   EINVAL
 *     The socket is already bound to an address.
 *   ENOTSOCK
 *     psock is a descriptor for a file, not a socket.
 *   ENETDOWN
 *     Network not started.
 *
 * Assumptions:
 *
 ****************************************************************************/
int espcp_usrsock_bind(struct socket *psock, const struct sockaddr *addr, socklen_t addrlen)
{
    MEADOW_TRACE_INFORMATION("bind(%d, %p, %d)\n", espcp_sockfd(psock), addr, addrlen);

    if (espcp_get_configuration()->esp_not_responding)
    {
        MEADOW_TRACE_DEBUG("bind - result ENETDOWN\n");
        return(-ENETDOWN);
    }

    int32_t result = -1;
    espcp_message_t *message = NULL;
    struct sockaddr_in *sin = (struct sockaddr_in *) addr;

    espcp_sock_addr_t *sockAddr = (espcp_sock_addr_t *) zalloc(sizeof(espcp_sock_addr_t));
    if (sockAddr == NULL)
    {
        MEADOW_TRACE_DEBUG("bind - result ENOMEM\n");
        return(-ENOMEM);
    }
    sockAddr->family = sin->sin_family;
    sockAddr->port = sin->sin_port;
    memcpy(&sockAddr->ip4_address, &sin->sin_addr, sizeof(sin->sin_addr));
    int encodedSockAddrSize = espcp_sock_addr_buffer_size(sockAddr);
    uint8_t *encodedSockAddr = (uint8_t *) zalloc(encodedSockAddrSize);
    if (encodedSockAddr == NULL)
    {
        free(sockAddr);
        MEADOW_TRACE_DEBUG("bind - result ENOMEM\n");
        return(-ENOMEM);
    }
    espcp_encode_sock_addr(sockAddr, encodedSockAddr);
    free(sockAddr);

    espcp_bind_request_t *request = (espcp_bind_request_t *) zalloc(sizeof(espcp_bind_request_t));
    if (request == NULL)
    {
        free(encodedSockAddr);
        MEADOW_TRACE_DEBUG("bind - result ENOMEM\n");
        return(-ENOMEM);
    }
    request->socket_handle = espcp_sockfd(psock);
    request->addr = encodedSockAddr;
    request->addr_length = encodedSockAddrSize;

    int payload_length = espcp_bind_request_buffer_size(request);
    uint8_t *payload = (uint8_t *) zalloc(payload_length);
    if (payload == NULL)
    {
        free(encodedSockAddr);
        free(request);
        MEADOW_TRACE_DEBUG("bind - result ENOMEM\n");
        return(-ENOMEM);
    }
    else
    {
        espcp_encode_bind_request(request, payload);
        free(encodedSockAddr);
        free(request);

        message = espcp_create_message_on_heap(espcp_message_types_header, espcp_esp32_interfaces_wi_fi,
                                               espcp_wi_fi_function_bind, espcp_status_codes_completed_ok,
                                               espcp_get_next_message_id(), payload, payload_length);
        if (message == NULL)
        {
            result = -ENOMEM;
        }
        else
        {
            if (espcp_queue_message(message, true) == espcp_status_codes_completed_ok)
            {
                switch (message->status_code)
                {
                    case espcp_status_codes_completed_ok:
                        {
                            espcp_integer_and_errno_response_t *response = espcp_extract_integer_and_errno_response(message->payload);
                            if (response == NULL)
                            {
                                result = -ENOMEM;
                            }
                            else
                            {
                                result = -response->response_errno;
                                free(response);
                            }
                        }
                        break;
                    case espcp_status_codes_thread_pool_is_full:
                    case espcp_status_codes_esp_out_of_memory:
                        result = -ENOMEM;
                        break;
                    default:
                        result = -1;
                        break;
                }
            }
        }
    }

    espcp_delete_message_and_payload(message);

    MEADOW_TRACE_INFORMATION("bind - socket %d, result %" PRId32 "\n", espcp_sockfd(psock), result);

    return (result);
}

/****************************************************************************
 * Name: espcp_usrsock_close
 *
 * Description:
 *  Closes a file descriptor, so that it no longer refers to any socket and 
 *  may be reused.
 *
 * Input Parameters:
 *   conn     usrsock socket connection structure
 * 
 * Returns:
 *  0 if successful, negated errno on error.
 * 
 *  EBADF
 *      fd isn't a valid open file descriptor.
 *  EIO
 *      An I/O error occurred.
 *  ENETDOWN
 *      Network down / not connected.
 * 
 ****************************************************************************/
int espcp_usrsock_close(struct socket *psock)
{
    MEADOW_TRACE_INFORMATION("close(%d)\n", espcp_sockfd(psock));

    meadow_watchdog_t watchdog_close = meadow_watchdog_activate(WATCHDOG_CLOSE_TIMEOUT_MILLISECONDS, CLOSE_WATCHDOG);

    if (espcp_get_configuration()->esp_not_responding)
    {
        meadow_watchdog_deactivate(watchdog_close);
        MEADOW_TRACE_DEBUG("close - result ENETDOWN\n");
        return(-ENETDOWN);
    }

    int32_t result = -1;
    espcp_message_t *message = NULL;

    espcp_close_request_t *request = (espcp_close_request_t *) zalloc(sizeof(espcp_close_request_t));
    if (request == NULL)
    {
        meadow_watchdog_deactivate(watchdog_close);
        MEADOW_TRACE_DEBUG("close - result ENOMEM\n");
        return(-ENOMEM);
    }
    request->socket_handle = espcp_sockfd(psock);

    int payload_length = espcp_close_request_buffer_size(request);
    uint8_t *payload = (uint8_t *) zalloc(payload_length);
    if (payload == NULL)
    {
        free(request);
        result = -ENOMEM;
    }
    else
    {
        espcp_encode_close_request(request, payload);
        free(request);

        message = espcp_create_message_on_heap(espcp_message_types_header, espcp_esp32_interfaces_wi_fi,
                                               espcp_wi_fi_function_close, espcp_status_codes_completed_ok,
                                               espcp_get_next_message_id(), payload, payload_length);
        if (message == NULL)
        {
            result = -ENOMEM;
        }
        else
        {
            if (espcp_queue_message(message, true) == espcp_status_codes_completed_ok)
            {
                switch (message->status_code)
                {
                    case espcp_status_codes_completed_ok:
                        {
                            espcp_integer_and_errno_response_t *response = espcp_extract_integer_and_errno_response(message->payload);
                            if (response == NULL)
                            {
                                result = -ENOMEM;
                            }
                            else
                            {
                                result = response->result;
                                if (result < 0)
                                {
                                    result = -response->response_errno;
                                }
                                free(response);
                            }
                        }
                        break;
                    case espcp_status_codes_thread_pool_is_full:
                    case espcp_status_codes_esp_out_of_memory:
                        result = -ENOMEM;
                        break;
                    default:
                        result = -1;
                        break;
                }
            }
        }
    }

    espcp_delete_message_and_payload(message);
    meadow_watchdog_deactivate(watchdog_close);

    MEADOW_TRACE_INFORMATION("close - socket %d, result %" PRId32 "\n", espcp_sockfd(psock), result);

    /* Free the per-socket connection state. */
    free(psock->s_conn);
    psock->s_conn = NULL;

    return (result);
}

/****************************************************************************
 * Name: espcp_usrsock_connect
 *
 * Description:
 *   Perform a usrsock connection
 *
 * Input Parameters:
 *   psock - A reference to the socket structure of the socket to be connected
 *   addr    The address of the remote server to connect to
 *   addrlen Length of address buffer
 *
 * Returned Value:
 *   0 on success, negated errno on error.
 *
 ****************************************************************************/
int espcp_usrsock_connect(struct socket *psock, const struct sockaddr *addr, socklen_t addrlen)
{
    MEADOW_TRACE_INFORMATION("connect(%d, %p, %d)\n", espcp_sockfd(psock), addr, addrlen);

    if (espcp_get_configuration()->esp_not_responding)
    {
        MEADOW_TRACE_DEBUG("connect - result ENETDOWN\n");
        return(-ENETDOWN);
    }

    int32_t result = -1;
    espcp_message_t *message = NULL;
    struct sockaddr_in *sin = (struct sockaddr_in *) addr;

    espcp_sock_addr_t *sockAddr = (espcp_sock_addr_t *) zalloc(sizeof(espcp_sock_addr_t));
    if (sockAddr == NULL)
    {
        MEADOW_TRACE_DEBUG("connect - result -1 (sockAddr is NULL)\n");
        return(-1);
    }
    sockAddr->family = sin->sin_family;
    sockAddr->port = sin->sin_port;
    memcpy(&sockAddr->ip4_address, &sin->sin_addr, sizeof(sin->sin_addr));
    int encodedSockAddrSize = espcp_sock_addr_buffer_size(sockAddr);
    uint8_t *encodedSockAddr = (uint8_t *) zalloc(encodedSockAddrSize);
    if (encodedSockAddr == NULL)
    {
        free(sockAddr);
        MEADOW_TRACE_DEBUG("connect - result ENOMEM\n");
        return(-ENOMEM);
    }
    espcp_encode_sock_addr(sockAddr, encodedSockAddr);
    free(sockAddr);

    espcp_connect_request_t *request = (espcp_connect_request_t *) zalloc(sizeof(espcp_connect_request_t));
    if (request == NULL)
    {
        free(encodedSockAddr);
        MEADOW_TRACE_DEBUG("connect - result ENOMEM\n");
        return(-ENOMEM);
    }
    request->socket_handle = espcp_sockfd(psock);
    request->addr = encodedSockAddr;
    request->addr_length = encodedSockAddrSize;

    int payload_length = espcp_connect_request_buffer_size(request);
    uint8_t *payload = (uint8_t *) zalloc(payload_length);
    if (payload == NULL)
    {
        free(encodedSockAddr);
        free(request);
        MEADOW_TRACE_DEBUG("connect - result ENOMEM\n");
        return(-ENOMEM);
    }
    else
    {
        espcp_encode_connect_request(request, payload);
        free(encodedSockAddr);
        free(request);

        message = espcp_create_message_on_heap(espcp_message_types_header, espcp_esp32_interfaces_wi_fi,
                                               espcp_wi_fi_function_connect, espcp_status_codes_completed_ok,
                                               espcp_get_next_message_id(), payload, payload_length);
        if (message == NULL)
        {
            result = -ENOMEM;
        }
        else
        {
            if (espcp_queue_message(message, true) == espcp_status_codes_completed_ok)
            {
                switch (message->status_code)
                {
                    case espcp_status_codes_completed_ok:
                        {
                            espcp_integer_and_errno_response_t *response = espcp_extract_integer_and_errno_response(message->payload);
                            if (response == NULL)
                            {
                                result = -ENOMEM;
                            }
                            else
                            {
                                result = -response->response_errno;
                                free(response);
                            }
                        }
                        break;
                    case espcp_status_codes_thread_pool_is_full:
                    case espcp_status_codes_esp_out_of_memory:
                        result = -ENOMEM;
                        break;
                    default:
                        result = -EIO;
                        break;
                }
            }
        }
    }

    espcp_delete_message_and_payload(message);

    //
    //  While a non-blocking connect is in progress, POLLOUT means "connected" and only the
    //  ESP32 knows when that is, so poll_setup stops answering POLLOUT locally.
    //
    if (result == -EINPROGRESS)
    {
        espcp_conn(psock)->esp_state |= ESPCP_SF_CONNECT_INPROGRESS;
        espcp_conn(psock)->esp_state &= ~ESPCP_SF_WR_READY;
    }
    if (result == 0)
    {
        espcp_conn(psock)->esp_state &= ~ESPCP_SF_CONNECT_INPROGRESS;
    }

    MEADOW_TRACE_INFORMATION("connect - socket %d, result %" PRId32 "\n", espcp_sockfd(psock), result);

    return (result);
}

/****************************************************************************
 * Name: espcp_usrsock_getsockpeername
 *
 * Description:
 *   The getsockname() function retrieves the locally-bound name of the
 *   specified socket, stores this address in the sockaddr structure pointed
 *   to by the 'addr' argument, and stores the length of this address in the
 *   object pointed to by the 'addrlen' argument.
 *
 *   If the actual length of the address is greater than the length of the
 *   supplied sockaddr structure, the stored address will be truncated.
 *
 *   If the socket has not been bound to a local name, the value stored in
 *   the object pointed to by address is unspecified.
 *
 * Input Parameters:
 *   conn     usrsock socket connection structure
 *   addr     sockaddr structure to receive data [out]
 *   addrlen  Length of sockaddr structure [in/out]
 *   function Function to call, getsockname or getpeername.
 *
 * Returns:
 *  0 on success, negated errno on error.
 *
 ****************************************************************************/
static int espcp_usrsock_getsockpeername(struct socket *psock, struct sockaddr *addr, socklen_t *addrlen, enum espcp_wi_fi_function function)
{
    MEADOW_TRACE_INFORMATION("getsockpeername - socket %d\n", espcp_sockfd(psock));

    if (espcp_get_configuration()->esp_not_responding)
    {
        MEADOW_TRACE_DEBUG("getsockpeername - result ENETDOWN\n");
        return(-ENETDOWN);
    }

    int32_t result = -1;
    espcp_message_t *message = NULL;

    espcp_get_sock_peer_name_request_t *request = (espcp_get_sock_peer_name_request_t *) zalloc(sizeof(espcp_get_sock_peer_name_request_t));
    if (request == NULL)
    {
        MEADOW_TRACE_DEBUG("getsockpeername - result ENOMEM\n");
        return(-ENOMEM);
    }
    request->socket_handle = espcp_sockfd(psock);

    int payload_length = espcp_get_sock_peer_name_request_buffer_size(request);
    uint8_t *payload = (uint8_t *) zalloc(payload_length);
    if (payload == NULL)
    {
        free(request);
        MEADOW_TRACE_DEBUG("getsockpeername - result ENOMEM\n");
        return(-ENOMEM);
    }
    else
    {
        espcp_encode_get_sock_peer_name_request(request, payload);
        free(request);

        message = espcp_create_message_on_heap(espcp_message_types_header, espcp_esp32_interfaces_wi_fi,
                                               function, espcp_status_codes_completed_ok,
                                               espcp_get_next_message_id(), payload, payload_length);
        if (message == NULL)
        {
            result = -ENOMEM;
        }
        else
        {
            if (espcp_queue_message(message, true) == espcp_status_codes_completed_ok)
            {
                switch (message->status_code)
                {
                    case espcp_status_codes_completed_ok:
                        {
                            espcp_get_sock_peer_name_response_t *response = espcp_extract_get_sock_peer_name_response(message->payload);
                            if (response == NULL)
                            {
                                result = -ENOMEM;
                            }
                            else
                            {
                                result = response->result;
                                if (result == 0)
                                {
                                    espcp_sock_addr_t *sockAddr = espcp_extract_sock_addr(response->addr);
                                    if (sockAddr == NULL)
                                    {
                                        result = -ENOMEM;
                                    }
                                    else
                                    {
                                        if ((addr != NULL) && (addrlen != NULL))
                                        {
                                            struct sockaddr_in sai = {};
                                            sai.sin_family = sockAddr->family;
                                            memcpy(&sai.sin_addr, &sockAddr->ip4_address, sizeof(sai.sin_addr));
                                            sai.sin_port = sockAddr->port;
                                            int copyAmount = (sizeof(struct sockaddr_in) <= *addrlen) ? sizeof(struct sockaddr_in) : *addrlen;
                                            memcpy(addr, &sai, copyAmount);
                                            *addrlen = sizeof(sai);
                                        }
                                        free(sockAddr);
                                    }
                                }
                                else
                                {
                                    result = -response->response_errno;
                                }
                                espcp_free_get_sock_peer_name_response(response);
                            }
                        }
                        break;
                    case espcp_status_codes_thread_pool_is_full:
                    case espcp_status_codes_esp_out_of_memory:
                        result = -ENOMEM;
                        break;
                    default:
                        result = -1;
                        break;
                }
            }
        }
    }

    espcp_delete_message_and_payload(message);

    MEADOW_TRACE_INFORMATION("getsockpeername - socket %d, result %" PRId32 "\n", espcp_sockfd(psock), result);

    return (result);
}

/****************************************************************************
 * Name: espcp_usrsock_getpeername
 *
 * Description:
 *   The getpeername() function retrieves the remote-connected name of the
 *   specified socket, stores this address in the sockaddr structure pointed
 *   to by the 'addr' argument, and stores the length of this address in the
 *   object pointed to by the 'addrlen' argument.
 *
 *   If the actual length of the address is greater than the length of the
 *   supplied sockaddr structure, the stored address will be truncated.
 *
 *   If the socket has not been bound to a local name, the value stored in
 *   the object pointed to by address is unspecified.
 *
 * Parameters:
 *   conn     usrsock socket connection structure
 *   addr     sockaddr structure to receive data [out]
 *   addrlen  Length of sockaddr structure [in/out]
 *
 * Returns:
 *  0 on success, negated errno on error.
 *
 ****************************************************************************/
int espcp_usrsock_getpeername(struct socket *psock, struct sockaddr *addr, socklen_t *addrlen)
{
    MEADOW_TRACE_INFORMATION("getpeername(%d, %p, %p)\n", espcp_sockfd(psock), addr, addrlen);

    if (espcp_get_configuration()->esp_not_responding)
    {
        MEADOW_TRACE_DEBUG("getpeername - result ENETDOWN\n");
        return(-ENETDOWN);
    }

    int result = espcp_usrsock_getsockpeername(psock, addr, addrlen, espcp_wi_fi_function_get_peer_name);

    MEADOW_TRACE_INFORMATION("getpeername - socket %d result %d\n", espcp_sockfd(psock), result);

    return(result);
}

/****************************************************************************
 * Name: espcp_usrsock_getsockname
 *
 * Description:
 *   The getsockname() function retrieves the locally-bound name of the
 *   specified socket, stores this address in the sockaddr structure pointed
 *   to by the 'addr' argument, and stores the length of this address in the
 *   object pointed to by the 'addrlen' argument.
 *
 *   If the actual length of the address is greater than the length of the
 *   supplied sockaddr structure, the stored address will be truncated.
 *
 *   If the socket has not been bound to a local name, the value stored in
 *   the object pointed to by address is unspecified.
 *
 * Input Parameters:
 *   conn     usrsock socket connection structure
 *   addr     sockaddr structure to receive data [out]
 *   addrlen  Length of sockaddr structure [in/out]
 *
 * Returns:
 *  0 on success, negated errno on error.
 *
 ****************************************************************************/
int espcp_usrsock_getsockname(struct socket *psock, struct sockaddr *addr, socklen_t *addrlen)
{
    MEADOW_TRACE_INFORMATION("getsockname(%d, %p, %p)\n", espcp_sockfd(psock), addr, addrlen);

    if (espcp_get_configuration()->esp_not_responding)
    {
        MEADOW_TRACE_DEBUG("getsockname - result ENETDOWN\n");
        return(-ENETDOWN);
    }

    int result = espcp_usrsock_getsockpeername(psock, addr, addrlen, espcp_wi_fi_function_get_sock_name);

    MEADOW_TRACE_INFORMATION("getsockname - socket %d result %d\n", espcp_sockfd(psock), result);

    return(result);
}

/****************************************************************************
 * Name: espcp_usrsock_send_ioctl_to_esp
 *
 * Description:
 *  The simple cases have been taken care of so we now head over to the ESP32
 *  and let it perform the ioctl request.
 *
 * Parameters:
 *   psock      A pointer to a NuttX-specific, internal socket structure
 *   cmd        The ioctl command
 *   arg        The argument of the ioctl cmd
 *   arglen     Number of bytes 
 * 
 * Returns:
 *  0 on success, negated errno on error.
 *
 ****************************************************************************/
static int espcp_usrsock_send_ioctl_to_esp(struct socket *psock, int cmd, void *arg, size_t arglen)
{
    int result = 0;
    espcp_ioctl_request_t *request = (espcp_ioctl_request_t *) zalloc(sizeof(espcp_ioctl_request_t));
    if (request == NULL)
    {
        MEADOW_TRACE_DEBUG("espcp_usrsock_send_ioctl_to_esp - result ENOMEM\n");
        return(-ENOMEM);
    }
    request->command = cmd;
    struct ifconf *ifc = (struct ifconf *) arg;
    struct lifreq *lifr = (struct lifreq *) arg;

    //
    //  SIOCSESPNONBLOCK is the only ioctl that applies to one socket, so its payload also
    //  carries the ESP socket and the requested state: [command][ESP socket][nonblock].  It
    //  is built here so that the generated, single field espcp_ioctl_request_t and its
    //  encoder stay as they are; every other ioctl sends just the command.
    //
    int payload_length;
    uint8_t *payload;
    if (cmd == SIOCSESPNONBLOCK)
    {
        payload_length = 3 * sizeof(int32_t);
        payload = (uint8_t *) zalloc(payload_length);
        if (payload != NULL)
        {
            int nonblock = ((arg != NULL) && (arglen >= sizeof(int))) ? *((const int *) arg) : 0;
            espcp_encode_int32(cmd, payload);
            espcp_encode_int32(espcp_sockfd(psock), payload + sizeof(int32_t));
            espcp_encode_int32(nonblock, payload + (2 * sizeof(int32_t)));
        }
    }
    else
    {
        payload_length = espcp_ioctl_request_buffer_size(request);
        payload = (uint8_t *) zalloc(payload_length);
        if (payload != NULL)
        {
            espcp_encode_ioctl_request(request, payload);
        }
    }
    if (payload == NULL)
    {
        free(request);
        MEADOW_TRACE_DEBUG("espcp_usrsock_send_ioctl_to_esp - result ENOMEM\n");
        return(-ENETDOWN);
    }
    else
    {
        free(request);

        espcp_message_t *message = espcp_create_message_on_heap(espcp_message_types_header, espcp_esp32_interfaces_wi_fi,
                                            espcp_wi_fi_function_ioctl, espcp_status_codes_completed_ok,
                                            espcp_get_next_message_id(), payload, payload_length);
        if (message == NULL)
        {
            free(payload);
            MEADOW_TRACE_DEBUG("espcp_usrsock_send_ioctl_to_esp - result ENOMEM\n");
            return(-ENOMEM);
        }
        if (espcp_queue_message(message, true) == espcp_status_codes_completed_ok)
        {
            switch (message->status_code)
            {
                case espcp_status_codes_completed_ok:
                    {
                        espcp_ioctl_response_t *response = espcp_extract_ioctl_response(message->payload);
                        if (response != NULL)
                        {
                            if (response->result != -1)
                            {
                                struct sockaddr sa;
                                memset(&sa, 0, sizeof(struct sockaddr));
                                sa.sa_family = AF_INET;
                                switch (cmd)
                                {
                                    case SIOCGIFCONF:
                                        if (arglen < (sizeof(struct ifconf)))
                                        {
                                            ifc->ifc_len = 0;
                                        }
                                        else
                                        {
                                            ifc->ifc_len = sizeof(struct ifreq);
                                            struct ifreq *ifr = ifc->ifc_req;
                                            result = espcp_sock_addr_to_sockaddr((void *) &ifr->ifr_ifru.ifru_addr, response->addr);
                                        }
                                        break;
                                    case SIOCGIFADDR:       /* Get IP address */
                                    case SIOCGIFNETMASK:    /* Get network mask */
                                        //
                                        //  This relies upon the fact that the ifru_addr and ifru_netmask are in a union.
                                        //
                                        result = espcp_sock_addr_to_sockaddr((void *) &lifr->lifr_ifru.lifru_addr, response->addr);
                                        break;
                                    case SIOCGIFHWADDR:     /* Get hardware address */
                                        memset(&lifr->lifr_ifru.lifru_hwaddr, 0, sizeof(lifr->lifr_ifru.lifru_hwaddr));
                                        lifr->lifr_ifru.lifru_hwaddr.sa_family = AF_INET;
                                        memcpy((void *) &lifr->lifr_ifru.lifru_hwaddr.sa_data, (void *) response->addr, MEADOW_MAC_ADDRESS_SIZE);
                                        // memcpy((void *) &lifr->lifr_ifru.lifru_hwaddr, (void *) , sizeof(sa));
                                        break;
                                    case SIOCGIFFLAGS:
                                        lifr->lifr_flags = response->flags;
                                        lifr->lifr_flags |= IFF_WIFI;
                                        //
                                        //  The ESP32 reports IFF_UP when the station has an IP
                                        //  address.  There is no separate carrier state, so up
                                        //  also means running (POSIX callers want both).
                                        //
                                        if ((lifr->lifr_flags & IFF_UP) != 0)
                                        {
                                            lifr->lifr_flags |= IFF_RUNNING;
                                        }
                                        break;
                                    case SIOCSESPNONBLOCK:  /* The ESP socket's O_NONBLOCK has been set */
                                        break;
                                    default:
                                        MEADOW_TRACE_CRITICAL("%s@%d Unknown ioctl command %08x.\n", __FILE__, __LINE__, cmd);
                                        result = -EINVAL;
                                        break;
                                }
                            }
                            else
                            {
                                result = -response->response_errno;
                            }
                            free(response);
                        }
                        else
                        {
                            result = -EINVAL;
                        }
                    }
                    break;
                case espcp_status_codes_thread_pool_is_full:
                case espcp_status_codes_esp_out_of_memory:
                    result = -ENOMEM;
                    break;
                default:
                    result = -1;
                    break;
            }
        }
        espcp_delete_message_and_payload(message);
    }
    return(result);
}

/****************************************************************************
 * Name: espcp_usrsock_ioctl
 *
 * Description:
 *  The usrsock_ioctl() function performs network device specific operations.
 *
 * Parameters:
 *   psock      A pointer to a NuttX-specific, internal socket structure
 *   cmd        The ioctl command
 *   arg        The argument of the ioctl cmd
 *   arglen     Number of bytes 
 * 
 * Returns:
 *  0 on success, negated errno on error.
 *
 ****************************************************************************/
int espcp_usrsock_ioctl(struct socket *psock, int cmd, unsigned long arg)
{
    MEADOW_TRACE_INFORMATION("ioctl(%d, %d, 0x%08lx)\n", espcp_sockfd(psock), cmd, arg);

    int result = 0;

    if (espcp_get_configuration()->esp_not_responding)
    {
        MEADOW_TRACE_DEBUG("ioctl - result ENETDOWN\n");
        return(-ENETDOWN);
    }

    void *argp = (void *)(uintptr_t)arg;

    if (argp != NULL)
    {
        // struct ifconf *ifc = (struct ifconf *) argp;
        struct lifreq *lifr = (struct lifreq *) argp;
        switch (cmd)
        {
            // case SIOCGIFCONF:
            //     if (ifc->ifc_req == NULL)
            //     {
            //         ifc->ifc_len = sizeof(struct lifreq);
            //     }
            //     break;
            case SIOCGIFNAME:
                if (lifr->lifr_ifindex > 1)
                {
                    result = -ENOTTY;
                }
                else
                {
                    strcpy(lifr->lifr_name, "wlan0");
                }
                break;
            case SIOCGIFBRDADDR:    /* Get broadcast IP address */
            case SIOCGIFDSTADDR:    /* Get P-to-P address */
                //
                //  The sa structure has been filled with zeroes so the address will be 0.0.0.0.
                //
                memset((void *) &lifr->lifr_ifru.lifru_dstaddr, 0, sizeof(struct sockaddr));
                break;
            case FIONBIO:
                //
                //  NuttX passes fcntl(F_SETFL) O_NONBLOCK changes to the socket interface as
                //  FIONBIO.  The socket that matters is the lwip socket on the ESP32, so push
                //  the state there.  On success netdev_ioctl() goes on to set the local
                //  _SF_NONBLOCK flag.  If the ESP32 rejects it (firmware without
                //  SIOCSESPNONBLOCK), return -ENOTTY so that the local flag is still set
                //  and fcntl() succeeds, with the ESP32 socket left blocking as before.
                //
                result = espcp_usrsock_send_ioctl_to_esp(psock, SIOCSESPNONBLOCK, argp, sizeof(int));
                if (result < 0)
                {
                    MEADOW_TRACE_INFORMATION("ioctl - ESP32 did not accept SIOCSESPNONBLOCK (%d), socket stays blocking on the ESP32\n", result);
                    result = -ENOTTY;
                }
                break;
            default:
                result = espcp_usrsock_send_ioctl_to_esp(psock, cmd, argp, sizeof(unsigned long));
                break;
        }
    }
    else
    {
        result = -EINVAL;
    }

    MEADOW_TRACE_INFORMATION("ioctl - socket %d result %d\n", espcp_sockfd(psock), result);

    return (result);
}

/****************************************************************************
 * Name: espcp_usrsock_listen
 *
 * Description:
 *   To accept connections, a socket is first created with psock_socket(), a
 *   willingness to accept incoming connections and a queue limit for
 *   incoming connections are specified with psock_listen(), and then the
 *   connections are accepted with psock_accept().  For the case of AFINET
 *   and AFINET6 sockets, psock_listen() calls this function.  The
 *   psock_listen() call applies only to sockets of type SOCK_STREAM or
 *   SOCK_SEQPACKET.
 *
 * Parameters:
 *   psock    Reference to an internal, bound socket structure.
 *   backlog  The maximum length the queue of pending connections may grow.
 *            If a connection request arrives with the queue full, the client
 *            may receive an error with an indication of ECONNREFUSED or,
 *            if the underlying protocol supports retransmission, the request
 *            may be ignored so that retries succeed.
 *
 * Returned Value:
 *  0 on success, negated errno on error.
 *
 ****************************************************************************/
int espcp_usrsock_listen(struct socket *psock, int backlog)
{
    MEADOW_TRACE_INFORMATION("listen(%d, %d)\n", espcp_sockfd(psock), backlog);

    if (espcp_get_configuration()->esp_not_responding)
    {
        MEADOW_TRACE_DEBUG("listen - result ENETDOWN\n");
        return(-ENETDOWN);
    }

    int32_t result = -1;
    espcp_message_t *message = NULL;

    espcp_listen_request_t *request = (espcp_listen_request_t *) zalloc(sizeof(espcp_listen_request_t));
    if (request == NULL)
    {
        MEADOW_TRACE_DEBUG("listen - result ENOMEM\n");
        return(-ENOMEM);
    }
    request->socket_handle = espcp_sockfd(psock);
    request->back_log = backlog;

    int payload_length = espcp_listen_request_buffer_size(request);
    uint8_t *payload = (uint8_t *) zalloc(payload_length);
    if (payload == NULL)
    {
        free(request);
        result = -ENOMEM;
    }
    else
    {
        espcp_encode_listen_request(request, payload);
        free(request);

        message = espcp_create_message_on_heap(espcp_message_types_header, espcp_esp32_interfaces_wi_fi,
                                               espcp_wi_fi_function_listen, espcp_status_codes_completed_ok,
                                               espcp_get_next_message_id(), payload, payload_length);
        if (message == NULL)
        {
            result = -ENOMEM;
        }
        else
        {
            if (espcp_queue_message(message, true) == espcp_status_codes_completed_ok)
            {
                switch (message->status_code)
                {
                    case espcp_status_codes_completed_ok:
                        {
                            espcp_integer_and_errno_response_t *response = espcp_extract_integer_and_errno_response(message->payload);
                            if (response == NULL)
                            {
                                result = -ENOMEM;
                            }
                            else
                            {
                                result = -response->response_errno;
                                free(response);
                            }
                        }
                        break;
                    case espcp_status_codes_thread_pool_is_full:
                    case espcp_status_codes_esp_out_of_memory:
                        result = -ENOMEM;
                        break;
                    default:
                        result = -1;
                        break;
                }
            }
        }
    }

    espcp_delete_message_and_payload(message);

    MEADOW_TRACE_INFORMATION("listen - socket %d result %" PRId32 "\n", espcp_sockfd(psock), result);

    return (result);
}

/****************************************************************************
 * Name: espcp_usrsock_poll_setup
 *
 * Description:
 *   Setup a poll request passing the request information to the ESP32.
 *   to this function.
 *
 * Input Parameters:
 *   psock - Pointer to the structure holding information about the socket.
 *   fds   - The structure describing the events to be monitored.
 *
 * Returned Value:
 *  0 on success, negated errno on error.
 *
 ****************************************************************************/
static int espcp_usrsock_poll_setup(struct socket *psock, struct pollfd *fds)
{
    if (espcp_get_configuration()->esp_not_responding)
    {
        return(-ENETDOWN);
    }

    //
    //  Answer locally, without a round trip to the ESP32, when a requested event is already
    //  known to be pending (see ESPCP_SF_*).  .NET samples every socket with a 0-timeout poll,
    //  which a poll sent to the ESP32 can never answer in time.
    //
    struct espcp_usrsock_conn_s *conn = espcp_conn(psock);
    pollevent_t ready = 0;
    if (((fds->events & POLLOUT) != 0) &&
        (((conn->esp_state & (ESPCP_SF_CONNECT_INPROGRESS | ESPCP_SF_SEND_EAGAIN)) == 0) ||
         ((conn->esp_state & ESPCP_SF_WR_READY) != 0)))
    {
        ready |= POLLOUT;
    }
    if (((fds->events & POLLIN) != 0) && ((conn->esp_state & ESPCP_SF_RD_READY) != 0))
    {
        ready |= POLLIN;
    }
    if ((conn->esp_state & ESPCP_SF_RD_HUP) != 0)
    {
        ready |= POLLHUP;
    }
    if ((conn->esp_state & ESPCP_SF_RD_ERR) != 0)
    {
        ready |= POLLERR;
    }
    if (ready != 0)
    {
        poll_notify(&fds, 1, ready);
        return(0);
    }

    int result = 0;

    espcp_poll_request_t *request = (espcp_poll_request_t *) zalloc(sizeof(espcp_poll_request_t));
    if (request == NULL)
    {
        return (-ENOMEM);
    }
    request->socket_handle = espcp_sockfd(psock);
    request->events = fds->events;
    request->timeout = -1;
    request->setup = 1;
    request->setup_message_id = espcp_get_next_message_id();

    // The following comment looks to have been added by Alexis. I added
    // MEADOW.OS REBASE to the comment so it could be looked at by someone who
    // knew what the comment means and not forgotten.
    //
    //MEADOW.OS REBASE: Install poll callback!

    int payload_length = espcp_poll_request_buffer_size(request);
    uint8_t *payload = (uint8_t *) zalloc(payload_length);
    if (payload == NULL)
    {
        free(request);
        return (-ENOMEM);
    }
    espcp_encode_poll_request(request, payload);

    espcp_message_t *message = espcp_create_message_on_heap(espcp_message_types_header, espcp_esp32_interfaces_wi_fi,
                                                            espcp_wi_fi_function_poll,
                                                            espcp_status_codes_completed_ok,
                                                            request->setup_message_id, payload, payload_length);       
    free(request);
    if (message == NULL)
    {
        free(payload);
        return (-ENOMEM);
    }

    espcp_poll_request_list_item_t *pr = (espcp_poll_request_list_item_t *) zalloc(sizeof(espcp_poll_request_list_item_t));
    if (pr == NULL)
    {
        espcp_delete_message_and_payload(message);
        return (-ENOMEM);
    }
    else
    {
        //
        //  Note that "request" has already been freed at this point, so the
        //  message ID has to come from the message rather than from the request.
        //
        MEADOW_TRACE_INFORMATION("poll setup - Setting up poll request ID %08" PRIx32 ", socket %d\n", message->message_id, espcp_sockfd(psock));
        pr->fd = fds;
        pr->conn = conn;
        pr->esp_sockfd = conn->esp_sockfd;
        pr->request_id = message->message_id;
        espcp_lock_poll_requests_queue();
        bool added = gl_add_item_to_head(_espcp_poll_requests, pr);
        espcp_unlock_poll_requests_queue();

        if (!added)
        {
            //
            //  There is nowhere to record the request, so the response from the ESP32 could never be routed back to this descriptor.
            //
            free(pr);
            espcp_delete_message_and_payload(message);
            return (-ENOMEM);
        }

        if (espcp_queue_message(message, true) == espcp_status_codes_completed_ok)
        {
            espcp_integer_and_errno_response_t *response = espcp_extract_integer_and_errno_response(message->payload);
            if (response == NULL)
            {
                result = -ENOMEM;
            }
            else
            {
                if (response->result < 0)
                {
                    result = -response->response_errno;
                }
                free(response);
            }
        }
        else
        {
            if (message->status_code == espcp_status_codes_esp_out_of_memory)
            {
                result = -ENOMEM;
            }
            else
            {
                result = -EFAULT;
            }
        }

        if (result != 0)
        {
            //
            //  The poll on the ESP32 failed so remove the request from the list.
            //
            espcp_lock_poll_requests_queue();
            void *removed = gl_remove_item(_espcp_poll_requests, message->message_id, espcp_usrsock_poll_request_compare_message_id);
            espcp_unlock_poll_requests_queue();
            free(removed);
        }
    }

    espcp_delete_message_and_payload(message);
    return(result);
}

/****************************************************************************
 * Name: espcp_usrsock_poll_teardown
 *
 * Description:
 *  Teardown a a poll request setup with a previous call to
 *  espcp_usrsock_poll_setup
 *
 * Input Parameters:
 *   psock - Pointer to the structure holding information about the socket.
 *   fds   - The structure describing the events to be monitored.
 *
 * Returned Value:
 *  0 on success, negated errno on error.
 * 
 ****************************************************************************/
static int espcp_usrsock_poll_teardown(struct socket *psock, struct pollfd *fds)
{
    MEADOW_TRACE_INFORMATION("poll teardown\n");

    int result = 0;

    //
    //  Remove the item from the list rather than just looking it up.  This method is going to dispose of the poll request
    //  whatever happens, and removing it here gives this method exclusive ownership of the item for the rest of the call.
    //
    //  gl_remove_item() frees the list node but not the data so pr has to be freed here.
    //
    espcp_lock_poll_requests_queue();
    espcp_poll_request_list_item_t *pr = (espcp_poll_request_list_item_t *) gl_remove_item(_espcp_poll_requests,
                                                (uint32_t) fds->fd, espcp_usrsock_poll_request_compare_fd_pointer);
    espcp_unlock_poll_requests_queue();

    if (pr == NULL)
    {
        //
        //  The request has already been removed from the queue by the interrupt handler so we treat this as a success.
        //
        MEADOW_TRACE_INFORMATION("Poll teardown - Cannot find poll request for socket %d\n", espcp_sockfd(psock));
    }
    else
    {
        //
        //  The request ID is the only thing needed from the list item, so take a copy and release the item now.
        //  Every exit path below is then free of any clean up obligation.
        //
        uint32_t request_id = pr->request_id;
        free(pr);
        if (espcp_get_configuration()->esp_not_responding)
        {
            //
            //  Checked only after the item has been removed: the item points at the caller's
            //  pollfd, so it must not outlive this call (Legacy 65e257319d5).
            //
            return(-ENETDOWN);
        }

        espcp_poll_request_t *request = (espcp_poll_request_t *) zalloc(sizeof(espcp_poll_request_t));
        if (request == NULL)
        {
            return (-ENOMEM);
        }
        request->socket_handle = espcp_sockfd(psock);
        request->setup = 0;
        request->setup_message_id = request_id;

        int payload_length = espcp_poll_request_buffer_size(request);
        uint8_t *payload = (uint8_t *) zalloc(payload_length);
        if (payload == NULL)
        {
            free(request);
            return (-ENOMEM);
        }
        espcp_encode_poll_request(request, payload);
        free(request);

        espcp_message_t *message = espcp_create_message_on_heap(espcp_message_types_header, espcp_esp32_interfaces_wi_fi,
                                                                espcp_wi_fi_function_poll,
                                                                espcp_status_codes_completed_ok,
                                                                espcp_get_next_message_id(), payload, payload_length);       
        if (message == NULL)
        {
            free(payload);
            return (-ENOMEM);
        }

        if (espcp_queue_message(message, true) == espcp_status_codes_completed_ok)
        {
            espcp_integer_and_errno_response_t *response = espcp_extract_integer_and_errno_response(message->payload);
            if (response == NULL)
            {
                result = -ENOMEM;
            }
            else
            {
                if (response->result < 0)
                {
                    result = -response->response_errno;
                }
                free(response);
            }
        }
        espcp_delete_message_and_payload(message);
    }

    MEADOW_TRACE_INFORMATION("poll teardown - exit\n");

    return(result);
}

/****************************************************************************
 * Name: espcp_usrsock_poll_interrupt_handler
 *
 * Description:
 *   This interrupt handler will be called when the poll request on the ESP32
 *   has a return value.
 *
 * Input Parameters:
 *   message - Message from the ESP32 with the result of the poll request.
 *
 ****************************************************************************/
void espcp_usrsock_poll_interrupt_handler(espcp_message_t *message)
{
    MEADOW_TRACE_INFORMATION("poll interrupt handler - enter\n");

    uint32_t request_id = 0;

    if (message->payload == NULL)
    {
        syslog(LOG_ERR, "%s@%d Poll interrupt event has no payload.\n", __FILE__, __LINE__);
        espcp_delete_message_and_payload(message);
        return;
    }

    espcp_interrupt_poll_response_t *ipr = espcp_extract_interrupt_poll_response(message->payload);
    if (ipr != NULL)
    {
        request_id = ipr->setup_message_id;

        //
        //  Remove the request from the list so taking ownership.  This preents teardown or a second interrupt from trying use this item.
        //
        espcp_lock_poll_requests_queue();
        espcp_poll_request_list_item_t *pr = (espcp_poll_request_list_item_t *) gl_remove_item(_espcp_poll_requests,
                                                    request_id, espcp_usrsock_poll_request_compare_message_id);
        espcp_unlock_poll_requests_queue();
        if (pr != NULL)
        {
            MEADOW_TRACE_INFORMATION("poll interrupt handler - found originating request %08" PRIx32 "\n", request_id);
            //
            //  Feed the readiness cache, unless the connection no longer has the ESP socket the
            //  poll was set up on (a stale HUP or ERR would fail every poll on a new socket).
            //
            if (pr->conn->esp_sockfd == pr->esp_sockfd)
            {
                if ((ipr->returned_events & POLLIN) != 0)
                {
                    pr->conn->esp_state |= ESPCP_SF_RD_READY;
                }
                if ((ipr->returned_events & POLLHUP) != 0)
                {
                    pr->conn->esp_state |= ESPCP_SF_RD_HUP;
                }
                if ((ipr->returned_events & POLLERR) != 0)
                {
                    pr->conn->esp_state |= ESPCP_SF_RD_ERR;
                }
                if ((ipr->returned_events & POLLOUT) != 0)
                {
                    pr->conn->esp_state |= ESPCP_SF_WR_READY;
                }
            }
            //
            //  poll_notify() sets revents and wakes the poll() waiting on this descriptor;
            //  setting revents alone left the waiter asleep until its timeout.
            //
            poll_notify(&pr->fd, 1, ipr->returned_events);
            MEADOW_TRACE_INFORMATION("poll interrupt handler - fd=%d events=%" PRIu32 " revents=%" PRIu32 "\n", pr->fd->fd, pr->fd->events, pr->fd->revents);
        }
        else
        {
            MEADOW_TRACE_INFORMATION("poll interrupt handler - Cannot find request %08" PRIx32 "\n", request_id);
        }
        free(pr);
        free(ipr);
    }

    espcp_delete_message_and_payload(message);
    MEADOW_TRACE_INFORMATION("poll interrupt handler - exit, request ID: %08" PRIx32 "\n", request_id);
}

/****************************************************************************
 * Name: espcp_usrsock_poll
 *
 * Description:
 *   The standard poll() operation redirects operations on socket descriptors
 *   to this function.
 *
 * Input Parameters:
 *   psock - An instance of the internal socket structure.
 *   fds   - The structure describing the events to be monitored.
 *   setup - true: Setup up the poll; false: Teardown the poll
 *
 * Returned Value:
 *  0 on success, negated errno on error.
 *
 ****************************************************************************/
int espcp_usrsock_poll(struct socket *psock, struct pollfd *fds, bool setup)
{
    MEADOW_TRACE_INFORMATION("poll(%d, %p, %d)\n", espcp_sockfd(psock), fds, setup ? 1 : 0);

    meadow_watchdog_t watchdog_poll = meadow_watchdog_activate(WATCHDOG_POLL_TIMEOUT_MILLISECONDS, POLL_WATCHDOG);

    if (espcp_get_configuration()->esp_not_responding)
    {
        meadow_watchdog_deactivate(watchdog_poll);
        MEADOW_TRACE_DEBUG("poll - result ENETDOWN\n");
        return(-ENETDOWN);
    }

    int result = 0;
    if (setup)
    {
        result = espcp_usrsock_poll_setup(psock, fds);
    }
    else
    {
        result = espcp_usrsock_poll_teardown(psock, fds);
        MEADOW_TRACE_INFORMATION("poll - teardown returned %d\n", result);
    }

    meadow_watchdog_deactivate(watchdog_poll);
    MEADOW_TRACE_INFORMATION("poll - socket %d, result %d\n", espcp_sockfd(psock), result);

    return (result);
}

/****************************************************************************
 * Name: espcp_log_socket_option_name
 *
 * Description:
 *  Send the name of the socket option to the logging stream (when logging
 *  is enabled).
 *
 * Input Parameters:
 *  option - option ID to be decoded and sent to the log stream.
 *
 * Returns:
 *  None.
 *
 ****************************************************************************/
static void espcp_log_socket_option_name(int level, int option)
{
    if (level == SOL_SOCKET)
    {
        switch (option)
        {
            case SO_ACCEPTCONN:
                MEADOW_TRACE_INFORMATION("Socket option: SO_ACCEPTCONN\n");
                break;
            case SO_BROADCAST:
                MEADOW_TRACE_INFORMATION("Socket option: SO_BROADCAST\n");
                break;
            case SO_DEBUG:
                MEADOW_TRACE_INFORMATION("Socket option: SO_DEBUG\n");
                break;
            case SO_DONTROUTE:
                MEADOW_TRACE_INFORMATION("Socket option: SO_DONTROUTE\n");
                break;
            case SO_ERROR:
                MEADOW_TRACE_INFORMATION("Socket option: SO_ERROR\n");
                break;
            case SO_KEEPALIVE:
                MEADOW_TRACE_INFORMATION("Socket option: SO_KEEPALIVE\n");
                break;
            case SO_LINGER:
                MEADOW_TRACE_INFORMATION("Socket option: SO_LINGER\n");
                break;
            case SO_OOBINLINE:
                MEADOW_TRACE_INFORMATION("Socket option: SO_OOBINLINE\n");
                break;
            case SO_RCVBUF:
                MEADOW_TRACE_INFORMATION("Socket option: SO_RCVBUF\n");
                break;
            case SO_RCVLOWAT:
                MEADOW_TRACE_INFORMATION("Socket option: SO_RCVLOWAT\n");
                break;
            case SO_RCVTIMEO:
                MEADOW_TRACE_INFORMATION("Socket option: SO_RCVTIMEO\n");
                break;
            case SO_REUSEADDR:
                MEADOW_TRACE_INFORMATION("Socket option: SO_REUSEADDR\n");
                break;
            case SO_SNDBUF:
                MEADOW_TRACE_INFORMATION("Socket option: SO_SNDBUF\n");
                break;
            case SO_SNDLOWAT:
                MEADOW_TRACE_INFORMATION("Socket option: SO_SNDLOWAT\n");
                break;
            case SO_SNDTIMEO:
                MEADOW_TRACE_INFORMATION("Socket option: SO_SNDTIMEO\n");
                break;
            case SO_TYPE:
                MEADOW_TRACE_INFORMATION("Socket option: SO_TYPE\n");
                break;
            default:
                MEADOW_TRACE_INFORMATION("Unknown socket option name: 0x%x (%d)\n", option, option);
                break;
        }
    }
    else
    {
        if (level == SOL_TCP)
        {
            switch (option)
            {
                case TCP_NODELAY:
                    MEADOW_TRACE_INFORMATION("TCP option: TCP_NODELAY\n");
                    break;
                default:
                    MEADOW_TRACE_INFORMATION("Unknown TCP option name: 0x%x (%d)\n", option, option);
                    break;
            }
        }
    }
}

/****************************************************************************
 * Name: espcp_usrsock_getsockopt
 *
 * Description:
 *   getsockopt() retrieve thse value for the option specified by the
 *   'option' argument for the socket specified by the 'psock' argument. If
 *   the size of the option value is greater than 'value_len', the value
 *   stored in the object pointed to by the 'value' argument will be silently
 *   truncated. Otherwise, the length pointed to by the 'value_len' argument
 *   will be modified to indicate the actual length of the 'value'.
 *
 *   The 'level' argument specifies the protocol level of the option. To
 *   retrieve options at the socket level, specify the level argument as
 *   SOL_SOCKET.
 *
 *   See <sys/socket.h> a complete list of values for the 'option' argument.
 * 
 *   getsockopt is documented here: https://linux.die.net/man/3/getsockopt
 *
 * Input Parameters:
 *   conn      usrsock socket connection structure
 *   level     Protocol level to set the option
 *   option    identifies the option to get
 *   value     Points to the argument value
 *   value_len The length of the argument value
 *
 * Returns:
 *  0 on success, negated errno on error.
 * 
 *  -EINVAL: Value length is not large enough to store the result.
 *
 ****************************************************************************/
int espcp_usrsock_getsockopt(struct socket *psock, int level, int option,
                             void *value, socklen_t *value_len)
{
    MEADOW_TRACE_INFORMATION("getsockopt(%d, %d, %d, %p, %p)\n", espcp_sockfd(psock), level, option, value, value_len);

    if (espcp_get_configuration()->esp_not_responding)
    {
        MEADOW_TRACE_DEBUG("getsockopt - result ENETDOWN\n");
        return(-ENETDOWN);
    }
    espcp_log_socket_option_name(level, option);

    if (value == NULL)
    {
        return(-EFAULT);
    }
    switch (option)
    {
        case SO_LINGER:
        case SO_SNDTIMEO:
        case SO_RCVTIMEO:
        case SO_RCVBUF:
            //
            //  Decode and store value.
            //
            break;
        case SO_DEBUG:
        case SO_DONTROUTE:
        case SO_OOBINLINE:
        case SO_SNDBUF:
        case SO_SNDLOWAT:
        case SO_RCVLOWAT:
            //
            //  Not supported by the ESP32.
            //
            return(-EPFNOSUPPORT);
            break;
        case SO_ERROR:
            //
            //  Sent to the ESP32, which returns and clears the pending socket error (an
            //  int, decoded below like SO_RCVBUF).  .NET completes a non-blocking
            //  connect by reading SO_ERROR once the socket polls writable (0 = connected).
            //
            break;
        case SO_ACCEPTCONN:
        case SO_TYPE:
            //
            //  Supported by the ESP but not implemented yet.
            //
            return(-EPFNOSUPPORT);
            break;
        default:
            //
            //  If we get here then we have an option that has not been considered.
            //
            return(-EPFNOSUPPORT);
            break;
    }

    int result = -1;
    espcp_get_sock_opt_request_t *request = (espcp_get_sock_opt_request_t *) zalloc(sizeof(espcp_get_sock_opt_request_t));
    if (request == NULL)
    {
        MEADOW_TRACE_DEBUG("getsockopt - result ENOMEM\n");
        return(-ENOMEM);
    }
    request->socket_handle = espcp_sockfd(psock);
    request->option_name = option;
    request->level = level;
    int payload_length = espcp_get_sock_opt_request_buffer_size(request);
    uint8_t *payload = (uint8_t *) zalloc(payload_length);
    espcp_message_t *message = NULL;
    if (payload == NULL)
    {
        free(request);
        result = -ENOMEM;
    }
    else
    {
        espcp_encode_get_sock_opt_request(request, payload);
        free(request);
        message = espcp_create_message_on_heap(espcp_message_types_header, espcp_esp32_interfaces_wi_fi,
                                               espcp_wi_fi_function_get_sock_opt, espcp_status_codes_completed_ok,
                                               espcp_get_next_message_id(), payload, payload_length);
        if (message == NULL)
        {
            free(payload);
            result = -ENOMEM;
        }
        else
        {
            if (espcp_queue_message(message, true) == espcp_status_codes_completed_ok)
            {
                switch (message->status_code)
                {
                    case espcp_status_codes_completed_ok:
                        {
                            espcp_get_sock_opt_response_t *response = espcp_extract_get_sock_opt_response(message->payload);
                            if (response == NULL)
                            {
                                result = -ENOMEM;
                            }
                            else
                            {
                                result = (response->result < 0) ? -response->response_errno : response->result;
                                if (result == 0)
                                {
                                    void *source = NULL;
                                    int source_size = 0;
                                    switch (option)
                                    {
                                        case SO_LINGER:
                                            {
                                                espcp_linger_t *esp_lv = espcp_extract_linger(response->option_value);
                                                if (esp_lv == NULL)
                                                {
                                                    result = -ENOMEM;
                                                }
                                                else
                                                {
                                                    source_size = sizeof(struct linger);
                                                    source = zalloc(source_size);
                                                    if (source == NULL)
                                                    {
                                                        result = -ENOMEM;
                                                    }
                                                    else
                                                    {
                                                        struct linger *lv = (struct linger *) source;
                                                        lv->l_linger = esp_lv->l_linger;
                                                        lv->l_onoff = esp_lv->l_on_off;
                                                    }
                                                    free(esp_lv);
                                                }
                                            }
                                            break;
                                        case SO_SNDTIMEO:
                                        case SO_RCVTIMEO:
                                            {
                                                espcp_time_val_t *esp_tv = espcp_extract_time_val(response->option_value);
                                                if (esp_tv == NULL)
                                                {
                                                    result = -ENOMEM;
                                                }
                                                else
                                                {
                                                    source_size = sizeof(struct timeval);
                                                    source = zalloc(source_size);
                                                    if (source == NULL)
                                                    {
                                                        result = -ENOMEM;
                                                    }
                                                    else
                                                    {
                                                        struct timeval *tv = (struct timeval *) source;
                                                        tv->tv_sec = esp_tv->tv_sec;
                                                        tv->tv_usec = esp_tv->tv_usec;
                                                    }
                                                    free(esp_tv);
                                                }
                                            }
                                            break;
                                        case SO_ERROR:
                                        case SO_RCVBUF:
                                            {
                                                espcp_integer_response_t *esp_iv = espcp_extract_integer_response(response->option_value);
                                                if (esp_iv == NULL)
                                                {
                                                    result = -ENOMEM;
                                                }
                                                else
                                                {
                                                    source_size = sizeof(int);
                                                    source = zalloc(source_size);
                                                    if (source == NULL)
                                                    {
                                                        result = -ENOMEM;
                                                    }
                                                    else
                                                    {
                                                        *((int *) source) = esp_iv->result;
                                                    }
                                                    free(esp_iv);
                                                }
                                            }
                                            break;
                                    }
                                    if ((result == 0) && (source != NULL))
                                    {
                                        if (*value_len < (socklen_t) source_size)
                                        {
                                            source_size = *value_len;
                                        }
                                        memcpy(value, source, source_size);
                                        *value_len = source_size;
                                    }
                                    else
                                    {
                                        if (result == 0)
                                        {
                                            result = -ENOPROTOOPT;
                                        }
                                    }
                                    free(source);
                                }
                                espcp_free_get_sock_opt_response(response);
                            }
                        }
                        break;
                    case espcp_status_codes_thread_pool_is_full:
                    case espcp_status_codes_esp_out_of_memory:
                        result = -ENOMEM;
                        break;
                    default:
                        result = -1;
                        break;
                }
            }                
        }
    }

    espcp_delete_message_and_payload(message);

    //
    //  .NET completes a non-blocking connect by polling POLLOUT and then reading SO_ERROR.
    //  Whatever the result, the connect attempt is over, so POLLOUT is answered locally again.
    //
    if (option == SO_ERROR)
    {
        espcp_conn(psock)->esp_state &= ~(ESPCP_SF_CONNECT_INPROGRESS | ESPCP_SF_WR_READY);
    }

    MEADOW_TRACE_INFORMATION("getsockopt - socket %d result %d\n", espcp_sockfd(psock), result);

    return(result);
}

/**
 * @brief Encode an integer value for the ESP32.
 * 
 * @param data 
 *      Pointer to the integer value to be encoded.
 * 
 * @return uint8_t* 
 *      Buffer containing the encoded value or NULL if there was a problem.
 */
static uint8_t *espcp_usrsock_encode_integer(const void *data)
{
    uint8_t *result = NULL;

    if (data != NULL)
    {
        result = (uint8_t *) zalloc(sizeof(int));
        if (result != NULL)
        {
            espcp_encode_int32(*((int32_t *) data), result);
        }
    }

    return(result);
}

/**
 * @brief Encode a TCP option value for the ESP32.
 * 
 * @param request
 *      Pointer to a setsockopt request to be sent to the ESP32.
 * 
 * @param option 
 *      Name of the option value to be encoded.
 * 
 * @param value
 *      Pointer to the option value data.
 * 
 * @param length 
 *      Length of the option value data.
 *  
 * @return int
 *      0 on success or negated error code if there was a problem.
 */
static int espcp_usrsock_encode_socket_option_value(espcp_set_sock_opt_request_t *request, int option, const void *value, socklen_t length)
{
    int result = 0;

    if (value == NULL)
    {
        result = -EINVAL;
    }
    else
    {
        switch (option)
        {
            case SO_SNDTIMEO:
            case SO_RCVTIMEO:
                {
                    espcp_time_val_t *tv = (espcp_time_val_t *) zalloc(sizeof(espcp_time_val_t));
                    if (tv == NULL)
                    {
                        result = -ENOMEM;
                    }
                    else
                    {
                        struct timeval *ov = (struct timeval *) value;
                        tv->tv_sec = ov->tv_sec;
                        tv->tv_usec = ov->tv_usec;
                        request->option_value_length = espcp_time_val_buffer_size(tv);
                        request->option_value = (uint8_t *) zalloc(request->option_value_length);
                        if (request->option_value != NULL)
                        {
                            espcp_encode_time_val(tv, request->option_value);
                        }
                        else
                        {
                            result = -ENOMEM;
                        }
                        free(tv);
                    }
                }
                break;
            case SO_LINGER:
                //
                //  Add implementation when enabled in the ESP32 build.
                //
                break;
            case SO_DEBUG:
            case SO_DONTROUTE:
            case SO_OOBINLINE:
            case SO_SNDBUF:
            case SO_SNDLOWAT:
            case SO_RCVLOWAT:
                //
                //  Not supported by the ESP32.
                //
                break;
            case SO_RCVBUF:
            case SO_REUSEADDR:
                request->option_value = espcp_usrsock_encode_integer(value);
                if (request->option_value == NULL)
                {
                    result = -ENOMEM;
                }
                else
                {
                    request->option_value_length = sizeof(int);
                }
                break;
            case SO_ACCEPTCONN:
            case SO_ERROR:
            case SO_TYPE:
                //
                //  Supported by the ESP but not implemented yet.
                //
                break;
            default:
                break;
        }
    }

    return(result);
}

/**
 * @brief Encode a TCP option value for the ESP32.
 * 
 * @param request
 *      Pointer to a setsockopt request to be sent to the ESP32.
 * 
 * @param option 
 *      Name of the option value to be encoded.
 * 
 * @param value
 *      Pointer to the option value data.
 * 
 * @param length 
 *      Length of the option value data.
 *  
 * @return int
 *      0 on success or negated error code if there was a problem.
 */
static int espcp_usrsock_encode_tcp_option_value(espcp_set_sock_opt_request_t *request, int option, const void *value, socklen_t length)
{
    int result = 0;

    if (value == NULL)
    {
        result = -EINVAL;
    }
    else
    {
        switch(option)
        {
            case TCP_NODELAY:
                request->option_value = espcp_usrsock_encode_integer(value);
                if (request->option_value == NULL)
                {
                    result = -ENOMEM;
                }
                else
                {
                    request->option_value_length = sizeof(int);
                }
                break;
            default:
                result = -EINVAL;
                break;
        }
    }

    return(result);
}

/****************************************************************************
 * Name: espcp_usrsock_setsockopt
 *
 * Description:
 *   psock_setsockopt() sets the option specified by the 'option' argument,
 *   at the protocol level specified by the 'level' argument, to the value
 *   pointed to by the 'value' argument for the socket on the 'psock'
 *   argument.
 *
 *   The 'level' argument specifies the protocol level of the option. To set
 *   options at the socket level, specify the level argument as SOL_SOCKET.
 *
 *   See <sys/socket.h> a complete list of values for the 'option' argument.
 * 
 *   According to the ESP32 documentation, the following socket option types
 *   are NOT supported:
 *      - SoDebug
 *      - SoDontRoute
 *      - SoUseLoopBack
 *      - SoOobInline
 *      - SoReusePort
 *      - SoSndBuf
 *      - SoSndLoWat
 *      - SoRcvLoWat
 * 
 * Input Parameters:
 *   psock     usrsock socket connection structure
 *   level     Protocol level to set the option
 *   option    identifies the option to set
 *   value     Points to the argument value
 *   value_len The length of the argument value
 * 
 * Returns:
 *  0 on success, negated errno on error.
 *
 ****************************************************************************/
int espcp_usrsock_setsockopt(struct socket *psock, int level, int option,
                             const void *value, socklen_t value_len)
{
    MEADOW_TRACE_INFORMATION("setsockopt(%d, %d, %d, %p, %u)\n", espcp_sockfd(psock), level, option, value, value_len);

    if (espcp_get_configuration()->esp_not_responding)
    {
        MEADOW_TRACE_DEBUG("setsockopt - result ENETDOWN\n");
        return(-ENETDOWN);
    }

    espcp_set_sock_opt_request_t *request = (espcp_set_sock_opt_request_t *) zalloc(sizeof(espcp_set_sock_opt_request_t));
    if (request == NULL)
    {
        MEADOW_TRACE_DEBUG("setsockopt - result ENOMEM\n");
        return(-ENOMEM);
    }

    espcp_log_socket_option_name(level, option);

    int32_t result = 0;
    switch (level)
    {
        case SOL_SOCKET:
            result = espcp_usrsock_encode_socket_option_value(request, option, value, value_len);
            break;
        case SOL_TCP:
            result = espcp_usrsock_encode_tcp_option_value(request, option, value, value_len);
            break;
        default:
            break;
    }
    if (result < 0)
    {
        free(request);
        return(result);
    }

    espcp_message_t *message = NULL;
    if (request->option_value != NULL)
    {
        request->socket_handle = espcp_sockfd(psock);
        request->level = level;
        request->option_name = option;

        int payload_length = espcp_set_sock_opt_request_buffer_size(request);
        uint8_t *payload = (uint8_t *) zalloc(payload_length);
        if (payload == NULL)
        {
            free(request->option_value);
            free(request);
            result = -ENOMEM;
        }
        else
        {
            espcp_encode_set_sock_opt_request(request, payload);
            free(request->option_value);
            free(request);

            message = espcp_create_message_on_heap(espcp_message_types_header, espcp_esp32_interfaces_wi_fi,
                                                espcp_wi_fi_function_set_sock_opt, espcp_status_codes_completed_ok,
                                                espcp_get_next_message_id(), payload, payload_length);
            if (message == NULL)
            {
                free(payload);
                result = -ENOMEM;
            }
            else
            {
                if (espcp_queue_message(message, true) == espcp_status_codes_completed_ok)
                {
                    switch (message->status_code)
                    {
                        case espcp_status_codes_completed_ok:
                            {
                                espcp_integer_and_errno_response_t *response = espcp_extract_integer_and_errno_response(message->payload);
                                if (response == NULL)
                                {
                                    result = -ENOMEM;
                                }
                                else
                                {
                                    result = (response->result < 0) ? -response->response_errno : response->result;
                                    free(response);
                                }
                            }
                            break;
                        case espcp_status_codes_thread_pool_is_full:
                        case espcp_status_codes_esp_out_of_memory:
                            result = -ENOMEM;
                            break;
                        default:
                            result = -1;
                            break;
                    }
                }
            }
        }
    }
    else
    {
        //
        //  For non-supported options, pretend we have succeeded.
        //
        result = 0;
    }

    espcp_delete_message_and_payload(message);

    MEADOW_TRACE_INFORMATION("setsockopt - socket %d, result %" PRId32 "\n", espcp_sockfd(psock), result);

    return (result);
}

/****************************************************************************
 * Name: espcp_usrsock_socket
 *
 * Description:
 *   socket() creates an endpoint for communication and returns a socket
 *   structure.
 *
 * Input Parameters:
 *   domain   - (see sys/socket.h)
 *   type     - (see sys/socket.h)
 *   protocol - (see sys/socket.h)
 *   psock    - A pointer to a user allocated socket structure to be
 *              initialized.
 *
 * Returned Value:
 *  0 on success, negated errno on error.
 *
 *   EACCES
 *     Permission to create a socket of the specified type and/or protocol
 *     is denied.
 *   EAFNOSUPPORT
 *     The implementation does not support the specified address family.
 *   EINVAL
 *     Unknown protocol, or protocol family not available.
 *   EMFILE
 *     Process file table overflow.
 *   ENFILE
 *     The system limit on the total number of open files has been reached.
 *   ENOBUFS or ENOMEM
 *     Insufficient memory is available. The socket cannot be created until
 *     sufficient resources are freed.
 *   EPROTONOSUPPORT
 *     The protocol type or the specified protocol is not supported within
 *     this domain.
 *
 * Assumptions:
 *
 ****************************************************************************/
int espcp_usrsock_socket(int domain, int type, int protocol, struct socket *psock)
{
    MEADOW_TRACE_INFORMATION("socket(%d, %d, %d)\n", domain, type, protocol);
    meadow_watchdog_t watchdog_socket = meadow_watchdog_activate(WATCHDOG_SOCKET_TIMEOUT_MILLISECONDS, SOCKET_WATCHDOG);

    if (espcp_get_configuration()->esp_not_responding)
    {
        meadow_watchdog_deactivate(watchdog_socket);
        MEADOW_TRACE_DEBUG("socket - result ENETDOWN\n");
        return(-ENETDOWN);
    }

    int result = -1;

    espcp_socket_request_t *request = (espcp_socket_request_t *) zalloc(sizeof(espcp_socket_request_t));
    if (request == NULL)
    {
        meadow_watchdog_deactivate(watchdog_socket);
        MEADOW_TRACE_DEBUG("socket - result ENOMEM\n");
        return (-ENOMEM);
    }
    request->domain = domain;
    request->type = type;
    request->protocol = protocol;
    //
    //  Since NuttX 12.1 (abba05a934) getifaddrs() and the other users of NET_SOCK_TYPE
    //  open a SOCK_CTRL socket just to issue interface ioctls.  lwIP on the ESP32 has no
    //  SOCK_CTRL and rejects it with EINVAL, so ask for a datagram socket instead (as
    //  NET_SOCK_TYPE was before 12.1).  The local socket keeps the SOCK_CTRL type.
    //
    if (type == SOCK_CTRL)
    {
        request->type = SOCK_DGRAM;
    }

    int payload_length = espcp_socket_request_buffer_size(request);
    uint8_t *payload = (uint8_t *) zalloc(payload_length);
    if (payload == NULL)
    {
        free(request);
        meadow_watchdog_deactivate(watchdog_socket);
        return (-ENOMEM);
    }
    espcp_encode_socket_request(request, payload);
    free(request);

    espcp_message_t *message = espcp_create_message_on_heap(espcp_message_types_header, espcp_esp32_interfaces_wi_fi,
                                                            espcp_wi_fi_function_socket,
                                                            espcp_status_codes_completed_ok,
                                                            espcp_get_next_message_id(), payload, payload_length);
    if (message == NULL)
    {
        free(payload);
        meadow_watchdog_deactivate(watchdog_socket);
        MEADOW_TRACE_DEBUG("socket - result ENOMEM\n");
        return (-ENOMEM);
    }
    else
    {
        if (espcp_queue_message(message, true) == espcp_status_codes_completed_ok)
        {
            switch (message->status_code)
            {
                case espcp_status_codes_completed_ok:
                    {
                        espcp_integer_and_errno_response_t *response = espcp_extract_integer_and_errno_response(message->payload);
                        if (response == NULL)
                        {
                            result = -ENOMEM;
                        }
                        else
                        {
                            if (response->result < 0)
                            {
                                result = -response->response_errno;
                            }
                            else
                            {
                                result = response->result;
                            }
                            free(response);
                            psock->s_domain = domain;
                            psock->s_type = type;
                            if (result >= 0)
                            {
                                struct espcp_usrsock_conn_s *conn = (struct espcp_usrsock_conn_s *) zalloc(sizeof(struct espcp_usrsock_conn_s));
                                if (conn == NULL)
                                {
                                    result = -ENOMEM;
                                }
                                else
                                {
                                    conn->esp_sockfd = result;
                                    psock->s_conn = conn;
                                }
                            }
                        }
                    }
                    break;
                case espcp_status_codes_thread_pool_is_full:
                case espcp_status_codes_esp_out_of_memory:
                    result = -ENOMEM;
                    break;
                default:
                    result = -1;
                    break;
            }
        }
    }

    espcp_delete_message_and_payload(message);
    meadow_watchdog_deactivate(watchdog_socket);

    MEADOW_TRACE_INFORMATION("socket - socket %d, result %d\n",
                             psock->s_conn ? espcp_sockfd(psock) : -1, result);

    return (result);
}

/****************************************************************************
 * Name: espcp_usrsock_sendmsg
 *
 * Description:
 *  TODO: Implement this method and complete the comment header.
 * 
 * Input Parameters:
 * 
 *  psock - Pointer to the socket structure to read from
 *  msg - Message to send.
 *  flags - Flags
 *
 * Returned Value:
 *  -1
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
ssize_t espcp_usrsock_sendmsg(struct socket *psock, struct msghdr *msg, int flags)
{
    MEADOW_TRACE_INFORMATION("sendmsg(%d, %p, %d)\n", espcp_sockfd(psock), msg, flags);

    if (espcp_get_configuration()->esp_not_responding)
    {
        MEADOW_TRACE_DEBUG("sendmsg - result ENETDOWN\n");
        return(-ENETDOWN);
    }

    espcp_encoded_msghdr_t *encoded_hdr = espcp_encode_msghdr(msg);
    if (encoded_hdr == NULL)
    {
        MEADOW_TRACE_DEBUG("sendmsg - result ENOMEM\n");
        return(-ENOMEM);
    }

    espcp_sendmsg_request_t *request = (espcp_sendmsg_request_t *) zalloc(sizeof(espcp_sendmsg_request_t));
    if (request == NULL)
    {
        espcp_free_encoded_msghdr(encoded_hdr);
        MEADOW_TRACE_DEBUG("sendmsg - result ENOMEM\n");
        return(-ENOMEM);
    }
    request->socket_handle = espcp_sockfd(psock);
    request->msghdr_data = encoded_hdr->scatter_array;
    request->msghdr_length = encoded_hdr->scatter_array_length;
    request->flags = flags;

    int payload_length = espcp_sendmsg_request_buffer_size(request);
    uint8_t *payload = (uint8_t *) zalloc(payload_length);
    if (payload == NULL)
    {
        espcp_free_encoded_msghdr(encoded_hdr);
        free(request);
        MEADOW_TRACE_DEBUG("sendmsg - result ENOMEM\n");
        return(-ENOMEM);
    }
    espcp_encode_sendmsg_request(request, payload);
    espcp_free_encoded_msghdr(encoded_hdr);
    free(request);

    espcp_message_t *message = espcp_create_message_on_heap(espcp_message_types_header, espcp_esp32_interfaces_wi_fi,
                                        espcp_wi_fi_function_send_msg, espcp_status_codes_completed_ok,
                                        espcp_get_next_message_id(), payload, payload_length);
    int32_t result = -1;
    if (message == NULL)
    {
        free(payload);
        result = -ENOMEM;
    }
    else
    {
        if (espcp_queue_message(message, true) == espcp_status_codes_completed_ok)
        {
            switch (message->status_code)
            {
                case espcp_status_codes_completed_ok:
                    {
                        espcp_integer_and_errno_response_t *response = espcp_extract_integer_and_errno_response(message->payload);
                        if (response == NULL)
                        {
                            result = -ENOMEM;
                        }
                        else
                        {
                            result = (response->result < 0) ? -response->response_errno : response->result;
                            free(response);
                        }
                    }
                    break;
                case espcp_status_codes_thread_pool_is_full:
                case espcp_status_codes_esp_out_of_memory:
                    result = -ENOMEM;
                    break;
                default:
                    break;
            }
        }
    }

    espcp_delete_message_and_payload(message);

    //
    //  A send that returned EAGAIN means the ESP32's send buffer is full, so POLLOUT has to
    //  come from the ESP32 until a send succeeds; a successful send also shows the socket is
    //  connected.
    //
    if (result == -EAGAIN)
    {
        espcp_conn(psock)->esp_state |= ESPCP_SF_SEND_EAGAIN;
        espcp_conn(psock)->esp_state &= ~ESPCP_SF_WR_READY;
    }
    if (result >= 0)
    {
        espcp_conn(psock)->esp_state &= ~(ESPCP_SF_SEND_EAGAIN | ESPCP_SF_CONNECT_INPROGRESS | ESPCP_SF_WR_READY);
    }

    MEADOW_TRACE_INFORMATION("sendmsg: socket %d, result: %" PRId32 "\n", espcp_sockfd(psock), result);

    return (result);
}

/****************************************************************************
 * Name: espcp_usrsock_shutdown
 *
 * Description:
 *  TODO: Implement this method and complete the comment header.
 * 
 * Input Parameters:
 * 
 *  psock - Pointer to the socket structure to read from
 *  how - Indicate how the socket should be shutdown.
 *
 * Returned Value:
 *  -1
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
int espcp_usrsock_shutdown(struct socket *psock, int how)
{
  espcp_usrsock_not_implemented(__func__);
  return(-EOPNOTSUPP);
}

/****************************************************************************
 * Name: espcp_usrsock_recvmsg
 *
 * Description:
 *  Receive a message from the socket via the ESP32, populating the data,
 *  source address, ancillary control data, and output flags of the
 *  supplied msghdr.
 *
 * Input Parameters:
 *
 *  psock - Pointer to the socket structure to read from
 *  msg - Pointer to a msghdr that will receive the message.
 *  flags - Receive flags
 *
 * Returned Value:
 *  Number of bytes received on success, negated errno on error.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
ssize_t espcp_usrsock_recvmsg(struct socket *psock, struct msghdr *msg, int flags)
{
    MEADOW_TRACE_INFORMATION("recvmsg(%d, %p, %d)\n", espcp_sockfd(psock), msg, flags);

    if (espcp_get_configuration()->esp_not_responding)
    {
        MEADOW_TRACE_DEBUG("recvmsg - result ENETDOWN\n");
        return(-ENETDOWN);
    }

    size_t total_length = 0;
    for (int i = 0; i < (int) msg->msg_iovlen; i++)
    {
        total_length += msg->msg_iov[i].iov_len;
    }
    if (total_length > ESPCP_MAXIMUM_PAYLOAD_SIZE)
    {
        total_length = ESPCP_MAXIMUM_PAYLOAD_SIZE;
    }

    espcp_recvmsg_request_t *request = (espcp_recvmsg_request_t *) zalloc(sizeof(espcp_recvmsg_request_t));
    if (request == NULL)
    {
        MEADOW_TRACE_DEBUG("recvmsg - result ENOMEM\n");
        return(-ENOMEM);
    }
    request->socket_handle = espcp_sockfd(psock);
    request->length = (int32_t) total_length;
    request->flags = flags;
    request->get_source_address = (msg->msg_name != NULL);
    request->control_length = (uint32_t) msg->msg_controllen;

    int payload_length = espcp_recvmsg_request_buffer_size(request);
    uint8_t *payload = (uint8_t *) zalloc(payload_length);
    if (payload == NULL)
    {
        free(request);
        MEADOW_TRACE_DEBUG("recvmsg - result ENOMEM\n");
        return(-ENOMEM);
    }
    espcp_encode_recvmsg_request(request, payload);
    free(request);

    espcp_message_t *message = espcp_create_message_on_heap(espcp_message_types_header, espcp_esp32_interfaces_wi_fi,
                                        espcp_wi_fi_function_recv_msg, espcp_status_codes_completed_ok,
                                        espcp_get_next_message_id(), payload, payload_length);
    int32_t result = -1;
    if (message == NULL)
    {
        free(payload);
        result = -ENOMEM;
    }
    else
    {
        if (espcp_queue_message(message, true) == espcp_status_codes_completed_ok)
        {
            switch (message->status_code)
            {
                case espcp_status_codes_completed_ok:
                    {
                        espcp_recvmsg_response_t *response = espcp_extract_recvmsg_response(message->payload);
                        if (response == NULL)
                        {
                            MEADOW_TRACE_DEBUG("recvmsg - result ENOMEM\n");
                            result = -ENOMEM;
                        }
                        else
                        {
                            if (response->result > 0)
                            {
                                uint32_t remaining = response->iov_data_length;
                                uint8_t *src = response->iov_data;
                                for (int i = 0; i < (int) msg->msg_iovlen && remaining > 0; i++)
                                {
                                    uint32_t copy_len = ((uint32_t) msg->msg_iov[i].iov_len < remaining) ? (uint32_t) msg->msg_iov[i].iov_len : remaining;
                                    memcpy(msg->msg_iov[i].iov_base, src, copy_len);
                                    src += copy_len;
                                    remaining -= copy_len;
                                }
                                if (response->source_address_length > 0 && msg->msg_name != NULL)
                                {
                                    uint32_t copy_len = (response->source_address_length < (uint32_t) msg->msg_namelen) ? response->source_address_length : (uint32_t) msg->msg_namelen;
                                    memcpy(msg->msg_name, response->source_address, copy_len);
                                    msg->msg_namelen = (socklen_t) response->source_address_len;
                                }
                                if (response->control_length > 0 && msg->msg_control != NULL)
                                {
                                    uint32_t copy_len = (response->control_length < (uint32_t) msg->msg_controllen) ? response->control_length : (uint32_t) msg->msg_controllen;
                                    memcpy(msg->msg_control, response->control, copy_len);
                                    msg->msg_controllen = (socklen_t) response->control_length;
                                }
                                msg->msg_flags = response->msg_flags;
                                result = response->result;
                            }
                            else
                            {
                                result = -response->response_errno;
                            }
                            espcp_free_recvmsg_response(response);
                        }
                    }
                    break;
                case espcp_status_codes_thread_pool_is_full:
                case espcp_status_codes_esp_out_of_memory:
                    result = -ENOMEM;
                    break;
                default:
                    break;
            }
        }
    }

    espcp_delete_message_and_payload(message);

    //
    //  Data shows the socket is connected.  EAGAIN means the ESP32's receive buffer is empty,
    //  so whether it is readable is unknown until the next poll interrupt.
    //
    if (result >= 0)
    {
        espcp_conn(psock)->esp_state &= ~ESPCP_SF_CONNECT_INPROGRESS;
    }
    if (result == -EAGAIN)
    {
        espcp_conn(psock)->esp_state &= ~ESPCP_SF_RD_READY;
    }

    MEADOW_TRACE_INFORMATION("recvmsg: socket %d, result: %" PRId32 "\n", espcp_sockfd(psock), result);

    return (result);
}

/****************************************************************************
 * Name: espcp_usrsock_not_implemented
 *
 * Description:
 *  Output a message to the logs indicating that a method that is not 
 *  implemented has been called.
 * 
 * Input Parameters:
 * 
 *  source - Method that has been called.
 *
 * Returned Value:
 *  None.
 *
 * Assumptions/Limitations:
 *  None
 *
 ****************************************************************************/
void espcp_usrsock_not_implemented(const char *source)
{
    syslog(LOG_CRIT, "%s@%d %s not implemented.\n", __FILE__, __LINE__, source);
}
