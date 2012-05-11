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
  printf("tcpip_handler()\n");
  if(uip_newdata()) {
    // TODO Check that this is the right port (and perhaps proto?)
    uint8_t instance_id;
    uip_ipaddr_t dag_id;
    uip_debug_ipaddr_print(&UIP_IP_BUF->srcipaddr);
    printf("\n");
    unsigned char * in_data = uip_appdata;
    memcpy(&instance_id, in_data, sizeof(instance_id));
    in_data += sizeof(instance_id);
    memcpy(&dag_id, in_data, sizeof(dag_id));

    for (i = 0; i < RPL_MAX_INSTANCES; ++i) {
      if (instance_table[i].used && instance_table[i].instance_id == instance_id) {
        for (j = 0; j < RPL_MAX_DAG_PER_INSTANCE; ++j) {
          if (instance_table[i].dag_table[j].used && uip_ipaddr_cmp(&instance_table[i].dag_table[j].dag_id, &dag_id)) {
            // uip_debug_ipaddr_print(&instance_table[i].dag_table[j].dag_id);
            // printf("\n");

            // My IP address | RPL Instance ID | DAG ID | Parent adress
            unsigned char out_data[sizeof(instance_id) + sizeof(dag_id) + sizeof(*instance_table[i].dag_table[j].preferred_parent)];
            unsigned char * out_data_p = out_data;
            uip_ipaddr_t * myip;
            myip = &uip_ds6_get_link_local(ADDR_PREFERRED)->ipaddr;
            if (myip == NULL) // We have no interface to use
              return;
            // My IP adress
            memcpy(out_data_p, myip, sizeof(uip_ipaddr_t));
            out_data_p += sizeof(uip_ipaddr_t);
            // RPL Instance ID | DAG ID
            memcpy(out_data_p, in_data, sizeof(instance_id) + sizeof(dag_id));
            out_data_p += sizeof(instance_id) + sizeof(dag_id);
            // preferred parent
            printf("parent: ");
            uip_debug_ipaddr_print(&instance_table[i].dag_table[j].preferred_parent->addr);
            printf("\n");
            memcpy(out_data_p,
                &instance_table[i].dag_table[j].preferred_parent->addr,
                sizeof(instance_table[i].dag_table[j].preferred_parent->addr));
            out_data_p += sizeof(instance_table[i].dag_table[j].preferred_parent->addr);

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
  printf("Got ping from ");
  uip_debug_ipaddr_print(&UIP_IP_BUF->srcipaddr);
  printf("\n");
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
      printf("Something happened\n");
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
