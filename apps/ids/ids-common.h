#ifndef __IDS_COMMON_H__
#define __IDS_COMMON_H__
#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_UDP_BUF          ((struct uip_udp_hdr *)&uip_buf[UIP_LLIPH_LEN])
#define UIP_ICMP_BUF            ((struct uip_icmp_hdr *)&uip_buf[uip_l2_l3_hdr_len])

#define MAPPER_CLIENT_PORT 4713
#define MAPPER_SERVER_PORT 4714

#include "net/uip.h"

/**
 * Indicate to the IDS that a packet was lost while trying to transmitt to the
 * specificed destination
 */
void packet_lost(uip_ipaddr_t * dest);

#endif

