#include "ids-client.h"
#include "ids-common.h"

#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/uip.h"
#include "net/rpl/rpl.h"

#include <string.h>

#define DEBUG DEBUG_NONE
#include "net/uip-debug.h"

static struct uip_udp_conn *mapper_conn;
extern uip_ds6_route_t uip_ds6_routing_table[UIP_DS6_ROUTE_NB];
extern rpl_instance_t instance_table[];

PROCESS(mapper_client, "IDS network mapper client");
AUTOSTART_PROCESSES(&mapper_client);

static void
tcpip_handler(void)
{
  static int i, j;
  uint16_t tmp_id;
  PRINTF("tcpip_handler()\n");
  if(uip_newdata()) {
    // TODO Check that this is the right port (and perhaps proto?)
    uint8_t instance_id;
    uint8_t timestamp;
    uint16_t dag_id;
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
              compress_ipaddr_t(&instance_table[i].dag_table[j].dag_id) ==
              dag_id) {
            if (instance_table[i].dag_table[j].version != version) {
              PRINTF("Wrong RPL DODAG Version Number\n");
              return;
            }
            // All IPs are compressed to fit in a uint16_t (compress_ipaddr_t)
            // rpl_rank_t is a uint16_t
            //
            // My IP (uint16_t) | IID (uint8_t) | DAG ID (ipaddr_t) |
            // Dag Ver.  (uint8_t) | Timestamp (uint8_t) | Rank (uint16_t) |
            // Parent IP (uint16_t) | #neighbors (uint16_t) | NEIGHBORS
            //
            // NEIGHBORS = Neighbor ID (uint16_t) | Neighbor rank (uint16_t)

            // calculate size of out_data
            int outdata_size =
              sizeof(uint16_t) + sizeof(instance_id) + sizeof(dag_id) + sizeof(version) +
              sizeof(version) + sizeof(timestamp) + sizeof(rpl_rank_t) +
              sizeof(uint16_t) + sizeof(uint16_t);

            rpl_parent_t *p;
            for(p = list_head(instance_table[i].dag_table[j].parents);
                p != NULL; p = list_item_next(p)) {
              if (p->rank == -1)
                continue;
              outdata_size += sizeof(uint16_t) + sizeof(rpl_rank_t);
            }

            unsigned char out_data[outdata_size];
            unsigned char * out_data_p = out_data;
            uip_ipaddr_t * myip;
            myip = &uip_ds6_get_link_local(ADDR_PREFERRED)->ipaddr;
            if (myip == NULL) // We have no interface to use
              return;
            // My IP adress
            tmp_id = compress_ipaddr_t(myip);
            MAPPER_ADD_PACKETDATA(out_data_p, tmp_id);

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

            tmp_id = compress_ipaddr_t(&instance_table[i].dag_table[j].preferred_parent->addr);
            MAPPER_ADD_PACKETDATA(out_data_p, tmp_id);

            // Get all potential parents (neighbors) and their ranks
            uint16_t * neighbors = (uint16_t *)out_data_p;
            *neighbors = 0;
            out_data_p += sizeof(neighbors);

            for(p = list_head(instance_table[i].dag_table[j].parents); p !=
                NULL; p = list_item_next(p)) {
              if (p->rank == -1)
                continue;
              ++(*neighbors);
              tmp_id = compress_ipaddr_t(&p->addr);
              MAPPER_ADD_PACKETDATA(out_data_p, tmp_id);

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

PROCESS_THREAD(mapper_client, ev, data)
{
  PROCESS_BEGIN();

  PROCESS_PAUSE();

  PRINTF("Mapper-client started\n");

  mapper_conn = udp_new(NULL, UIP_HTONS(MAPPER_SERVER_PORT), NULL);
  udp_bind(mapper_conn, UIP_HTONS(MAPPER_CLIENT_PORT));

  PRINTF("Created a connection with the server ");
  PRINT6ADDR(&mapper_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n", UIP_HTONS(mapper_conn->lport),
      UIP_HTONS(mapper_conn->rport));

  while(1) {
    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    }
  }

  PROCESS_END();
}
