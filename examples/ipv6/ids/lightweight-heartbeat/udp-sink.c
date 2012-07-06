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

#include "net/netstack.h"
#include "dev/button-sensor.h"
#include "dev/serial-line.h"
#if CONTIKI_TARGET_Z1
#include "dev/uart0.h"
#else
#include "dev/uart1.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "collect-common.h"
#include "collect-view.h"

#define DEBUG DEBUG_NONE
#include "net/uip-debug.h"

#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_UDP_BUF          ((struct uip_udp_hdr *)&uip_buf[UIP_LLIPH_LEN])
#define UIP_ICMP_BUF            ((struct uip_icmp_hdr *)&uip_buf[uip_l2_l3_hdr_len])
#define PING6_DATALEN 16

#define PING_TIMEOUT 20 * CLOCK_SECOND

#define UDP_CLIENT_PORT 8775
#define UDP_SERVER_PORT 5688

#define CONTROL_CHAN_CLIENT_PORT 4712
#define CONTROL_CHAN_SERVER_PORT 4711

extern uip_ds6_route_t uip_ds6_routing_table[];

struct ids_host_info {
  uip_ipaddr_t addr;
  int8_t outstanding_echos;
};

static struct uip_udp_conn *server_conn;
static struct uip_udp_conn *control_conn;
static struct ids_host_info host[47];
static int hosts;
static uint8_t count = 0;
static int current_ping = 0;

PROCESS(udp_server_process, "UDP server process");
AUTOSTART_PROCESSES(&udp_server_process,&collect_common_process);
/*---------------------------------------------------------------------------*/
void
collect_common_set_sink(void)
{
}
/*---------------------------------------------------------------------------*/
void
collect_common_net_print(void)
{
  PRINTF("I am sink!\n");
}
/*---------------------------------------------------------------------------*/
void
collect_common_send(void)
{
  /* Server never sends */
}
/*---------------------------------------------------------------------------*/
void
collect_common_net_init(void)
{
#if CONTIKI_TARGET_Z1
  uart0_set_input(serial_line_input_byte);
#else
  uart1_set_input(serial_line_input_byte);
#endif
  serial_line_init();

  PRINTF("I the DETECTING sink! Compile time: %s\n", __TIME__);
}

void add_ip(uip_ipaddr_t * addr) {
  int i;
  for (i = 0; i < hosts; ++i) {
    if (uip_ipaddr_cmp(addr, &host[i].addr))
      return;
  }
  memcpy(&host[hosts++].addr, addr, sizeof(*addr));
  printf("Adding new IP: ");
  uip_debug_ipaddr_print(addr);
  printf("\n");
}

void del_ip(uip_ipaddr_t * addr) {
  int i, found;
  for (i = 0, found = 0; i < hosts; ++i) {
    if (uip_ipaddr_cmp(addr, &host[i].addr)) found = 1;
    if (found) {
      host[i] = host[i+1];
    }
  }
  if (!found)
    return;
  printf("Removed ip: ");
  uip_debug_ipaddr_print(addr);
  printf("\n");

  if (hosts > 0)
    --hosts;
}

/**
 * Find the ids info corresponding to the IP provided
 * @return The info if found, NULL otherwise
 */
struct ids_host_info * find_info(uip_ipaddr_t * addr) {
  int i;
  for (i = 0; i < hosts; ++i) {
    if (uip_ipaddr_cmp(addr, &host[i].addr)) {
      return &host[i];
    }
  }
  return NULL;
}

