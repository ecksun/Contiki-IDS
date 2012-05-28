#include "ids-common.h"

#include "net/uip.h"
#include "net/uip-ds6.h"

#include "net/uip-debug.h"

#include <stdio.h>

extern uip_ds6_route_t uip_ds6_routing_table[];
extern uip_ds6_defrt_t uip_ds6_defrt_list[];

void packet_lost(uip_ipaddr_t * dest) {
  uip_ds6_route_t * route = uip_ds6_route_lookup(dest);

  if (route != NULL) {
    printf("ROUTE IS NOT NULL!!!!!!\n");
  }

  printf("Stuff heading to ");
  uip_debug_ipaddr_print(dest);
  printf(" lost\n");

  printf("uip_ds6_defrt_list: \n");
  int i;
  for (i = 0; i < UIP_DS6_DEFRT_NB; ++i) {
    if (!uip_ds6_defrt_list[i].isused)
      continue;
    printf("%d: ", i);
    uip_debug_ipaddr_print(&uip_ds6_defrt_list[i].ipaddr);
    printf("\n");
  }

  // TODO Find the route corresponidng to the next-hop and change its metric
  /* Next hop determination */
  uip_ipaddr_t * nexthop = NULL;
  if(!uip_is_addr_mcast(dest)) {
    if(uip_ds6_is_addr_onlink(&UIP_IP_BUF->destipaddr)){
      nexthop = &UIP_IP_BUF->destipaddr;
    } else {
      uip_ds6_route_t* locrt;
      locrt = uip_ds6_route_lookup(&UIP_IP_BUF->destipaddr);
      if(locrt == NULL) {
        if((nexthop = uip_ds6_defrt_choose()) == NULL) {
          printf("Couldnt find a nexthop\n");
          return;
        }
      } else {
        nexthop = &locrt->nexthop;
      }
    }
  }

  printf("The nexthop: ");
  uip_debug_ipaddr_print(nexthop);
  printf("\n");
}
