/**
 * @file c3pkt.c
 * @brief Own copied TCP packets until lwIP releases their last reference.
 */
#include "c3pkt.h"
#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "c3bport.h"
#include "lwip/priv/tcp_priv.h"
#include <stdbool.h>
#include <stddef.h>

#if !LWIP_SUPPORT_CUSTOM_PBUF || !LWIP_NETIF_TX_SINGLE_PBUF ||                 \
    (MEM_ALIGNMENT != 4)
#error                                                                         \
    "Fixed TCP packets require the reviewed custom, single-buffer Wi-Fi port."
#endif

#define C3PKT_DATA_SLOTS    5U
#define C3PKT_CONTROL_SLOTS 2U
#define C3PKT_CONTROL_BYTES (TCP_HLEN + TCP_MAX_OPTION_BYTES + 1U)

typedef struct
{
    struct pbuf_custom header;
    uint32_t words[LWIP_MEM_ALIGN_SIZE(LWIP_MEM_ALIGN_SIZE(PBUF_TRANSPORT) +
                                       TCP_MSS) /
                   sizeof(uint32_t)];
} c3pkt_data_t;

typedef struct
{
    struct pbuf_custom header;
    uint32_t           words[LWIP_MEM_ALIGN_SIZE(LWIP_MEM_ALIGN_SIZE(PBUF_IP) +
                                       C3PKT_CONTROL_BYTES) /
                   sizeof(uint32_t)];
} c3pkt_control_t;

static c3pkt_data_t    g_data[C3PKT_DATA_SLOTS]       = {0};
static c3pkt_control_t g_control[C3PKT_CONTROL_SLOTS] = {0};
/* Only lwIP's serialized TCP core enters the split or allocates a packet. */
static bool gb_splitting = false;

static void release (struct pbuf * p_packet);

/** @brief Leave one data slot for a split and separate capacity for ACKs. */
struct pbuf * c3pkt_alloc (pbuf_layer layer, uint16_t length, pbuf_type type)
{
    struct pbuf_custom * p_header  = NULL;
    void *               p_storage = NULL;
    uint16_t             size      = 0U;

    if (PBUF_RAM != type)
    {
        return NULL;
    }
    const uint32_t saved = c3bport_enter();
    if ((PBUF_TRANSPORT == layer) && (TCP_MSS >= length))
    {
        size_t free_count = 0U;
        for (size_t index = 0U; index < C3PKT_DATA_SLOTS; ++index)
        {
            if (NULL == g_data[index].header.custom_free_function)
            {
                ++free_count;
                if (gb_splitting || (free_count > 1U))
                {
                    p_header  = &g_data[index].header;
                    p_storage = g_data[index].words;
                    size      = sizeof(g_data[index].words);
                    break;
                }
            }
        }
    }
    else if ((PBUF_IP == layer) && (C3PKT_CONTROL_BYTES >= length))
    {
        for (size_t index = 0U; index < C3PKT_CONTROL_SLOTS; ++index)
        {
            if (NULL == g_control[index].header.custom_free_function)
            {
                p_header  = &g_control[index].header;
                p_storage = g_control[index].words;
                size      = sizeof(g_control[index].words);
                break;
            }
        }
    }
    else
    {
        /* This TCP port emits no other packet shapes. */
    }
    struct pbuf * p_packet = NULL;
    if (NULL != p_header)
    {
        p_packet =
            pbuf_alloced_custom(layer, length, type, p_header, p_storage, size);
        if (NULL != p_packet)
        {
            p_header->custom_free_function = release;
        }
    }
    c3bport_leave(saved);
    return p_packet;
}

/** @brief Permit a fifth transport packet only inside the native TCP split. */
err_t c3pkt_split (struct tcp_pcb * p_connection, uint16_t length)
{
    gb_splitting       = true;
    const err_t status = tcp_split_unsent_seg(p_connection, length);
    gb_splitting       = false;
    return status;
}

/** @brief A custom header begins with the exact pbuf supplied by lwIP. */
static void release (struct pbuf * p_packet)
{
    struct pbuf_custom * const p_header = (struct pbuf_custom *)p_packet;
    const uint32_t             saved    = c3bport_enter();
    p_header->custom_free_function      = NULL;
    c3bport_leave(saved);
}

#endif

/* End of file. */

