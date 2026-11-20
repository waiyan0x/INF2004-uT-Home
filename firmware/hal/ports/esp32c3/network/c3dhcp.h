/**
 * @file c3dhcp.h
 * @brief Fixed client and packet storage for the station DHCP implementation.
 */
#ifndef UTHOME_C3DHCP_H
#define UTHOME_C3DHCP_H

#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include <stdint.h>

void * c3dhcp_alloc (mem_size_t size);
void c3dhcp_free (void * p_memory);
struct pbuf * c3dhcp_packet (pbuf_layer layer, uint16_t length, pbuf_type type);
#endif

#endif

/* End of file. */

