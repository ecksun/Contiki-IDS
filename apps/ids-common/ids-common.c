#include "ids-common.h"

#include "net/uip.h"

#define DEBUG DEBUG_NONE
#include "net/uip-debug.h"

void make_ipaddr_global(uip_ipaddr_t * ip) {
  ip->u16[0] = 0xaaaa;
}

uint16_t compress_ipaddr_t(uip_ipaddr_t * ipaddr) {
  PRINTF("Compressing ");
  PRINT6ADDR(ipaddr);
  PRINTF(" to %x\n", ipaddr->u16[7]);
  return ipaddr->u16[7];
}
