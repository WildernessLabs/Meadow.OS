/****************************************************************************
 * Meadow.OS/ethernet/meadow_ethnet_local.h
 *
 *   Copyright (C) 2021-2023 Wilderness Labs. All rights reserved.
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
#ifndef __CONFIGS_MEADOW_SRC_MEADOW_ETHNET_LOCAL__H
#define __CONFIGS_MEADOW_SRC_MEADOW_ETHNET_LOCAL__H

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <nuttx/config.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <stdio.h>

#include <netinet/in.h>
#include <net/if.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* IFF_DOWN was removed from NuttX net/if.h; in NuttX, the absence of
 * IFF_UP (value 0) represents the interface-down state.
 */

#ifndef IFF_DOWN
#  define IFF_DOWN 0
#endif

#define MEADOW_THREAD_NAME_ETHNET_CONNECTION "EthConn"
#define MEADOW_THREAD_PRIORITY_ETHNET_CONNECTION 120
#define MEADOW_THREAD_STACKSIZE_ETHNET_CONNECTION 4096

// Needed to verify that the connected lan chip is the supported LAN9355
#define LAN9355_CHIP_ID_REVISION_REGISTER (0x50)  // 32-bit register

/****************************************************************************
 * Private Data
 ****************************************************************************/

// Note in Nuttx 12 there were 2 structs one named 'dhcp_state_s' another
// named 'dhcp_state'. This struct was named 'dhcp_state' it's here as
// 'dhcp_info_s' to avoid additional confusion.
struct dhcp_info_s
{
  struct in_addr serverid;
  struct in_addr ipaddr;
  struct in_addr netmask;
  struct in_addr dnsaddr;
  struct in_addr default_router;
  uint32_t       lease_time;      /* Lease expires in this number of seconds */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

// Utilities
void meadow_eth_utils_syslog_ip_mac(bool useDhcp);
int meadow_eth_utils_get_hw_mac(const char *interfaceName, uint8_t *macAddr);
int meadow_eth_utils_exec_ifup(const char *interfaceName);
int meadow_eth_utils_exec_ifdown(const char *interfaceName);
int meadow_eth_utils_set_mac(const char *interfaceName, const uint8_t *macAddr);
int meadow_eth_utils_set_ipv4(const char *interfaceName,
          const struct in_addr *addr);
int meadow_eth_utils_get_ipv4(const char *interfaceName,
          struct in_addr *addr);
int meadow_eth_utils_get_mac(const char *interfaceName,
          uint8_t *macAddr);
int meadow_eth_utils_set_ipv4_mask(const char *interfaceName,
          const struct in_addr *addr);
int meadow_eth_utils_set_dns(const struct in_addr *inaddr);
int meadow_eth_utils_set_router(const char *interfaceName,
          const struct in_addr *addr);
uint32_t meadow_eth_utils_parse_ip_str(const char *address);
int meadow_eth_utils_verify_lan9355(void);

int meadow_lan9355_phywrite_16(uint16_t phyAddr, uint16_t regAddr, uint16_t value);
int meadow_lan9355_phyread_16(uint16_t phyAddr, uint16_t regAddr, uint16_t *value);
int meadow_lan9355_phywrite_32(uint16_t csrAddr, uint32_t value);
int meadow_lan9355_phyread_32(uint16_t csrAddr, uint32_t *value);

// Shared, non-utility functions
int meadow_eth_dhcp_get_dhcp_info(struct dhcp_info_s *dhcp_info,
          const char *interfaceName, const uint8_t *macAddr);

int meadow_eth_mon_config_lan9355_irq(void);
int meadow_eth_mon_disable_lan9355_irq(void);
int meadow_eth_mon_reenable_lan9355_irq(void);

int meadow_eth_conn_startup(void);
int meadow_eth_conn_link_status_changed(bool linkStatusUp);



/////////////////////////////////////////////////////////////////////// 
// STUFF FROM V12 v

// Copied from /Meadow.OS/esp32/components/F7Services/WiFi/WiFiRequestHandler.hpp
// but needed here
#define _SIOCBASE           (0x0700)            /* Socket ioctl commands */
#define _IOC(type,nr)       ((type)|(nr))
#define _SIOC(nr)           _IOC(_SIOCBASE,nr)
#define SIOCGIFADDR         _SIOC(0x0001)       /* Get IP address */


/****************************************************************************
 * Public Types
 ****************************************************************************/

 typedef void (*dhcpc_callback_t)(FAR struct dhcp_info_s *presult);
 
 /****************************************************************************
  * Public Function Prototypes
  ****************************************************************************/
 
 #ifdef __cplusplus
 #define EXTERN extern "C"
 extern "C"
 {
 #else
 #define EXTERN extern
 #endif
 
 FAR void *dhcpc_open(FAR const char *interface,
                      FAR const void *mac_addr, int mac_len);
 int  dhcpc_request(FAR void *handle, FAR struct dhcp_info_s *presult);
 int  dhcpc_request_async(FAR void *handle, dhcpc_callback_t callback);
 void dhcpc_cancel(FAR void *handle);
 void dhcpc_close(FAR void *handle);
 
 // Added 
 #ifdef CONFIG_NET_IPv4
int netlib_get_ipv4addr(FAR const char *ifname, FAR struct in_addr *addr);
int netlib_set_ipv4addr(FAR const char *ifname,
                        FAR const struct in_addr *addr);
#endif

 #undef EXTERN
 #ifdef __cplusplus
 }
 #endif
/////////////////////////////////////////////////////////////////////// 


#endif // __CONFIGS_MEADOW_SRC_MEADOW_ETHNET_LOCAL__H
