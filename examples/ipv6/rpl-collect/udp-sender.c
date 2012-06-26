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
#include "contiki-net.h"
#include "net/uip.h"
#include "net/uip-ds6.h"
#include "net/uip-udp-packet.h"
#include "net/neighbor-info.h"
#include "net/rpl/rpl.h"
#include "dev/serial-line.h"

#include "ids-central.h"

#include <stdio.h>
#include <string.h>

#define DEBUG DEBUG_PRINT
#include "net/uip-debug.h"

static struct uip_udp_conn *mapper_conn;
static uip_ipaddr_t server_ipaddr;

/*---------------------------------------------------------------------------*/
PROCESS(udp_client_process, "UDP client process");
AUTOSTART_PROCESSES(&udp_client_process);
/*---------------------------------------------------------------------------*/
extern uip_ds6_route_t uip_ds6_routing_table[UIP_DS6_ROUTE_NB];

static void
tcpip_handler(void)
{
  static int i, j;
  PRINTF("tcpip_handler()\n");
  if(uip_newdata()) {
    // TODO Check that this is the right port (and perhaps proto?)
    uint8_t instance_id;
    uint8_t timestamp;
    uip_ipaddr_t dag_id;
    uint8_t version;
    PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
    PRINTF("\n");
    unsigned char * in_data = uip_appdata;
    MAPPER_GET_PACKETDATA(instance_id, in_data);
    MAPPER_GET_PACKETDATA(dag_id, in_data);
    MAPPER_GET_PACKETDATA(version, in_data);
    MAPPER_GET_PACKETDATA(timestamp, in_data);

    for (i = 0; i < RPL_MAX_INSTANCES; ++i) {
      if (instance_table[i].used && instance_table[i].instance_id == instance_id) {
        for (j = 0; j < RPL_MAX_DAG_PER_INSTANCE; ++j) {
          if (instance_table[i].dag_table[j].used &&
              uip_ipaddr_cmp(&instance_table[i].dag_table[j].dag_id, &dag_id))
          {
            if (instance_table[i].dag_table[j].version != version) {
              PRINTF("Wrong RPL DODAG Version Number\n");
              return;
            }

            printf("asdf :");
            uip_debug_ipaddr_print(&instance_table[i].dag_table[j].dag_id);
            printf("\n");
            printf("instance_id = %d\n", instance_id);

            // Node ID (our IP) | RPL Instance ID | DODAG ID |
            // DODAG Version Number | Timestamp | My Rank | Parent adress |
            // # Neighbors | Neighbor
            //
            // Neighbor = [ ip_addr_t | rpl_rank_t ]

            // calculate size of out_data
            int outdata_size = sizeof(instance_id) + sizeof(dag_id) +
              sizeof(version) + sizeof(uint8_t) + sizeof(rpl_rank_t) +
              sizeof(*instance_table[i].dag_table[j].preferred_parent);

            rpl_parent_t *p;
            for(p = list_head(instance_table[i].dag_table[j].parents);
                p != NULL; p = list_item_next(p))
              outdata_size += sizeof(uip_ipaddr_t) + sizeof(rpl_rank_t);

            unsigned char out_data[outdata_size];
            unsigned char * out_data_p = out_data;
            uip_ipaddr_t * myip;
            myip = &uip_ds6_get_link_local(ADDR_PREFERRED)->ipaddr;
            if (myip == NULL) // We have no interface to use
              return;
            // My IP adress
            MAPPER_ADD_PACKETDATA(out_data_p, *myip);

            // RPL Instance ID | DODAG ID | DODAG Version Number | Timestamp

            MAPPER_ADD_PACKETDATA(out_data_p, instance_id);
            MAPPER_ADD_PACKETDATA(out_data_p, dag_id);
            MAPPER_ADD_PACKETDATA(out_data_p, version);
            MAPPER_ADD_PACKETDATA(out_data_p, timestamp);

            // My rank
            MAPPER_ADD_PACKETDATA(out_data_p, instance_table[i].dag_table[j].rank);

            // preferred parent
            PRINTF("parent: ");
            PRINT6ADDR(&instance_table[i].dag_table[j].preferred_parent->addr);
            PRINTF("\n");
            MAPPER_ADD_PACKETDATA(out_data_p,
                instance_table[i].dag_table[j].preferred_parent->addr);

            // Get all potential parents (neighbors) and their ranks
            uint16_t * neighbors = (uint16_t *)out_data_p;
            *neighbors = 0;
            out_data_p += sizeof(neighbors);

            for(p = list_head(instance_table[i].dag_table[j].parents); p != NULL; p = list_item_next(p)) {
              ++(*neighbors);
              MAPPER_ADD_PACKETDATA(out_data_p, p->addr);

              MAPPER_ADD_PACKETDATA(out_data_p, p->rank);

              PRINT6ADDR(&p->addr);
              PRINTF(" got rank %d\n", p->rank);
            }
            PRINTF("%d neighbors\n", *neighbors);

            uip_udp_packet_sendto(mapper_conn, out_data, sizeof(out_data), &UIP_IP_BUF->srcipaddr, UIP_HTONS(MAPPER_SERVER_PORT));
            break;
          }
        }
        break;
      }
    }

    /* Ignore incoming data */
  }
}


/*---------------------------------------------------------------------------*/
static void
set_global_address(void)
{
  uip_ipaddr_t ipaddr;

  uip_ip6addr(&ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);

  /* set server address */
  uip_ip6addr(&server_ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 1);

}


static void handle_reply(void) {
  PRINTF("Got ping from ");
  PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
  PRINTF("\n");
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_client_process, ev, data)
{
  PROCESS_BEGIN();

  PROCESS_PAUSE();

  set_global_address();

  PRINTF("UDP client process started\n");

  mapper_conn = udp_new(NULL, UIP_HTONS(MAPPER_SERVER_PORT), NULL);
  udp_bind(mapper_conn, UIP_HTONS(MAPPER_CLIENT_PORT));

  PRINTF("Created a connection with the server ");
  PRINT6ADDR(&mapper_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n",
        UIP_HTONS(mapper_conn->lport), UIP_HTONS(mapper_conn->rport));

  icmp6_new(NULL);

  while(1) {
    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    }
    else if(ev == tcpip_icmp6_event && *(uint8_t *)data == ICMP6_ECHO_REQUEST) {
      handle_reply();
    }
    else {
      PRINTF("Something happened\n");
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
