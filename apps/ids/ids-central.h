#ifndef __IDS_CENTRAL_H__
#define __IDS_CENTRAL_H__

#include "ids-common.h"

#define NETWORK_NODES 8
#define NETWORK_DENSITY 8 // The number of neighbors for each node

extern uip_ds6_route_t uip_ds6_routing_table[];
extern rpl_instance_t instance_table[];

struct ids_host_info {
  uip_ipaddr_t addr;
  int8_t outstanding_echos;
};

struct ids_host_info host[NETWORK_NODES];
int hosts = 0;

#endif