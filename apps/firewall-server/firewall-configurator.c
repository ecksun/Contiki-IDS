#include <string.h>

#include "firewall-configurator.h"
#include "ids-common.h"
#include "net/firewall.h"

#define DEBUG DEBUG_PRINT
#include "net/uip-debug.h"

PROCESS(firewall_configurator, "Firewall Configurator");
AUTOSTART_PROCESSES(&firewall_configurator);

static struct uip_udp_conn * fw_conf_conn;

void tcpip_handler(void) {
  uint8_t *appdata;
  uint16_t dest;
  uip_ipaddr_t src;
  int i;
  int first_unused;
  if(!uip_newdata())
    return;

  if (UIP_HTONS(UIP_UDP_BUF->destport) != FW_CONF_SERVER_PORT)
    return;

  /*
   * The packet format is on the form:
   * Destination IP (IP of the sensor node) | Source IP (external IP)
   *
   * Destination IP are compressed with ipaddr_compress_t. It is not possible
   * to compress the source IP as that could be any host on the internet, that
   * is we need to be able to represent the entire IPv6 128 bit adress space.
   */
  appdata = (uint8_t *) uip_appdata;
  MAPPER_GET_PACKETDATA(dest, appdata);
  MAPPER_GET_PACKETDATA(src, appdata);

  // Make sure we arent being fooled
  // TODO Make this secure, that is authenticate the sender or the IP header
  if (compress_ipaddr_t(&UIP_IP_BUF->srcipaddr) != dest)
    return;

  PRINTF("Got a new filter request from node %x asking to filter packets from ", dest);
  PRINT6ADDR(&src);
  PRINTF("\n");

  // Check if there already is a global filter like this
  for (i = 0; i < GLOBAL_FILTERS; ++i) {
    // In case we have already registered this filter as a global filter
    if (uip_ipaddr_cmp(&filters_global[i], &src)) {
      PRINTF("Already a registered global filter\n");
      return;
    }
  }

  // Check if there is a local filter like this
  first_unused = -1;
  for (i = 0; i < SMALL_FILTERS; ++i) {
    if (filters_small[i].state == FILTER_UNUSED) {
      if (first_unused== -1)
        first_unused= i;
      continue;
    }
    // We already have a similar filter
    if (uip_ipaddr_cmp(&filters_small[i].src, &src)) {
      // This node has already reported this source before
      if (filters_small[i].dest == dest) {
        PRINTF("This filter has already been reported\n");
        return;
      }

      PRINTF("Promoting to a global filter\n");
      // Promote the filter to a global filter
      memcpy(&filters_global[global_index], &src, sizeof(uip_ipaddr_t));
      global_index = (global_index + 1) % GLOBAL_FILTERS;
      filters_small[i].state = FILTER_UNUSED;
      return;
    }
  }

  // This filter is new

  // Out of space, remove oldest in a round-robin fashion
  if (first_unused == -1) {
    memcpy(&filters_small[small_index], &src, sizeof(uip_ipaddr_t));
    filters_small[small_index].dest = dest;
    filters_small[small_index].state = FILTER_USED;
    small_index = (small_index + 1) % SMALL_FILTERS;
  }
  // An unused spot, use it!
  else {
    memcpy(&filters_small[first_unused], &src, sizeof(uip_ipaddr_t));
    filters_small[first_unused].dest = dest;
    filters_small[first_unused].state = FILTER_USED;
  }
}

PROCESS_THREAD(firewall_configurator, ev, data)
{
  PROCESS_BEGIN();

  PROCESS_PAUSE();

  fw_conf_conn = udp_new(NULL, UIP_HTONS(FW_CONF_CLIENT_PORT), NULL);
  udp_bind(fw_conf_conn, UIP_HTONS(FW_CONF_SERVER_PORT));

  PRINTF("Starting firewall process\n");

  while(1) {
    PROCESS_YIELD();
    if(ev == tcpip_event) {
      tcpip_handler();
    } 
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/

