#ifndef __IDS_CLIENT_H__
#define __IDS_CLIENT_H__

#include "net/uip.h"

/**
 * Indicate to the IDS that a packet was lost while trying to transmitt to the
 * specificed destination
 */
void packet_lost(uip_ipaddr_t * dest);

#endif

