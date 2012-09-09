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

#define DEBUG DEBUG_PRINT
#include "net/uip-debug.h"

#define INCONSISTENCY_THREASHOLD 2

#define IDS_TEMP_ERROR 0x01
#define IDS_RANK_ERROR 0x02
#define IDS_RELATIVE_ERROR 0x04

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
static uint8_t timestamp = MAPPING_RECENT_WINDOW;

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
 * This timer is used to sleep between sending requests to individual hosts
 */
static struct etimer host_timer;

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
   * IP adress of this node
   */
  uip_ipaddr_t *ip;

  /**
   * The compressed IP of the node works as its ID
   */
  uint16_t id;

  /**
   * Timestamp of last recieved information update
   */
  uint8_t timestamp;

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

  /**
   * The status of the current node, a set of bits which indicate the state
   * between analysis for different checks
   */
  uint8_t status;
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
 * Search for a node by ID
 *
 * @return Returns a pointer to the node or NULL if none is found
 */
struct Node *
find_node(uint16_t id)
{
  int i;

  for(i = 0; i < node_index; ++i) {
    if(id == network[i].id) {
      return &network[i];
    }
  }
  return NULL;
}

/**
 * Add a new node to the network graph based on the compressed IP
 *
 * If the node already exists in the network no new node will be added and a
 * pointer to that adress will be returned
 *
 * This function will go through the routing table and add a pointer to the IP
 * in the routing table, in order to save memory.
 *
 * The function will return NULL if either we ran out of memory or if we
 * couldnt find the IP-adress in our routing table.
 *
 * @return A pointer to the new node or NULL if an error occured.
 */
struct Node *
add_node(uint16_t id)
{
  struct Node *node = find_node(id);

  if(node != NULL)
    return node;

  if(node_index >= NETWORK_NODES) {     // Out of memory
    PRINTF("Out of memory\n");
    return NULL;
  }

  int i;

  // Find the IP in the routing table, we want our records to point to them in
  // order to have them somewhat persistent, and at the same time save memory
  // (by only pointing to RPLs IPs)
  for(i = 0; i < UIP_DS6_ROUTE_NB; ++i) {
    if(!uip_ds6_routing_table[i].isused)
      continue;
    if(compress_ipaddr_t(&uip_ds6_routing_table[i].ipaddr) == id) {
      network[node_index].ip = &uip_ds6_routing_table[i].ipaddr;
      network[node_index].id = compress_ipaddr_t(network[node_index].ip);

      PRINTF("Creating new node with IP: ");
      PRINT6ADDR(network[node_index].ip);
      PRINTF(" (%x)\n", id);
      return &network[node_index++];
    }
  }
  PRINTF("No entry in the routing table matching ID %x!\n", id);
  return NULL;
}

