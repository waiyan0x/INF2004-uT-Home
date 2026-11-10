/**
 * @file c3mqtt.c
 * @brief Drive one ESP-TLS client and copied per-source MQTT telemetry.
 */
#include "c3mqtt.h"
#if UTHOME_NETWORK_WIFI
#include "netbroker.h"
#include "esp_tls.h"
#include "esp_random.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <stdio.h>
#include <string.h>

static netbroker_t       g_broker            = {0};
static c3mqtt_config_t   g_config            = {0};
static esp_tls_cfg_t     g_tls_config        = {0};
static esp_tls_t *       gp_tls              = NULL;
static sampcache_entry_t g_sample            = {0};
static char              g_node[32]          = {0};
static char              g_availability[80]  = {0};
static uint64_t          g_next_ms           = 0U;
static uint16_t          g_source            = 0U;
static bool              gb_attempted        = false;
static char              g_request_topic[80] = {0};
static char              g_result_topic[80]  = {0};
static bool              gb_result           = false;
static bool server_name (const char * p_name);
static int connect_step (void * p_context);
static int read_step (void * p_context, uint8_t * p_data, size_t size);
static int write_step (void * p_context, const uint8_t * p_data, size_t size);
static void close_step (void * p_context);

/** @brief Initialize only once, with explicit certificate and broker identity.
 */
int c3mqtt_start (const c3mqtt_config_t * p_config, const char * p_node)
{
    struct in_addr address = {0};
    int            length  = 0;
    if ((gb_attempted) || (NULL == p_config) || (NULL == p_node) ||
        (NULL == p_config->p_address) || (NULL == p_config->p_server_name) ||
        (!server_name(p_config->p_server_name)) || (NULL == p_config->p_ca) ||
        (2U > p_config->ca_size) || (8192U < p_config->ca_size) ||
        ('\0' != p_config->p_ca[p_config->ca_size - 1U]) ||
        (0U == p_config->port) ||
        (1 != inet_pton(AF_INET, p_config->p_address, &address)) ||
        (NULL == memchr(p_node, '\0', sizeof(g_node))))
    {
        return -1;
    }
    gb_attempted = true;
    g_config     = *p_config;
    memcpy(g_node, p_node, strlen(p_node) + 1U);
    length = snprintf(g_availability,
                      sizeof(g_availability),
                      "uthome/v1/%s/availability",
                      g_node);
    if ((0 > length) || ((size_t)length >= sizeof(g_availability)))
    {
        return -1;
    }
    g_tls_config =
        (esp_tls_cfg_t){.cacert_buf   = p_config->p_ca,
                        .cacert_bytes = (unsigned int)p_config->ca_size,
                        .common_name  = p_config->p_server_name,
                        .non_block    = true,
                        .timeout_ms   = 1};
    const netbroker_config_t config = {.p_connect      = connect_step,
                                       .p_read         = read_step,
                                       .p_write        = write_step,
                                       .p_close        = close_step,
                                       .p_client       = g_node,
                                       .p_user         = p_config->p_user,
                                       .p_password     = p_config->p_password,
                                       .p_availability = g_availability};
    return netbroker_init(&g_broker, &config);
}

/** @brief One broker I/O unit, and at most one copied sensor each second. */
int c3mqtt_step (uint64_t               now_ms,
                 bool                   b_ip,
                 const netread_info_t * p_info,
                 netread_capture_t *    p_capture,
                 void *                 p_context)
{
    uint8_t  frame[UTHOME_BLE_FRAME] = {0};
    char     topic[128]              = {0};
    uint64_t captured_ms             = 0U;
    int      length                  = 0;
    int      result                  = 0;
    if ((!gb_attempted) || (NULL == p_info) || (NULL == p_capture))
    {
        return -1;
    }
    result = netbroker_step(
        &g_broker, now_ms, b_ip, p_info->meta.generation, esp_random() % 251U);
    if (NETBROKER_READY != g_broker.state)
    {
        gb_result = false;
    }
    if (gb_result)
    {
        if ((!g_broker.mailbox.b_pending) && (0U == g_broker.size))
        {
            gb_result = false;
        }
        else
        {
            return result;
        }
    }
    if ((0 != result) || (NETBROKER_READY != g_broker.state) ||
        (now_ms < g_next_ms))
    {
        return result;
    }
    g_next_ms             = now_ms + 1000U;
    const uint16_t source = g_source;
    g_source = (uint16_t)((g_source + 1U) % UTHOME_SAMPCACHE_SOURCES);
    if ((0 != p_capture(p_context, source, &g_sample, &captured_ms)) ||
        (source != g_sample.source.source_index) ||
        (0 != netble_encode(
                  &g_sample, &p_info->meta, captured_ms, frame, sizeof(frame))))
    {
        return result;
    }
    length = snprintf(topic,
                      sizeof(topic),
                      "uthome/v1/%s/telemetry/%s",
                      g_node,
                      g_sample.source.name);
    if ((0 > length) || ((size_t)length >= sizeof(topic)))
    {
        return -1;
    }
    return netbroker_offer(&g_broker, topic, frame, sizeof(frame));
}