void send_ping(uip_ipaddr_t * dest_addr)
{
  struct ids_host_info * ids_info;
  UIP_IP_BUF->vtc = 0x60;
  UIP_IP_BUF->tcflow = 1;
  UIP_IP_BUF->flow = 0;
  UIP_IP_BUF->proto = UIP_PROTO_ICMP6;
  UIP_IP_BUF->ttl = uip_ds6_if.cur_hop_limit;
  uip_ipaddr_copy(&UIP_IP_BUF->destipaddr, dest_addr);
  uip_ds6_select_src(&UIP_IP_BUF->srcipaddr, &UIP_IP_BUF->destipaddr);

  UIP_ICMP_BUF->type = ICMP6_ECHO_REQUEST;
  UIP_ICMP_BUF->icode = 0;
  /* set identifier and sequence number to 0 */
  memset((uint8_t *)UIP_ICMP_BUF + UIP_ICMPH_LEN, 0, 4);
  /* put one byte of data */
  memset((uint8_t *)UIP_ICMP_BUF + UIP_ICMPH_LEN + UIP_ICMP6_ECHO_REQUEST_LEN,
      count, PING6_DATALEN);


  uip_len = UIP_ICMPH_LEN + UIP_ICMP6_ECHO_REQUEST_LEN + UIP_IPH_LEN + PING6_DATALEN;
  UIP_IP_BUF->len[0] = (uint8_t)((uip_len - 40) >> 8);
  UIP_IP_BUF->len[1] = (uint8_t)((uip_len - 40) & 0x00FF);

  UIP_ICMP_BUF->icmpchksum = 0;
  UIP_ICMP_BUF->icmpchksum = ~uip_icmp6chksum();

  UIP_STAT(++uip_stat.icmp.sent);

  ids_info = find_info(dest_addr);
  if (ids_info == NULL) {
    PRINTF("No request sent for this packet from ");
    PRINT6ADDR(dest_addr);
    PRINTF("\n");
  }
  else {
    ids_info->outstanding_echos++;
  }

  tcpip_ipv6_output();

  count++;
}

void ping_all() {
  for (; current_ping < UIP_DS6_ROUTE_NB && !uip_ds6_routing_table[current_ping].isused; ++current_ping);
  if (!uip_ds6_routing_table[current_ping].isused) {
    ++current_ping;
    return;
  }

  add_ip(&uip_ds6_routing_table[current_ping].ipaddr);
  send_ping(&uip_ds6_routing_table[current_ping].ipaddr);
  current_ping++;
  if (current_ping >= UIP_DS6_ROUTE_NB-1) {
    current_ping = 0;
  }
}

/*---------------------------------------------------------------------------*/
static void
tcpip_handler(void)
{
  uint8_t *appdata;
  rimeaddr_t sender;
  uint8_t seqno;
  uint8_t hops;
#if DEBUG & DEBUG_PRINT
  int i;
  int len;
  uip_ipaddr_t ip_recieved;
#endif

  if (uip_newdata()) {

    appdata = (uint8_t *)uip_appdata;
    sender.u8[0] = UIP_IP_BUF->srcipaddr.u8[15];
    sender.u8[1] = UIP_IP_BUF->srcipaddr.u8[14];
    seqno = *appdata;
    hops = uip_ds6_if.cur_hop_limit - UIP_IP_BUF->ttl + 1;

#if DEBUG & DEBUG_PRINT
    if (UIP_HTONS(UIP_UDP_BUF->destport) == CONTROL_CHAN_SERVER_PORT) {
      PRINTF("Got a message on the control channel!\n");
      // appdata += 2;
      len = uip_datalen();

      switch (appdata[0]) {
        case 'a': // add
        case 'r': // remove
        case 'p': // ping
          for (i = 1; i < len; ++i) {
            ip_recieved.u8[i-1] = appdata[i];
          } 
          break;
        case 'l': // list
          printf("Listing IP addresses (%d)\n", hosts);
          for (i = 0; i < hosts; ++i) {
            printf("%2d: ", i);
            uip_debug_ipaddr_print(&host[i].addr);
            printf(" %-9d", host[i].outstanding_echos);
            printf("\n");
          }
      }

      switch (appdata[0]) {
        case 'a':
          add_ip(&ip_recieved);
          break;
        case 'r':
          del_ip(&ip_recieved);
          break;
        case 'p':
          send_ping(&ip_recieved);
        break;
      }

      return;
    }
#endif

    collect_common_recv(&sender, seqno, hops,
                        appdata + 2, uip_datalen() - 2);

  }
}