void
print_subtree(struct Node *node, int depth)
{
  int i;

  printf("%*s", depth * 2, "");

  uip_debug_ipaddr_print(node->ip);

  if(node->visited) {
    printf("\n");
    return;
  }
  node->visited = 1;

  printf(" (t: %d, p: %x, r: %d) ", node->timestamp, node->parent_id, node->rank);

  printf("    {");

  for(i = 0; i < node->neighbors; ++i) {
    uip_debug_ipaddr_print(node->neighbor[i].node->ip);
    printf(" (%d) ,", node->neighbor[i].rank);
  }
  printf("}\n");

  for(i = 0; i < node->neighbors; ++i) {
    if(node->neighbor[i].node->parent != NULL &&
            uip_ipaddr_cmp(node->neighbor[i].node->parent->ip, node->ip))
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
  printf("Network graph at timestamp %d:\n\n", timestamp);
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
  uint16_t src_id, parent_id, dag_id;
  uint8_t rpl_instance_id, version_recieved, timestamp_recieved;

  struct Node *id, *parent;
  uint16_t neighbors;
  uint16_t neighbor_id;
  int i;

  if(!uip_newdata())
    return;

  // Make sure it is the correct port
  if (UIP_HTONS(UIP_UDP_BUF->destport) != MAPPER_SERVER_PORT)
    return;

  appdata = (uint8_t *) uip_appdata;
  MAPPER_GET_PACKETDATA(src_id, appdata);

  PRINTF("Source ID: %x\n", src_id);

  id = add_node(src_id);
  if(id == NULL)
    return;

  PRINTF("Found node ");
  PRINT6ADDR(id->ip);
  PRINTF("\n");

  // RPL Instance ID | DODAG ID | DAG Version | Timestamp

  MAPPER_GET_PACKETDATA(rpl_instance_id, appdata);

  MAPPER_GET_PACKETDATA(dag_id, appdata);

  if (dag_id != compress_ipaddr_t(&current_dag->dag_id)) {
    PRINTF("Mapping information recieved which does not match our current");
    PRINTF("DODAG, information ignored (got %x while expecting %x)\n", dag_id,
        compress_ipaddr_t(&current_dag->dag_id));
    return;
  }

  MAPPER_GET_PACKETDATA(version_recieved, appdata);
  if (version_recieved != current_dag->version) {
    PRINTF("Non-matching DODAG Version Number for incoming mapping information\n");
    PRINTF("got %d while expecting %d\n", version_recieved, current_dag->version);
    return;
  }
  MAPPER_GET_PACKETDATA(timestamp_recieved, appdata);
  if (timestamp_recieved != timestamp) {
    PRINTF("Non-matching timestamp for incoming mapping information\n");
    PRINTF("got %d while expecting %d\n", timestamp_recieved, timestamp);
    return;
  }

  id->timestamp = timestamp_recieved;

  // Rank
  MAPPER_GET_PACKETDATA(id->rank, appdata);

  // Parent
  MAPPER_GET_PACKETDATA(parent_id, appdata);
  parent = add_node(parent_id);
  if(parent == NULL)
    return;

  PRINTF("Found parent ");
  PRINT6ADDR(parent->ip);
  PRINTF("\n");

  id->parent = parent;

  // Get the number of neighbors
  MAPPER_GET_PACKETDATA(neighbors, appdata);

  // Scan all neighbors
  for(i = 0; i < neighbors && i < NETWORK_DENSITY; ++i) {
    MAPPER_GET_PACKETDATA(neighbor_id, appdata);

    id->neighbor[i].node = add_node(neighbor_id);

    MAPPER_GET_PACKETDATA(id->neighbor[i].rank, appdata);

    if(parent->id == neighbor_id)
      id->parent_id = i;
  }
  id->neighbors = neighbors;
}

/**
 * Check a timestamp to se if it is to old or not
 *
 * This function takes into account over and underflows.
 */
int timestamp_outdated(uint8_t ts, uint8_t margin) {
  uint8_t diff;
  if (timestamp >= ts)
    diff = timestamp - ts;
  else
    return 1; // Timestamp in future

  if (diff > margin)
    return 1;
  else
    return 0;
}

/**
 * Check if a Node structure is valid and up to date
 */
int valid_node(struct Node * node) {
  return node->timestamp != 0 && !timestamp_outdated(node->timestamp,
      MAPPING_RECENT_WINDOW*2);
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
  struct Node * node = NULL;
  for(; working_host < UIP_DS6_ROUTE_NB; ++working_host) {
    if (!uip_ds6_routing_table[working_host].isused)
      continue;
    node = add_node(compress_ipaddr_t(&uip_ds6_routing_table[working_host].ipaddr));

    // If an error, just ignore the node
    if (node == NULL)
      continue;

    if (timestamp_outdated(node->timestamp, MAPPING_RECENT_WINDOW))
      break;
  }

  if (uip_ds6_routing_table[working_host].isused &&
      timestamp_outdated(node->timestamp, MAPPING_RECENT_WINDOW)) {
    // RPL Instance ID | DAG ID (compressed, uint16_t) | DAG Version | timestamp
    static char data[sizeof(current_rpl_instance_id) +
      sizeof(uint16_t) + sizeof(current_dag->version) +
      sizeof(timestamp)];
    void *data_p = data;
    uint16_t tmp;

    MAPPER_ADD_PACKETDATA(data_p, current_rpl_instance_id);
    tmp = compress_ipaddr_t(&current_dag->dag_id);
    MAPPER_ADD_PACKETDATA(data_p, tmp);
    MAPPER_ADD_PACKETDATA(data_p, current_dag->version);
    MAPPER_ADD_PACKETDATA(data_p, timestamp);

    PRINTF("sending data to: %2d ", working_host);
    PRINT6ADDR(&uip_ds6_routing_table[working_host].ipaddr);
    PRINTF("\n");
    uip_udp_packet_sendto(ids_conn, data, sizeof(data),
        &uip_ds6_routing_table[working_host].ipaddr,
        UIP_HTONS(MAPPER_CLIENT_PORT));
  }

  ++working_host;

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
  int status = 0;
  int i;

  for(i = 0; i < node_index; ++i) {
    if (!valid_node(&network[i]))
      continue;

    // // We use a 10% margin
    // if(network[i].rank + network[i].rank/10 <
        // network[i].neighbor[network[i].parent_id].rank +
        // rpl_get_instance(current_rpl_instance_id)->min_hoprankinc) {

    if(network[i].rank < network[i].neighbor[network[i].parent_id].rank +
        rpl_get_instance(current_rpl_instance_id)->min_hoprankinc) {
      network[i].status |= IDS_TEMP_ERROR;
      network[i].neighbor[network[i].parent_id].node->status |= IDS_TEMP_ERROR;

    }
  }
  for(i = 0; i < node_index; ++i) {
    if (!valid_node(&network[i]))
      continue;

    if((network[i].status & (IDS_TEMP_ERROR | IDS_RELATIVE_ERROR)) ==
        (IDS_TEMP_ERROR | IDS_RELATIVE_ERROR)) {
      if (status == 0)
        printf("The following nodes has advertised incorrect routes:\n");

      uip_debug_ipaddr_print(network[i].ip);
      printf(" (%d)\n", network[i].rank);
      status = 1;
    }
  }
  for(i = 0; i < node_index; ++i) {
    if (network[i].status & IDS_TEMP_ERROR) {
      // Promote the temporary error to a saved error
      network[i].status |= IDS_RELATIVE_ERROR;
      network[i].status &= ~IDS_TEMP_ERROR;
    }
    else {
      // Reset if we didnt see a repeat offence
      network[i].status &= ~IDS_RELATIVE_ERROR;
    }
  }
  return status;
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

  for (i = 0; i < node_index; ++i) {
    if (!valid_node(&network[i])) {
      if (status == 0)
        printf("The following list of nodes either have outdated or non-exsistent information: \n");
      uip_debug_ipaddr_print(network[i].ip);
      printf("\n");

      status = 1;
    }
  }

  return status;
}

int correct_rank_inconsistencies(void) {
  int i,j,k;
  int inconsistencies = 0;

  // We will use the visited status variable to count the number of
  // missbehaviours that have occured for a certain node
  for (i = 0; i < node_index; ++i)
    network[i].visited = 0;

  // We do not care about the roots neighboring ranks, if the node is lying
  // about its rank to the root it is off little use to check the validity of
  // it as its claimd rank will correspond to the rank it is reporting.

  for (i = 1; i < node_index; ++i) {
    // If we havent gotten any information for this node, ignore it
    if (!valid_node(&network[i]))
      continue;

    for (j = 0; j < network[i].neighbors; ++j) {
      // If we havent gotten any information from this neighbor, ignore it
      if (!valid_node(network[i].neighbor[j].node) ||
          network[i].neighbor[j].node == &network[0])
        continue;

      // We have an inconsistency

      int diff;
      if (network[i].neighbor[j].node->rank > network[i].neighbor[j].rank)
        diff = network[i].neighbor[j].node->rank - network[i].neighbor[j].rank;
      else
        diff = network[i].neighbor[j].rank - network[i].neighbor[j].node->rank ;

      // If the absolute difference is > 20% of the ranks averages.
      // (r1+r2)/2*0.2 => (r1+r2)/10
      if (diff > (network[i].neighbor[j].rank + network[i].neighbor[j].node->rank)/10) {
        PRINTF("Node %d is claiming node %d has rank %d, while it claims it has %d\n",
            (uint8_t) network[i].id, (uint8_t) network[i].neighbor[j].node->id,
            network[i].neighbor[j].rank, network[i].neighbor[j].node->rank);

        network[i].visited++;
        network[i].neighbor[j].node->visited++;
      }
    }
  }

  for (i = 0; i < node_index; ++i) {
    if (network[i].visited > INCONSISTENCY_THREASHOLD) {
      PRINTF("Rank inconsistency: ");
      PRINT6ADDR(network[i].ip);
      PRINTF("\n");
      inconsistencies = 1;

      // Update the rank of the lying node with the information from one of its
      // neighbors.

      struct Node * neighbor = NULL;
      for (k = 0; k < network[i].neighbors; ++k) {
        for (j = 0; j < network[i].neighbor[k].node->neighbors; ++j) {
          if (network[i].neighbor[k].node->neighbor[j].node->id == network[i].id) {
            neighbor = network[i].neighbor[k].node;
            network[i].rank = neighbor->neighbor[j].rank;
            break;
          }
        }
      }
      if (neighbor == NULL) {
        PRINTF("Could not correct ranks\n");
        continue;
      }

      PRINTF("Updating information with info from node %x\n", neighbor->id);

      // As we do not trust this node, overwrite the neighboring information
      // with the info from the nodes we do trust
      for (j = 0; j < network[i].neighbors; ++j) {
        if (network[i].neighbor[j].node->visited <= INCONSISTENCY_THREASHOLD)
          network[i].neighbor[j].rank = network[i].neighbor[j].node->rank;
      }

      PRINTF("New rank: %d\n", network[i].rank);
    }
  }
  return inconsistencies;
}

int detect_correct_rank_inconsistencies(void) {
  int status = 0;
  int i;
  status = correct_rank_inconsistencies();

  for (i = 0; i < node_index; ++i) {
    if (network[i].status & IDS_TEMP_ERROR) {
      // Promote the temporary error to a saved error
      network[i].status |= IDS_RANK_ERROR;
      network[i].status &= ~IDS_TEMP_ERROR;
    }
    else {
      // Reset if we didnt see a repeat offence
      network[i].status &= ~IDS_RANK_ERROR;
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
  detect_correct_rank_inconsistencies();
  check_child_parent_relation();
  missing_ids_info();
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(mapper, ev, data)
{
  int i, k;
  static int init = 1;

  PROCESS_BEGIN();

  PROCESS_PAUSE();

  memset(host, 0, sizeof(host));
  memset(network, 0, sizeof(network));

  PRINTF("IDS Server, compile time: %s\n", __TIME__);
  PRINTF("Mapping interval is %lu, hosts will be mapped with a %lu second delay\n", MAPPING_INTERVAL / CLOCK_SECOND, MAPPING_HOST_INTERVAL / CLOCK_SECOND);

  ids_conn = udp_new(NULL, UIP_HTONS(MAPPER_CLIENT_PORT), NULL);
  udp_bind(ids_conn, UIP_HTONS(MAPPER_SERVER_PORT));

  PRINTF("Created a server connection with remote address ");
  PRINT6ADDR(&ids_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n", UIP_HTONS(ids_conn->lport),
         UIP_HTONS(ids_conn->rport));

  etimer_set(&host_timer, MAPPING_HOST_INTERVAL); // Wake up and send the next information request
  etimer_set(&map_timer, MAPPING_INTERVAL); // Restart network mapping

  // Wait till we got an adress before starting the mapping
  while (uip_ds6_get_global(ADDR_PREFERRED) == NULL) {
      PROCESS_YIELD();
  }

  // Add this node (root node) to the network graph
  network[0].ip = &uip_ds6_get_global(ADDR_PREFERRED)->ipaddr;
  network[0].id = compress_ipaddr_t(network[0].ip);
  ++node_index;

  while(1) {
    PRINTF("snurrar\n");
    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    } else if(etimer_expired(&map_timer)) {

      // Map the next DAG.
      if(working_host == 0 && etimer_expired(&host_timer)) {
#if (DEBUG) & DEBUG_PRINT
        print_graph();
#endif
        if (init == 0)
          detect_inconsistencies();
        init = 0;

        // This will overflow, thats OK (and well-defined as it is unsigned)
        ++timestamp;

        for(; mapper_instance < RPL_MAX_INSTANCES; ++mapper_instance) {
          if(instance_table[mapper_instance].used == 0)
            continue;
          for(; mapper_dag < RPL_MAX_DAG_PER_INSTANCE; ++mapper_dag) {
            if(!instance_table[mapper_instance].dag_table[mapper_dag].used)
              continue;
            network[0].rank = instance_table[mapper_instance].min_hoprankinc;

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
                    add_node(compress_ipaddr_t(&uip_ds6_routing_table[i].ipaddr));
                  network[0].neighbor[k].rank = 0;

                  k++;
                }
              }
            }
            network[0].neighbors = k;
            network[0].timestamp = timestamp;

            goto found_network;

          }
          if(mapper_dag >= RPL_MAX_DAG_PER_INSTANCE - 1)
            mapper_dag = 0;
        }
        if(mapper_instance >= RPL_MAX_INSTANCES - 1)
          mapper_instance = 0;
      }

    found_network:
      if (etimer_expired(&host_timer)) {
        map_network();
        etimer_restart(&host_timer);
      }
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
