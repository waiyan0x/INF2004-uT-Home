/**
 * @file c3mqtt.c
 * @brief Drive one ESP-TLS client, copied per-source MQTT telemetry/state and
 * Home Assistant discovery.
 */
#include "c3mqtt.h"
#if UTHOME_NETWORK_WIFI
#include "netbroker.h"
#include "mqttdisc.h"
#include "esp_tls.h"
#include "esp_random.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <stdio.h>
#include <string.h>

/* Consecutive busy/failed captures tolerated on one discovery step before it
 * is skipped without publishing. Shared copies are short; at the 1 ms owner
 * delay this is about 0.1 s per step, 5.6 s for a pass that never captures. */
#define C3MQTT_CAPTURE_RETRIES 100U
/* Cursor value after the last step is offered and the broker has gone idle,
 * so the final discovery message can no longer be replaced by telemetry. */
#define C3MQTT_DISCOVERY_DRAINED (UTHOME_MQTTDISC_STEPS + 1U)

static netbroker_t       g_broker           = {0};
static c3mqtt_config_t   g_config           = {0};
static esp_tls_cfg_t     g_tls_config       = {0};
static esp_tls_t *       gp_tls             = NULL;
static sampcache_entry_t g_sample           = {0};
static char              g_node[32]         = {0};
static char              g_availability[80] = {0};
static uint64_t          g_next_ms          = 0U;
static uint64_t          g_captured_ms      = 0U;
static uint16_t          g_source           = 0U;
static uint8_t           g_discovery        = 0U;
static uint8_t           g_capture_failures = 0U;
static bool              gb_attempted       = false;
static bool              gb_state_due       = false;
static bool server_name (const char * p_name);
static int publish (uint64_t              now_ms,
                    const netble_meta_t * p_meta,
                    netread_capture_t *   p_capture,
                    void *                p_context);
static int discover (netread_capture_t * p_capture, void * p_context);
static int state_step (void);
static int telemetry_step (uint64_t              now_ms,
                           const netble_meta_t * p_meta,
                           netread_capture_t *   p_capture,
                           void *                p_context);
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

/** @brief One broker I/O unit, then at most one new publication offer. */
int c3mqtt_step (uint64_t               now_ms,
                 bool                   b_ip,
                 const netread_info_t * p_info,
                 netread_capture_t *    p_capture,
                 void *                 p_context)
{
    int result = 0;
    if ((!gb_attempted) || (NULL == p_info) || (NULL == p_capture))
    {
        return -1;
    }
    result = netbroker_step(
        &g_broker, now_ms, b_ip, p_info->meta.generation, esp_random() % 251U);
    if (NETBROKER_READY != g_broker.state)
    {
        /* Every new session, including one after a plan change, restarts
         * discovery from slot 0 and drops any state built for the old one. */
        g_discovery        = 0U;
        g_capture_failures = 0U;
        gb_state_due       = false;
    }
    if ((0 != result) || (NETBROKER_READY != g_broker.state))
    {
        return result;
    }
    /* Formatting buffers live in the callees, after the broker I/O returns. */
    return publish(now_ms, &p_info->meta, p_capture, p_context);
}

/** @brief Never trigger SDK work while reporting broker diagnostics. */
int c3mqtt_status (netbroker_view_t * p_view)
{
    return netbroker_snapshot(&g_broker, p_view);
}

/** @brief Discovery first, then rotating telemetry with its follow-on state.
 */
