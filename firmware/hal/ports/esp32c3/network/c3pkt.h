/**
 * @file c3pkt.h
 * @brief Fixed outgoing TCP and ARP packet storage for the Wi-Fi station.
 */
#ifndef UTHOME_C3PKT_H
#define UTHOME_C3PKT_H

#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "lwip/tcp.h"
#include <stdint.h>

/** @brief Lease a copied TCP packet or an Ethernet ARP frame. */
struct pbuf * c3pkt_alloc (pbuf_layer layer, uint16_t length, pbuf_type type);

/** @brief Lease the station frame for one IPv4 reply, fragment or link copy. */
struct pbuf * c3pkt_frame (pbuf_layer layer, uint16_t length, pbuf_type type);

/** @brief Lease an independent UDP header or Ethernet frame copy. */
struct pbuf * c3pkt_aux (pbuf_layer layer, uint16_t length, pbuf_type type);

/** @brief Make an owned, contiguous copy for a pending ARP entry. */
struct pbuf *
c3pkt_clone (pbuf_layer layer, pbuf_type type, struct pbuf * p_source);

/** @brief Let the core split its unsent head using the reserved capacity. */
err_t c3pkt_split (struct tcp_pcb * p_connection, uint16_t length);
#endif

#endif

/* End of file. */

