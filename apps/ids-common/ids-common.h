#ifndef __IDS_COMMON_H__
#define __IDS_COMMON_H__
#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_UDP_BUF          ((struct uip_udp_hdr *)&uip_buf[UIP_LLIPH_LEN])
#define UIP_ICMP_BUF            ((struct uip_icmp_hdr *)&uip_buf[uip_l2_l3_hdr_len])

#define MAPPER_CLIENT_PORT 4713
#define MAPPER_SERVER_PORT 4714

#include "net/uip.h"

/**
 * Copy the contents from source (needs to be a normal type) to dest, which
 * needs to be a pointer and increment dest by the size of source.
 *
 * Usefull for writing data to a buffer, the inverse of MAPPER_GET_PACKETDATA
 */
#define MAPPER_ADD_PACKETDATA(dest, source) \
  memcpy(dest, &source, sizeof(source)); dest += sizeof(source)
/**
 * Copy the contents from source (needs to be a pointer) to dest, which
 * needs to be normal data type. Increase source with the size of dest.
 *
 * Usefull for reading data from a buffer, the inverse of MAPPER_ADD_PACKETDATA
 */
#define MAPPER_GET_PACKETDATA(dest, source) \
  memcpy(&dest, source, sizeof(dest)); source += sizeof(dest)

void make_ipaddr_global(uip_ipaddr_t *);

uint16_t compress_ipaddr_t(uip_ipaddr_t *);

#endif

