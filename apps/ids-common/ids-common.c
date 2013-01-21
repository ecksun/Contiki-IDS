#include "ids-common.h"

#include "net/uip.h"

#define DEBUG DEBUG_NONE
#include "net/uip-debug.h"

/**
 * Utility method to make a ipv6 adress global
 */
void make_ipaddr_global(uip_ipaddr_t * ip) {
  ip->u16[0] = 0xaaaa;
}

/**
 * Compress a ipv6 adress down as much as possible
 */
uint16_t compress_ipaddr_t(uip_ipaddr_t * ipaddr) {
  PRINTF("Compressing ");
  PRINT6ADDR(ipaddr);
  PRINTF(" to %x\n", ipaddr->u16[7]);
  return ipaddr->u16[7];
}
