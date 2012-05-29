#include "ids-common.h"

#include "net/uip.h"
#include "net/uip-ds6.h"

#include "net/rpl/rpl.h"
#include "net/rpl/rpl-private.h"

#include "net/uip-debug.h"

#include <stdio.h>

extern uip_ds6_route_t uip_ds6_routing_table[];
extern uip_ds6_defrt_t uip_ds6_defrt_list[];

void packet_lost(uip_ipaddr_t * dest) {
  printf("Stuff heading to ");
  uip_debug_ipaddr_print(dest);
  printf(" lost\n");

  uip_ipaddr_t * nexthop = NULL;
  if(uip_ds6_is_addr_onlink(&UIP_IP_BUF->destipaddr)){
    // XXX Do we really care if the destination is onlink? It would be
    // handled by RPL in the normal case, then again it might be because of
    // interference from an attacking node, in which case we probably want to
    // do something about it (more than what ETX already does, perhaps).
    nexthop = &UIP_IP_BUF->destipaddr;
    printf("A direct neighbor, let other stuff handle that\n");
    return;
  }

  uip_ds6_route_t* route;
  route = uip_ds6_route_lookup(&UIP_IP_BUF->destipaddr);
  uint8_t * metric = NULL;

  // Check if we found a route
  if (route != NULL) {
    metric = &route->metric;
  }
  // Check and fetch the default route if no RPL route was found
  else if ((nexthop = uip_ds6_defrt_choose()) != NULL) {
    rpl_parent_t * parent = rpl_find_parent_any_dag(default_instance, nexthop);

    if (parent != NULL) {
      metric = &parent->link_metric;
    }
  }

  if (metric == NULL) {
    printf("Were unable to update metric\n");
    return;
  }

  printf("%d\n", *metric);

  uint8_t old_metric = *metric;
  *metric -= (*metric/5);

  // Check so we actually changed something (integer division can be a
  // problem).
  if (*metric == old_metric) {
    *metric -= 1;
  }
  // Prevent underflows
  else if (*metric > old_metric) {
    *metric = 0;
  }

  printf("Updated metric from %d to %d\n", old_metric, *metric);
}
