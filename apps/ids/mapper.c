/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/uip.h"
#include "net/rpl/rpl.h"
#include "net/rime/rimeaddr.h"

#include "ids-central.h"

#include "net/netstack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DEBUG DEBUG_NONE
#include "net/uip-debug.h"

static struct uip_udp_conn *ids_conn;
static int working_host = 0;
static uint8_t current_rpl_instance_id;
static uip_ipaddr_t current_dag_id;
static int mapper_instance = 0;
static int mapper_dag = 0;
static struct etimer map_timer;

static uip_ipaddr_t tmp_ip;

PROCESS(mapper, "IDS network mapper");
AUTOSTART_PROCESSES(&mapper);

struct Node;

struct Child {
  struct Node * node;
  rpl_rank_t rank;
};

struct Node {
  uip_ipaddr_t * id;
  struct Node * parent;
  rpl_rank_t parent_rank;
  struct Child children[NETWORK_DENSITY];
  uint16_t neighbors;
  uint8_t visited;
};

static struct Node network[NETWORK_NODES];
static int node_index = 0;

/**
 * Search for a node by IP
 *
 * @return Returns a pointer to the node or NULL if none is found
 */
struct Node * find_node(uip_ipaddr_t * ip) {
  int i;
  for (i = 0; i < node_index; ++i) {
    if (uip_ipaddr_cmp(ip, network[i].id)) {
      return &network[i];
    }
  }
  return NULL;
}

/**
 * Add a new node to the network graph
 *
 * Note that as the ip pointer is saved the memory it points to needs to be
 * kept
 *
 * If the node already exists in the network no new node will be added and a
 * pointer to that adress will be returned
 *
 * @return A pointer to the new node or NULL if we ran out of memory
 */
struct Node * add_node(uip_ipaddr_t * ip) {

  memcpy(&tmp_ip, ip, sizeof(tmp_ip));
  make_ipaddr_global(&tmp_ip);

  struct Node * node = find_node(&tmp_ip);

  if (node != NULL)
    return node;

  if (node_index >= NETWORK_NODES) // Out of memory
    return NULL;

  PRINTF("Creating new node: ");
  PRINT6ADDR(&tmp_ip);
  PRINTF("\n");
  int i;
  // Find the IP in the routing table, we want our records to point to them in
  // order to have them somewhat persistent, and at the same time save memory
  // (by only pointing to RPLs IPs)
  for (i = 0; i < UIP_DS6_ROUTE_NB; ++i) {
    if (!uip_ds6_routing_table[i].isused)
      continue;
    if (uip_ipaddr_cmp(&uip_ds6_routing_table[i].ipaddr, ip)) {
      network[node_index].id = &uip_ds6_routing_table[i].ipaddr;
      return &network[node_index++];
    }
  }
  return NULL;
}

void print_subtree(struct Node * node, int depth) {
  int i;
  printf("%*s", depth*2, "");

  uip_debug_ipaddr_print(node->id);

  if (node->visited) {
    printf("\n");
    return;
  }
  node->visited = 1;

  printf("    {");

  for (i = 0; i < node->neighbors; ++i) {
    uip_debug_ipaddr_print(node->children[i].node->id);
    printf(" (%d) ,", node->children[i].rank);
  }
  printf("}\n");

  for (i = 0; i < node->neighbors; ++i) {
    if (uip_ipaddr_cmp(node->children[i].node->parent->id, node->id))
      print_subtree(node->children[i].node, depth+1);
  }
}

void print_graph() {
  int i;
  for (i = 0; i < node_index; ++i) {
    network[i].visited = 0;
  }
  printf("Network graph:\n\n");
  print_subtree(&network[0], 0);
  printf("-----------------------\n");
}

void tcpip_handler() {
  if (!uip_newdata())
    return;

  uint8_t *appdata;
  uip_ipaddr_t src_ip, parent_ip;
  struct Node *id, *parent;

  appdata = (uint8_t *)uip_appdata;
  memcpy(&src_ip, appdata, sizeof(src_ip));
  PRINTF("Source IP: ");
  PRINT6ADDR(&src_ip);
  make_ipaddr_global(&src_ip);
  PRINTF("\n");
  id = add_node(&src_ip);
  if (id == NULL)
    return;
  PRINTF("Found node ");
  PRINT6ADDR(id->id);
  PRINTF("\n");

  appdata += sizeof(uint8_t) + 2*sizeof(uip_ipaddr_t);
  memcpy(&parent_ip, appdata, sizeof(parent_ip));
  appdata += sizeof(parent_ip);
  make_ipaddr_global(&parent_ip);
  parent = add_node(&parent_ip);
  if (parent == NULL)
    return;
  PRINTF("Found parent ");
  PRINT6ADDR(parent->id);
  PRINTF("\n");

  id->parent = parent;

  uint16_t neighbors;
  uip_ipaddr_t * neighbor_ip;

  // Get the number of neighbors
  memcpy(&neighbors, appdata, sizeof(neighbors));
  appdata += sizeof(neighbors);

  int i;
  // Scan all neighbors
  for (i = 0; i < neighbors && i < NETWORK_DENSITY; ++i) {
    neighbor_ip = (uip_ipaddr_t *)appdata;
    appdata += sizeof(*neighbor_ip);

    make_ipaddr_global(neighbor_ip);

    id->children[i].node = add_node(neighbor_ip);

    memcpy(&id->children[i].rank, appdata, sizeof(id->children[i].rank));
    appdata += sizeof(id->children[i].rank);
  }
  id->neighbors = neighbors;
}

