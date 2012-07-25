#ifndef __IDS_CENTRAL_H__
#define __IDS_CENTRAL_H__

#include "ids-common.h"

#define NETWORK_NODES 13
#define NETWORK_DENSITY 8 // The number of neighbors for each node
#define MAPPING_RECENT_WINDOW 1 // Acceptably old information, in MAPPING_INTERVAL units

#define MAPPING_INTERVAL 120 * CLOCK_SECOND // Time between new mapping atempts

#define MAPPING_HOST_INTERVAL MAPPING_INTERVAL / NETWORK_NODES

extern uip_ds6_route_t uip_ds6_routing_table[];
extern rpl_instance_t instance_table[];

struct ids_host_info {
  uip_ipaddr_t addr;
  int8_t outstanding_echos;
};

struct ids_host_info host[NETWORK_NODES];
int hosts = 0;

#endif
