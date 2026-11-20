/**
 * @file c3rx.h
 * @brief Fixed references to driver-owned Wi-Fi receive buffers.
 */
#ifndef UTHOME_C3RX_H
#define UTHOME_C3RX_H

#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "esp_netif.h"
#include "lwip/pbuf.h"
#include <stddef.h>

/** @brief Transfer driver-buffer ownership only when a packet is returned. */
struct pbuf * c3rx_alloc (esp_netif_t * p_netif,
                          void *        p_payload,
                          size_t        length,
                          void *        p_token);
#endif

#endif

/* End of file. */