static int publish (uint64_t              now_ms,
                    const netble_meta_t * p_meta,
                    netread_capture_t *   p_capture,
                    void *                p_context)
{
    if (UTHOME_MQTTDISC_STEPS > g_discovery)
    {
        /* Telemetry waits: a newer offer would replace pending discovery. */
        return netbroker_idle(&g_broker) ? discover(p_capture, p_context) : 0;
    }
    if (C3MQTT_DISCOVERY_DRAINED > g_discovery)
    {
        /* The final discovery message may still be pending; only the first
         * telemetry offer of a session waits for it to reach the wire. */
        if (!netbroker_idle(&g_broker))
        {
            return 0;
        }
        g_discovery = C3MQTT_DISCOVERY_DRAINED;
    }
    if ((gb_state_due) && (now_ms < g_next_ms))
    {
        return netbroker_idle(&g_broker) ? state_step() : 0;
    }
    if (now_ms < g_next_ms)
    {
        return 0;
    }
    /* A state message not sent before the next tick is dropped. */
    gb_state_due = false;
    return telemetry_step(now_ms, p_meta, p_capture, p_context);
}

/** @brief One retained config or removal per step; bounded capture retry.
 */
static int discover (netread_capture_t * p_capture, void * p_context)
{
    mqttdisc_message_t message     = {0};
    uint64_t           captured_ms = 0U;
    const uint16_t     slot        = mqttdisc_slot(g_discovery);
    const int captured = p_capture(p_context, slot, &g_sample, &captured_ms);
    if ((0 != captured) && (1 != captured))
    {
        /* Busy/failed capture never removes an entity. Retry the same step,
         * then skip it without any publication once the bound is reached. */
        ++g_capture_failures;
        if (C3MQTT_CAPTURE_RETRIES <= g_capture_failures)
        {
            g_capture_failures = 0U;
            ++g_discovery;
        }
        return 0;
    }
    g_capture_failures  = 0U;
    const int formatted = mqttdisc_config(
        g_discovery, g_node, (0 == captured) ? &g_sample : NULL, &message);
    ++g_discovery;
    if (0 != formatted)
    {
        return -1;
    }
    /* The payload is ASCII JSON text; character types may alias uint8_t. */
    return netbroker_offer(&g_broker,
                           message.topic,
                           (const uint8_t *)message.payload,
                           message.payload_size,
                           true);
}

/** @brief JSON state from the same copy as the preceding binary telemetry. */
static int state_step (void)
{
    mqttdisc_message_t message = {0};
    gb_state_due               = false;
    const int formatted =
        mqttdisc_state(g_node, &g_sample, g_captured_ms, &message);
    if (0 != formatted)
    {
        return (0 > formatted) ? -1 : 0;
    }
    /* The payload is ASCII JSON text; character types may alias uint8_t. */
    return netbroker_offer(&g_broker,
                           message.topic,
                           (const uint8_t *)message.payload,
                           message.payload_size,
                           false);
}

/** @brief At most one copied sensor each second, rotating over all slots. */
static int telemetry_step (uint64_t              now_ms,
                           const netble_meta_t * p_meta,
                           netread_capture_t *   p_capture,
                           void *                p_context)
{
    uint8_t  frame[UTHOME_BLE_FRAME] = {0};
    char     topic[128]              = {0};
    uint64_t captured_ms             = 0U;
    int      length                  = 0;
    g_next_ms                        = now_ms + 1000U;
    const uint16_t source            = g_source;
    g_source = (uint16_t)((g_source + 1U) % UTHOME_SAMPCACHE_SOURCES);
    if ((0 != p_capture(p_context, source, &g_sample, &captured_ms)) ||
        (source != g_sample.source.source_index))
    {
        return 0;
    }
    g_captured_ms = captured_ms;
    gb_state_due  = 1U == sampcache_quality(&g_sample, captured_ms);
    if (0 !=
        netble_encode(&g_sample, p_meta, captured_ms, frame, sizeof(frame)))
    {
        /* RAW has no binary frame; its JSON state may still follow. */
        return 0;
    }
    length = snprintf(topic,
                      sizeof(topic),
                      "uthome/v1/%s/telemetry/%s",
                      g_node,
                      g_sample.source.name);
    if ((0 > length) || ((size_t)length >= sizeof(topic)))
    {
        gb_state_due = false;
        return -1;
    }
    return netbroker_offer(&g_broker, topic, frame, sizeof(frame), false);
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

