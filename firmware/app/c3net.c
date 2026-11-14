/**
 * @file c3net.c
 * @brief Feed live owner metadata and copied samples to actual ESP transports.
 */
#include "c3net.h"
#include "c3tx.h"
#if UTHOME_NETWORK_WIFI || UTHOME_NETWORK_BLE
#include "activate.h"
#include "shared.h"
#include "c3gsvc.h"
#include "c3https.h"
#include "c3wsta.h"
#include "c3mqtt.h"
#include "esp_random.h"
#include "esp_mac.h"
#include <stdio.h>
#include <string.h>
#if UTHOME_NETWORK_WIFI
#include "netsetup.h"
#endif

static netread_info_t g_info       = {0};
static bool           gb_attempted = false;
#if UTHOME_NETWORK_WIFI
static bool gb_wifi  = false;
static bool gb_https = false;
#if UTHOME_SETUP_MQTT
static bool gb_mqtt = false;
#endif
#endif
#if UTHOME_NETWORK_BLE
static bool gb_ble = false;
#endif
static int capture (void *              p_context,
                    uint16_t            source,
                    sampcache_entry_t * p_entry,
                    uint64_t *          p_now);
static int metadata (const cfgctrl_t * p_control);

/** @brief Boot identity and explicit local deployment inputs; no defaults. */
int c3net_start (const cfgctrl_t * p_control, cfgmux_t * p_mux)
{
    uint8_t address[6] = {0};
    int     result     = 0;
    if ((gb_attempted) || (NULL == p_control) ||
        (ESP_OK != esp_read_mac(address, ESP_MAC_WIFI_STA)))
    {
        return -1;
    }
    gb_attempted = true;
    esp_fill_random(&g_info.meta.boot_id, sizeof(g_info.meta.boot_id));
    const int length = snprintf(g_info.node_id,
                                sizeof(g_info.node_id),
                                "uthome-%02x%02x%02x%02x%02x%02x",
                                (unsigned int)address[0],
                                (unsigned int)address[1],
                                (unsigned int)address[2],
                                (unsigned int)address[3],
                                (unsigned int)address[4],
                                (unsigned int)address[5]);
    if ((0 > length) || ((size_t)length >= sizeof(g_info.node_id)) ||
        (0 != metadata(p_control)))
    {
        return -1;
    }
#if UTHOME_NETWORK_WIFI
    if ((NULL != p_mux) && (0 != cfgmux_boot(p_mux, g_info.meta.boot_id)))
    {
        return -1;
    }
    g_info.b_configuration_supported = NULL != p_mux;

    const c3https_config_t https = {.p_certificate = g_setup_certificate,
                                    .p_key         = g_setup_key,
                                    .p_token       = g_setup_token,
                                    .certificate_size =
                                        sizeof(g_setup_certificate),
                                    .key_size   = sizeof(g_setup_key),
                                    .token_size = sizeof(g_setup_token) - 1U,
                                    .port       = UTHOME_SETUP_PORT,
                                    .p_mux      = p_mux};
    gb_https                     = 0 == c3https_start(&https);
    gb_wifi                      = 0 == c3wsta_start(
                       g_setup_ssid, g_setup_password, UTHOME_SETUP_TX_POWER);
#if UTHOME_SETUP_MQTT
    const c3mqtt_config_t broker = {.p_address     = g_setup_mqtt_address,
                                    .p_server_name = g_setup_mqtt_server_name,
                                    .p_user        = g_setup_mqtt_user,
                                    .p_password    = g_setup_mqtt_password,
                                    .p_ca          = g_setup_mqtt_ca,
                                    .ca_size       = sizeof(g_setup_mqtt_ca),
                                    .port          = UTHOME_SETUP_MQTT_PORT};
    gb_mqtt                      = 0 == c3mqtt_start(&broker, g_info.node_id);
    if (gb_mqtt)
    {
        gb_mqtt = 0 == c3mqtt_transactions();
    }
#endif
    if (!gb_wifi)
    {
        result = -1;
    }
#endif
#if !UTHOME_NETWORK_WIFI
    /* BLE has no configuration writes or upload owner. */
    (void)p_mux;
#endif
#if UTHOME_NETWORK_BLE
    gb_ble = 0 == c3gsvc_start(&g_info);
    if (!gb_ble)
    {
        result = -1;
    }
#endif
    return result;
}