void handle_reply(void) {
  struct ids_host_info * ids_info = find_info(&UIP_IP_BUF->srcipaddr);
  if (ids_info == NULL) {
    PRINTF("Sending to host that doesnt exist, this shouldnt happen\n");
    add_ip(&UIP_IP_BUF->srcipaddr);
  }
  else {
    ids_info->outstanding_echos--;
  }
}


/*---------------------------------------------------------------------------*/
static void
print_local_addresses(void)
{
  int i;
  uint8_t state;

  PRINTF("Server IPv6 addresses: ");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(state == ADDR_TENTATIVE || state == ADDR_PREFERRED) {
      PRINT6ADDR(&uip_ds6_if.addr_list[i].ipaddr);
      PRINTF("\n");
      /* hack to make address "final" */
      if (state == ADDR_TENTATIVE) {
        uip_ds6_if.addr_list[i].state = ADDR_PREFERRED;
      }
    }
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_server_process, ev, data)
{
  uip_ipaddr_t ipaddr;
  struct uip_ds6_addr *root_if;
  static struct etimer ping_timer;
  static int i;

  PROCESS_BEGIN();

  PROCESS_PAUSE();

  SENSORS_ACTIVATE(button_sensor);

  memset(host, 0, sizeof(host));

  PRINTF("UDP server started\n");

#if UIP_CONF_ROUTER
  uip_ip6addr(&ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 1);
  /* uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr); */
  uip_ds6_addr_add(&ipaddr, 0, ADDR_MANUAL);
  root_if = uip_ds6_addr_lookup(&ipaddr);
  if(root_if != NULL) {
    rpl_dag_t *dag;
    dag = rpl_set_root(RPL_DEFAULT_INSTANCE,(uip_ip6addr_t *)&ipaddr);
    uip_ip6addr(&ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
    rpl_set_prefix(dag, &ipaddr, 64);
    PRINTF("created a new RPL dag\n");
  } else {
    PRINTF("failed to create a new RPL DAG\n");
  }
#endif /* UIP_CONF_ROUTER */

  print_local_addresses();

  /* The data sink runs with a 100% duty cycle in order to ensure high
     packet reception rates. */
  NETSTACK_RDC.off(1);

  server_conn = udp_new(NULL, UIP_HTONS(UDP_CLIENT_PORT), NULL);
  udp_bind(server_conn, UIP_HTONS(UDP_SERVER_PORT));

  PRINTF("Created a server connection with remote address ");
  PRINT6ADDR(&server_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n", UIP_HTONS(server_conn->lport),
         UIP_HTONS(server_conn->rport));

  control_conn = udp_new(NULL, 0, NULL);
  udp_bind(control_conn, UIP_HTONS(CONTROL_CHAN_SERVER_PORT));

  icmp6_new(NULL);
  etimer_set(&ping_timer, PING_TIMEOUT);

  while(1) {
    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    } else if (ev == sensors_event && data == &button_sensor) {
      PRINTF("Initiaing global repair\n");
      rpl_repair_root(RPL_DEFAULT_INSTANCE);
    }
    else if(ev == tcpip_icmp6_event && *(uint8_t *)data == ICMP6_ECHO_REPLY) {
      handle_reply();
    }
    else if (etimer_expired(&ping_timer)){
      if (current_ping == 0) { // The timer just expired
        // Check to make sure all are online
        for (i = 0; i < hosts; ++i) {
          if (host[i].outstanding_echos != 0) {
            uip_debug_ipaddr_print(&host[i].addr);
            printf(" has not responded to ping\n");
            host[i].outstanding_echos = 0;
          }
        }
      }
      ping_all();
      if (current_ping == 0) { // We are done pinging all neighbors
        etimer_reset(&ping_timer);
      }
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
