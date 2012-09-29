#ifndef __FIREWALL_CLIENT_H__
#define __FIREWALL_CLIENT_H__

#include "net/uip.h"

/**
 * Report an abusive host to the borde router.
 */
void report_host(uip_ipaddr_t *);
void firewall_init(void);

#endif