/** @brief Never trigger SDK work while reporting broker diagnostics. */
int c3mqtt_status (netbroker_view_t * p_view)
{
    return netbroker_snapshot(&g_broker, p_view);
}

/** @brief Subscribe only to this node's fixed bounded command topic. */
int c3mqtt_transactions (void)
{
    const int input  = snprintf(g_request_topic,
                               sizeof(g_request_topic),
                               "uthome/v1/%s/transaction/request",
                               g_node);
    const int output = snprintf(g_result_topic,
                                sizeof(g_result_topic),
                                "uthome/v1/%s/transaction/result",
                                g_node);
    if ((!gb_attempted) || (input <= 0) || (output <= 0) ||
        ((size_t)input >= sizeof(g_request_topic)) ||
        ((size_t)output >= sizeof(g_result_topic)))
    {
        return -1;
    }
    return netbroker_subscribe(&g_broker, g_request_topic);
}

/** @brief Only SUBACK-complete sessions can receive or send transaction work.
 */
bool c3mqtt_ready (void)
{
    return NETBROKER_READY == g_broker.state;
}

/** @brief Copy into the application owner's command storage, without I/O. */
int c3mqtt_request (uint8_t * p_data, size_t capacity, size_t * p_size)
{
    return netbroker_request(&g_broker, p_data, capacity, p_size);
}

/** @brief A result cannot be replaced by subsequent telemetry offers. */
int c3mqtt_result (const uint8_t * p_data, size_t size)
{
    if ((size != 96U) || (NULL == p_data))
    {
        return -1;
    }
    if ((!c3mqtt_ready()) || gb_result || ('\0' == g_result_topic[0]))
    {
        return 1;
    }
    const int result = netbroker_offer(&g_broker, g_result_topic, p_data, size);
    if (0 == result)
    {
        gb_result = true;
    }
    return result;
}

/** @brief Restrict certificate/SNI names without unbounded reads. */
static bool server_name (const char * p_name)
{
    for (size_t index = 0U; index <= 253U; ++index)
    {
        const unsigned char value = (unsigned char)p_name[index];
        if (0U == value)
        {
            return 0U != index;
        }
        if (!((('a' <= value) && ('z' >= value)) ||
              (('A' <= value) && ('Z' >= value)) ||
              (('0' <= value) && ('9' >= value)) || ('.' == value) ||
              ('-' == value)))
        {
            return false;
        }
    }
    return false;
}

/** @brief Numeric address bypasses synchronous name resolution in ESP-TLS. */
static int connect_step (void * p_context)
{
    /* The one application owner holds the static TLS context. */
    (void)p_context;
    if (NULL == gp_tls)
    {
        gp_tls = esp_tls_init();
        if (NULL == gp_tls)
        {
            return -1;
        }
    }
    return esp_tls_conn_new_async(g_config.p_address,
                                  (int)strlen(g_config.p_address),
                                  g_config.port,
                                  &g_tls_config,
                                  gp_tls);
}

/** @brief Convert TLS WANT to a later owner iteration, EOF to disconnect. */
static int read_step (void * p_context, uint8_t * p_data, size_t size)
{
    /* Context is statically owned, not allocated per callback. */
    (void)p_context;
    const ssize_t count = esp_tls_conn_read(gp_tls, p_data, size);
    if ((ESP_TLS_ERR_SSL_WANT_READ == count) ||
        (ESP_TLS_ERR_SSL_WANT_WRITE == count))
    {
        return 0;
    }
    return ((0 < count) && ((size_t)count <= size)) ? (int)count : -1;
}

/** @brief Preserve partially written MQTT packets across TLS WANT results. */
static int write_step (void * p_context, const uint8_t * p_data, size_t size)
{
    /* Context is statically owned, not allocated per callback. */
    (void)p_context;
    const ssize_t count = esp_tls_conn_write(gp_tls, p_data, size);
    if ((ESP_TLS_ERR_SSL_WANT_READ == count) ||
        (ESP_TLS_ERR_SSL_WANT_WRITE == count))
    {
        return 0;
    }
    return ((0 < count) && ((size_t)count <= size)) ? (int)count : -1;
}

/** @brief ESP-TLS client destruction owns its socket, including partial init.
 */
static void close_step (void * p_context)
{
    /* Close is called only by the serialized broker state machine. */
    (void)p_context;
    if (NULL != gp_tls)
    {
        (void)esp_tls_conn_destroy(gp_tls);
        gp_tls = NULL;
    }
}
#endif

/* End of file. */

