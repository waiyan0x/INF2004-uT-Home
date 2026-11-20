/**
 * @file c3rx.c
 * @brief Retain each Wi-Fi receive buffer until its last lwIP reference.
 */
#include "c3rx.h"
#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "c3bport.h"
#include "esp_netif_net_stack.h"
#include "sdkconfig.h"
#include <stdint.h>

#if defined(CONFIG_LWIP_L2_TO_L3_COPY) || !LWIP_SUPPORT_CUSTOM_PBUF ||         \
    (CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM < 1)
#error "Wi-Fi RX requires zero-copy frames and a bounded buffer count."
#endif

typedef struct
{
    struct pbuf_custom header;
    esp_netif_t *      p_netif;
    void *             p_token;
} c3rx_frame_t;

static c3rx_frame_t g_frames[CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM] = {0};

static void release (struct pbuf * p_packet);

/** @brief Bind a received frame in place. The caller returns it on refusal. */
struct pbuf * c3rx_alloc (esp_netif_t * p_netif,
                          void *        p_payload,
                          size_t        length,
                          void *        p_token)
{
    if ((NULL == p_netif) || (NULL == p_payload) || (UINT16_MAX < length))
    {
        return NULL;
    }
    /* lwIP uses a 16-bit length for both the packet and its backing buffer. */
    const uint16_t bytes    = (uint16_t)length;
    struct pbuf *  p_packet = NULL;
    const uint32_t saved    = c3bport_enter();
    for (size_t index = 0U; index < CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM;
         ++index)
    {
        c3rx_frame_t * const p_frame = &g_frames[index];
        if (NULL == p_frame->p_netif)
        {
            p_packet = pbuf_alloced_custom(
                PBUF_RAW, bytes, PBUF_REF, &p_frame->header, p_payload, bytes);
            if (NULL != p_packet)
            {
                p_frame->p_netif                     = p_netif;
                p_frame->p_token                     = p_token;
                p_frame->header.custom_free_function = release;
            }
            break;
        }
    }
    c3bport_leave(saved);
    return p_packet;
}

/** @brief Return the original driver token after releasing the slot lock. */
static void release (struct pbuf * p_packet)
{
    /* Each enclosing structure begins with the preceding SDK type. */
    struct pbuf_custom * const p_header = (struct pbuf_custom *)p_packet;
    c3rx_frame_t * const       p_frame  = (c3rx_frame_t *)p_header;
    const uint32_t             saved    = c3bport_enter();
    esp_netif_t * const        p_netif  = p_frame->p_netif;
    void * const               p_token  = p_frame->p_token;
    p_frame->p_netif                    = NULL;
    p_frame->p_token                    = NULL;
    p_frame->header.pbuf.payload        = NULL;
    c3bport_leave(saved);
    esp_netif_free_rx_buffer(p_netif, p_token);
}

#endif

/* End of file. */