void map_network() {
  for (; working_host < UIP_DS6_ROUTE_NB && !uip_ds6_routing_table[working_host].isused; ++working_host);
  if (!uip_ds6_routing_table[working_host].isused) {
    ++working_host;
    return;
  }
  static char data[sizeof(current_rpl_instance_id) + sizeof(current_dag_id)];
  void * data_p = data;
  memcpy(data_p, &current_rpl_instance_id, sizeof(current_rpl_instance_id));
  data_p += sizeof(current_rpl_instance_id);
  memcpy(data_p, &current_dag_id, sizeof(current_dag_id));
  add_node(&uip_ds6_routing_table[working_host].ipaddr);

  PRINTF("sending data to: %2d ", working_host);
  PRINT6ADDR(&uip_ds6_routing_table[working_host].ipaddr);
  PRINTF("\n");
  uip_udp_packet_sendto(ids_conn, data, sizeof(data), &uip_ds6_routing_table[working_host++].ipaddr, UIP_HTONS(MAPPER_CLIENT_PORT));
  if (working_host >= UIP_DS6_ROUTE_NB) {
    working_host = 0;
    etimer_reset(&map_timer);
  }
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(mapper, ev, data)
{
  static struct etimer timer;

  PROCESS_BEGIN();

  PROCESS_PAUSE();

  memset(host, 0, sizeof(host));
  memset(network, 0, sizeof(network));

  PRINTF("IDS Server, compile time: %s\n", __TIME__);

  ids_conn = udp_new(NULL, UIP_HTONS(MAPPER_CLIENT_PORT), NULL);
  udp_bind(ids_conn, UIP_HTONS(MAPPER_SERVER_PORT));

  PRINTF("Created a server connection with remote address ");
  PRINT6ADDR(&ids_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n", UIP_HTONS(ids_conn->lport),
      UIP_HTONS(ids_conn->rport));

  etimer_set(&timer, CLOCK_SECOND);
  etimer_set(&map_timer, 20*CLOCK_SECOND);

  // Add this node (root node) to the network graph
  network[0].id = &uip_ds6_get_global(ADDR_PREFERRED)->ipaddr;
  ++node_index;

  while(1) {
    PRINTF("snurrar\n");
    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    }
    else if (etimer_expired(&map_timer)) {

      // Map the next DAG.
      if (working_host == 0) {
        print_graph();
        for (; mapper_instance < RPL_MAX_INSTANCES; ++mapper_instance) {
          if (instance_table[mapper_instance].used == 0)
            continue;
          for (; mapper_dag < RPL_MAX_DAG_PER_INSTANCE; ++mapper_dag) {
            if (!instance_table[mapper_instance].dag_table[mapper_dag].used)
              continue;

            current_rpl_instance_id = instance_table[mapper_instance].instance_id;
            memcpy(&current_dag_id, &instance_table[mapper_instance].dag_table[mapper_dag].dag_id, sizeof(current_dag_id));

            int i, k;
            // Reset the roots neighbor list and ranks

            for (i = 0, k = 0; i < UIP_DS6_ROUTE_NB; ++i) {
              if (uip_ds6_routing_table[i].isused) {
                memcpy(&tmp_ip, &uip_ds6_routing_table[i].nexthop, sizeof(tmp_ip));
                make_ipaddr_global(&tmp_ip);
                if (uip_ipaddr_cmp(&uip_ds6_routing_table[i].ipaddr, &tmp_ip)) {
                  network[0].children[k].node = add_node(&uip_ds6_routing_table[i].ipaddr);
                  network[0].children[k].rank = 0;

                  k++;
                }
              }
            }
            network[0].neighbors = k;

            goto found_network;

          }
          if (mapper_dag >= RPL_MAX_DAG_PER_INSTANCE-1)
            mapper_dag = 0;
        }
        if (mapper_instance >= RPL_MAX_INSTANCES-1)
          mapper_instance = 0;
      }

found_network:
      map_network();
      etimer_reset(&timer);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
