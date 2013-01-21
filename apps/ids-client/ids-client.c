#include "ids-client.h"
#include "ids-common.h"

#include "net/uip.h"
#include "net/uip-ds6.h"

#include "net/rpl/rpl.h"
#include "net/rpl/rpl-private.h"

#define DEBUG DEBUG_NONE
#include "net/uip-debug.h"

#include <stdio.h>

extern uip_ds6_route_t uip_ds6_routing_table[];
extern uip_ds6_defrt_t uip_ds6_defrt_list[];

/**
 * This method indicates a packet is lost when sending to the specified
 * destination.
 * 
 * It will slightly alter the routing metric for the parent used for that path
 * in order to, over time, stop using the parent in question.
 */
void packet_lost(uip_ipaddr_t * dest) {
  PRINTF("Packet lost on route to");
  PRINT6ADDR(dest);
  PRINTF("\n");

  uip_ipaddr_t * nexthop = NULL;
  if(uip_ds6_is_addr_onlink(&UIP_IP_BUF->destipaddr)){
    // XXX We probably dont want to do anything if the destination is a
    // neighbor, as that most often are handled by ETX
    PRINTF("A direct neighbor, doing nothing\n");
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
    PRINTF("Were unable to update metric\n");
    return;
  }

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

  PRINTF("Updated metric from %d to %d\n", old_metric, *metric);
}

