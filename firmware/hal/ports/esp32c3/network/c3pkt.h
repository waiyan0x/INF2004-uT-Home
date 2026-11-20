/**
 * @file c3pkt.h
 * @brief Fixed outgoing packet storage for the single TCP broker.
 */
#ifndef UTHOME_C3PKT_H
#define UTHOME_C3PKT_H

#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "lwip/tcp.h"
#include <stdint.h>

/** @brief Allocate a copied TCP transport packet or IP control packet. */
struct pbuf * c3pkt_alloc (pbuf_layer layer, uint16_t length, pbuf_type type);

/** @brief Let the core split its unsent head using the reserved capacity. */
err_t c3pkt_split (struct tcp_pcb * p_connection, uint16_t length);
#endif

#endif

/* End of file. */