/** @brief Shared owner ensures plan identity stays fixed during capture/encode.
 */
int c3net_step (const cfgctrl_t * p_control, uint64_t now_ms)
{
    int result = 0;
    if ((!gb_attempted) || (0 != metadata(p_control)))
    {
        return -1;
    }
#if UTHOME_NETWORK_WIFI
    if (gb_wifi)
    {
        if (0 != c3wsta_step(now_ms))
        {
            result = -1;
        }
        g_info.b_wifi_online = c3wsta_online();
        if ((gb_https) && (!c3tx_busy()))
        {
            const int served = c3https_step(
                now_ms, g_info.b_wifi_online, &g_info, capture, NULL);
            if ((0 > served) || ((0 < served) && (0 != metadata(p_control))))
            {
                return -1;
            }
        }
#if UTHOME_SETUP_MQTT
        if ((gb_mqtt) &&
            (0 !=
             c3mqtt_step(now_ms, g_info.b_wifi_online, &g_info, capture, NULL)))
        {
            result = -1;
        }
        c3tx_step(now_ms, g_info.meta.generation, gb_mqtt && (c3mqtt_ready()));
#endif
    }
#endif
#if UTHOME_NETWORK_BLE
    if ((gb_ble) && (0 != c3gsvc_step(now_ms, &g_info, capture, NULL)))
    {
        result = -1;
    }
#endif
    return result;
}

#if UTHOME_NETWORK_WIFI
/** @brief Encode a coherent station snapshot into the USB owner's buffer. */
int c3net_observe (uint8_t * p_data, size_t capacity, size_t * p_size)
{
    c3wsta_status_t status = {0};
    if ((NULL == p_data) || (NULL == p_size) || (24U > capacity) ||
        (0 != c3wsta_status(&status)))
    {
        return -1;
    }
    memset(p_data, 0, 24U);
    p_data[0] = 1U;
    p_data[1] = status.flags;
    p_data[2] = status.stage;
    p_data[3] = status.retry_state;
    /* Signed status conversion preserves its modulo-2^32 wire encoding. */
    cfgbytes_put(&p_data[4], (uint32_t)status.sdk_error);
    p_data[8] = (uint8_t)(status.disconnect_reason & UINT16_C(0xFF));
    p_data[9] = (uint8_t)(status.disconnect_reason >> 8U);
    memcpy(&p_data[12], status.address, sizeof(status.address));
    cfgbytes_put(&p_data[16], (uint32_t)(status.deadline_ms & UINT32_MAX));
    cfgbytes_put(&p_data[20], (uint32_t)(status.deadline_ms >> 32U));
    *p_size = 24U;
    return 0;
}
#endif

/** @brief Never bypass shared serialization or consume display/rule queues. */
static int capture (void *              p_context,
                    uint16_t            source,
                    sampcache_entry_t * p_entry,
                    uint64_t *          p_now)
{
    /* Adapter is stateless; all borrowed application metadata stays owner-only.
     */
    (void)p_context;
    return shared_source(source, p_entry, p_now);
}

/** @brief Copy controller/runtime state without mutating retry or save state.
 */
static int metadata (const cfgctrl_t * p_control)
{
    activate_status_t status = {0};
    if ((NULL == p_control) || (!p_control->b_initialized) ||
        (0 != activate_get_status(&status)))
    {
        return -1;
    }
    g_info.meta.active_revision = p_control->active_revision;
    g_info.meta.generation      = status.generation;
    g_info.saved_revision       = p_control->saved_revision;
    g_info.runtime_state        = (uint32_t)status.state;
    g_info.b_durable_known      = p_control->b_saved_known;
#if UTHOME_NETWORK_WIFI && UTHOME_SETUP_MQTT
    g_info.b_mqtt_enabled = true;
    if (0 != c3mqtt_status(&g_info.mqtt))
    {
        return -1;
    }
#endif
    return 0;
}
#endif

/* End of file. */

