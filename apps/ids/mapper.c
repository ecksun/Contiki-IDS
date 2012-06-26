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

/**
 * Connection for incoming IDS information packets
 */
static struct uip_udp_conn *ids_conn;

/**
 * The id of the host we are currently sending to
 */
static int working_host = 0;

/**
 * A timestamp to make sure mapping information recieved is recent.
 */
static uint8_t timestamp = 0;

/**
 * The current RPL instance id we are working with.
 *
 * Note that support for several RPL instances is not fully implemented
 */
static uint8_t current_rpl_instance_id;

/**
 * The ID of the current DAG we are working with.
 *
 * Note that support for several different DAGs are not fully implemented
 */
static rpl_dag_t * current_dag;

/**
 * The index of the current instance
 */
static int mapper_instance = 0;

/**
 * The index of the current dag
 */
static int mapper_dag = 0;

/**
 * When this timer goes of we will try to map the network further
 */
static struct etimer map_timer;

/**
 * A temporary variable to ease workflow
 */
static uip_ipaddr_t tmp_ip;

PROCESS(mapper, "IDS network mapper");
AUTOSTART_PROCESSES(&mapper);

struct Node;

/**
 * The association between a node and its neighbors
 */
struct Neighbor {
  /**
   * The neighbor
   */
  struct Node *node;
  /**
   * The rank of the node
   */
  rpl_rank_t rank;
};

/**
 * A network node, i.e a sensor.
 */
struct Node {
  /**
   * The ID, and IP adress of this node
   */
  uip_ipaddr_t *id;
  /**
   * A pointer to this nodes parent, or NULL if none exists (that is, it is
   * either the root or uninitialized)
   */
  struct Node *parent;
  /**
   * The index of the parent in the neighbor array, used for easier access
   */
  uint8_t parent_id;
  /**
   * The claimed rank of this node
   */
  rpl_rank_t rank;
  /**
   * A list of all neighbors to this node
   */
  struct Neighbor neighbor[NETWORK_DENSITY];
  /**
   * The amount of neighbors
   */
  uint16_t neighbors;
  /**
   * A status variable, used to help traversing the network graph and such.
   */
  uint8_t visited;
};

/**
 * A list of all nodes in the network
 */
static struct Node network[NETWORK_NODES];

/**
 * The length of the node list (network)
 */
static int node_index = 0;

/**
 * Search for a node by IP
 *
 * @return Returns a pointer to the node or NULL if none is found
 */
