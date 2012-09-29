#include "firewall-client.h"
#include "ids-common.h"

#include "contiki-net.h"
#include "net/rpl/rpl.h"

#define DEBUG DEBUG_PRINT
#include "net/uip-debug.h"

#include <string.h>
#include <stdio.h>

static struct uip_udp_conn *fw_conn;

void report_host(uip_ipaddr_t * host) {
  uip_ipaddr_t * myip;
  uint16_t myid;

  PRINTF("Abusive host: ");
  PRINT6ADDR(host);
  PRINTF("\n");

  /*
   * The packet format is on the form:
   * Destination IP (IP of the sensor node) | Source IP (external IP)
   *
   * Destination IP are compressed with ipaddr_compress_t. It is not possible
   * to compress the source IP as that could be any host on the internet, that
   * is we need to be able to represent the entire IPv6 128 bit adress space.
   */
  int outdata_size = sizeof(uint16_t) + sizeof(uip_ipaddr_t);

  unsigned char out_data[outdata_size];
  unsigned char * out_data_p = out_data;

  myip = &uip_ds6_get_link_local(ADDR_PREFERRED)->ipaddr;
  if (myip == NULL) // We have no interface to use
    return;
  myid = compress_ipaddr_t(myip);

  MAPPER_ADD_PACKETDATA(out_data_p, myid);

  MAPPER_ADD_PACKETDATA(out_data_p, *host);

  printf("sending to : ");
  uip_debug_ipaddr_print(&rpl_get_any_dag()->dag_id);
  printf("\n");

  uip_udp_packet_sendto(fw_conn, out_data, sizeof(out_data),
      &rpl_get_any_dag()->dag_id, UIP_HTONS(MAPPER_SERVER_PORT)); 
}

void firewall_init(void) {
  PRINTF("Initializing firewall\n");
  fw_conn = udp_new(NULL, UIP_HTONS(FW_CONF_SERVER_PORT), NULL);
  udp_bind(fw_conn, UIP_HTONS(FW_CONF_CLIENT_PORT));
}