struct Node *
find_node(uip_ipaddr_t * ip)
{
  int i;

  for(i = 0; i < node_index; ++i) {
    if(uip_ipaddr_cmp(ip, network[i].id)) {
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
struct Node *
add_node(uip_ipaddr_t * ip)
{

  memcpy(&tmp_ip, ip, sizeof(tmp_ip));
  make_ipaddr_global(&tmp_ip);

  struct Node *node = find_node(&tmp_ip);

  if(node != NULL)
    return node;

  if(node_index >= NETWORK_NODES)       // Out of memory
    return NULL;

  PRINTF("Creating new node: ");
  PRINT6ADDR(&tmp_ip);
  PRINTF("\n");
  int i;

  // Find the IP in the routing table, we want our records to point to them in
  // order to have them somewhat persistent, and at the same time save memory
  // (by only pointing to RPLs IPs)
  for(i = 0; i < UIP_DS6_ROUTE_NB; ++i) {
    if(!uip_ds6_routing_table[i].isused)
      continue;
    if(uip_ipaddr_cmp(&uip_ds6_routing_table[i].ipaddr, ip)) {
      network[node_index].id = &uip_ds6_routing_table[i].ipaddr;
      return &network[node_index++];
    }
  }
  return NULL;
}

void
print_subtree(struct Node *node, int depth)
{
  int i;

  printf("%*s", depth * 2, "");

  uip_debug_ipaddr_print(node->id);

  if(node->visited) {
    printf("\n");
    return;
  }
  node->visited = 1;

  printf(" (%d) ", node->parent_id);

  printf("    {");

  for(i = 0; i < node->neighbors; ++i) {
    uip_debug_ipaddr_print(node->neighbor[i].node->id);
    printf(" (%d) ,", node->neighbor[i].rank);
  }
  printf("}\n");

  for(i = 0; i < node->neighbors; ++i) {
    if(uip_ipaddr_cmp(node->neighbor[i].node->parent->id, node->id))
      print_subtree(node->neighbor[i].node, depth + 1);
  }
}

void
print_graph()
{
  int i;

  for(i = 0; i < node_index; ++i) {
    network[i].visited = 0;
  }
  printf("Network graph:\n\n");
  print_subtree(&network[0], 0);
  for(i = 0; i < node_index; ++i) {
    if(!network[i].visited)
      print_subtree(&network[i], 0);
  }
  printf("-----------------------\n");
}

void
tcpip_handler()
{
  uint8_t *appdata;
  uip_ipaddr_t src_ip, parent_ip;
  struct Node *id, *parent;
  uint16_t neighbors;
  uip_ipaddr_t *neighbor_ip;
  int i;

  if(!uip_newdata())
    return;

  appdata = (uint8_t *) uip_appdata;
  MAPPER_GET_PACKETDATA(src_ip, appdata);

  PRINTF("Source IP: ");
  PRINT6ADDR(&src_ip);
  PRINTF("\n");

  make_ipaddr_global(&src_ip);
  id = add_node(&src_ip);
  if(id == NULL)
    return;

  PRINTF("Found node ");
  PRINT6ADDR(id->id);
  PRINTF("\n");

  // TODO make sure instance ID and DAG ID matches

  // RPL Instance ID | DODAG ID | DAG Version | Timestamp
  appdata += sizeof(uint8_t) + sizeof(uip_ipaddr_t);        // RPL Instance ID | DAG ID
  appdata += sizeof(uint8_t)*2;

  // Rank
  MAPPER_GET_PACKETDATA(id->rank, appdata);

  // Parent
  MAPPER_GET_PACKETDATA(parent_ip, appdata);
  make_ipaddr_global(&parent_ip);
  parent = add_node(&parent_ip);
  if(parent == NULL)
    return;

  PRINTF("Found parent ");
  PRINT6ADDR(parent->id);
  PRINTF("\n");

  id->parent = parent;

  // Get the number of neighbors
  MAPPER_GET_PACKETDATA(neighbors, appdata);

  // Scan all neighbors
  for(i = 0; i < neighbors && i < NETWORK_DENSITY; ++i) {
    neighbor_ip = (uip_ipaddr_t *) appdata;
    appdata += sizeof(uip_ipaddr_t);

    make_ipaddr_global(neighbor_ip);

    id->neighbor[i].node = add_node(neighbor_ip);

    MAPPER_GET_PACKETDATA(id->neighbor[i].rank, appdata);

    if(uip_ipaddr_cmp(parent->id, neighbor_ip))
      id->parent_id = i;
  }
  id->neighbors = neighbors;
}

/**
 * Send out an information request to all nodes in the network, one at the
 * time.
 *
 * This function is stateful and in order to map the entire network this needs
 * to be run several times in order to request information from all nodes in
 * the network.
 */
void
map_network()
{
  for(;
      working_host < UIP_DS6_ROUTE_NB
      && !uip_ds6_routing_table[working_host].isused; ++working_host);

  if(!uip_ds6_routing_table[working_host].isused) {
    ++working_host;
    return;
  }
  static char data[sizeof(current_rpl_instance_id) +
    sizeof(current_dag->dag_id) + sizeof(current_dag->version) +
    sizeof(timestamp)];
  void *data_p = data;

  MAPPER_ADD_PACKETDATA(data_p, current_rpl_instance_id);
  MAPPER_ADD_PACKETDATA(data_p, current_dag->dag_id);
  MAPPER_ADD_PACKETDATA(data_p, current_dag->version);
  MAPPER_ADD_PACKETDATA(data_p, timestamp);

  add_node(&uip_ds6_routing_table[working_host].ipaddr);

  PRINTF("sending data to: %2d ", working_host);
  PRINT6ADDR(&uip_ds6_routing_table[working_host].ipaddr);
  PRINTF("\n");
  uip_udp_packet_sendto(ids_conn, data, sizeof(data),
                        &uip_ds6_routing_table[working_host++].ipaddr,
                        UIP_HTONS(MAPPER_CLIENT_PORT));

  if(working_host >= UIP_DS6_ROUTE_NB) {
    working_host = 0;
    etimer_reset(&map_timer);
  }
}

/**
 * Check that all information provided by nodes correspond with the information
 * provided by their parent
 */
int
check_child_parent_relation()
{
  int status = 1;
  int i;

  for(i = 0; i < node_index; ++i) {
    // Compare the parents rank to the one
    if(network[i].rank < network[i].neighbor[network[i].parent_id].rank) {
      printf("ATTACK ATTACK ATTACK: SOMEONE IS MESSING ABOUT!!!\n");
      printf("Child: ");
      uip_debug_ipaddr_print(network[i].id);
      printf(" parent: ");
      uip_debug_ipaddr_print(network[i].neighbor[network[i].parent_id].node->
                             id);
      printf("\nATTACK ATTACK ATTACK: SOMEONE IS MESSING ABOUT!!!\n");
      status = 0;
    }
  }
  return status;
}

/**
 * Visit all nodes in the given subtree
 *
 * This will only look at nodes which is indirectly a child to the given node.
 * All neighbors of a node is visited who also have that node as their parent.
 */
void
visit_tree(struct Node *node)
{
  int i;

  if(node->visited) {
    return;
  }
  node->visited = 1;

  for(i = 0; i < node->neighbors; ++i) {
    if(uip_ipaddr_cmp(node->neighbor[i].node->parent->id, node->id))
      visit_tree(node->neighbor[i].node);
  }
}

/**
 * This will find any missing nodes
 *
 * If some node has as of yet not sent information about its neighbors we
 * consider it a fault and alert the user
 *
 * @return True if some node have missing data.
 */
int
missing_ids_info()
{
  int i;
  int status = 0;

  for(i = 0; i < node_index; ++i) {
    network[i].visited = 0;
  }

  // Traverse the tree starting at the root
  visit_tree(&network[0]);

  for(i = 0; i < node_index; ++i) {
    if(!network[i].visited) {
      printf("Node not found: ");
      uip_debug_ipaddr_print(network[i].id);
      printf("\n");
      status = 1;
    }
  }
  return status;
}

/**
 * Run the intrusion detection rules
 */
void
detect_inconsistencies()
{
  check_child_parent_relation();
  missing_ids_info();
  // TODO Check if any node is lying about its rank
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(mapper, ev, data)
{
  static struct etimer timer;
  int i, k;

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

  etimer_set(&timer, CLOCK_SECOND); // Wake up and send the next information request
  etimer_set(&map_timer, 20 * CLOCK_SECOND); // Restart network mapping

  // Add this node (root node) to the network graph
  network[0].id = &uip_ds6_get_global(ADDR_PREFERRED)->ipaddr;
  ++node_index;

  while(1) {
    PRINTF("snurrar\n");
    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    } else if(etimer_expired(&map_timer)) {

      // Map the next DAG.
      if(working_host == 0) {
        print_graph();
        detect_inconsistencies();

        // This will overflow, thats OK (and well-defined as it is unsigned)
        ++timestamp;

        for(; mapper_instance < RPL_MAX_INSTANCES; ++mapper_instance) {
          if(instance_table[mapper_instance].used == 0)
            continue;
          for(; mapper_dag < RPL_MAX_DAG_PER_INSTANCE; ++mapper_dag) {
            if(!instance_table[mapper_instance].dag_table[mapper_dag].used)
              continue;

            current_rpl_instance_id =
              instance_table[mapper_instance].instance_id;
            current_dag = &instance_table[mapper_instance].dag_table[mapper_dag];

            // Reset the roots neighbor list and ranks

            for(i = 0, k = 0; i < UIP_DS6_ROUTE_NB; ++i) {
              if(uip_ds6_routing_table[i].isused) {
                memcpy(&tmp_ip, &uip_ds6_routing_table[i].nexthop,
                       sizeof(tmp_ip));
                make_ipaddr_global(&tmp_ip);
                if(uip_ipaddr_cmp(&uip_ds6_routing_table[i].ipaddr, &tmp_ip)) {
                  network[0].neighbor[k].node =
                    add_node(&uip_ds6_routing_table[i].ipaddr);
                  network[0].neighbor[k].rank = 0;

                  k++;
                }
              }
            }
            network[0].neighbors = k;

            goto found_network;

          }
          if(mapper_dag >= RPL_MAX_DAG_PER_INSTANCE - 1)
            mapper_dag = 0;
        }
        if(mapper_instance >= RPL_MAX_INSTANCES - 1)
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
