/**
 * @file c3wsta.c
 * @brief Event-driven station readiness without waiting in the owner loop.
 */
#include "c3wsta.h"
#if UTHOME_NETWORK_WIFI
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_random.h"
#include "c3bport.h"
#include "netretry.h"
#include <string.h>

static netretry_t g_retry         = {0};
static int32_t    g_sdk_error     = 0;
static uint32_t   g_address       = 0U;
static uint16_t   g_reason        = 0U;
static uint8_t    g_stage         = C3WSTA_STAGE_NONE;
static int8_t     g_tx_power      = 0;
static bool       gb_attempted    = false;
static bool       gb_started      = false;
static bool       gb_ip           = false;
static bool       gb_station      = false;
static bool       gb_disconnected = false;
static bool       gb_cancel       = false;
static bool       gb_fault        = false;
static void event (void *           p_context,
                   esp_event_base_t base,
                   int32_t          identifier,
                   void *           p_data);
static int cancel (void);
static int associate (void);
static int operation (uint8_t stage, int32_t sdk_error);
static size_t length (const char * p_text, size_t limit);

/** @brief Initialize RAM credentials once; retain failed SDK ownership. */
int c3wsta_start (const char * p_ssid,
                  const char * p_password,
                  int32_t      tx_power_quarter_dbm)
{
    wifi_init_config_t initial = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t      config  = {0};
    if ((gb_attempted) || (NULL == p_ssid) || (NULL == p_password) ||
        (32U < length(p_ssid, 33U)) || (63U < length(p_password, 64U)) ||
        ((0 != tx_power_quarter_dbm) &&
         ((8 > tx_power_quarter_dbm) || (84 < tx_power_quarter_dbm))))
    {
        return -1;
    }
    const size_t ssid_size     = strlen(p_ssid);
    const size_t password_size = strlen(p_password);
    if ((0U == ssid_size) || (8U > password_size))
    {
        return -1;
    }
    /* Zero and the validated SDK range 8..84 fit its signed int8_t ABI. */
    g_tx_power         = (int8_t)tx_power_quarter_dbm;
    gb_attempted       = true;
    initial.nvs_enable = false;
    /* The pinned SDK permits null instance outputs. These permanent handlers
     * are never unregistered; retaining three unused handles wastes RAM. */
    if ((0 != operation(C3WSTA_STAGE_NETIF, esp_netif_init())) ||
        (0 !=
         operation(C3WSTA_STAGE_EVENT_LOOP, esp_event_loop_create_default())) ||
        (0 != operation(C3WSTA_STAGE_INTERFACE,
                        (NULL == esp_netif_create_default_wifi_sta())
                            ? ESP_ERR_NO_MEM
                            : ESP_OK)) ||
        (0 !=
         operation(C3WSTA_STAGE_WIFI_EVENT,
                   esp_event_handler_instance_register(
                       WIFI_EVENT, ESP_EVENT_ANY_ID, event, NULL, NULL))) ||
        (0 !=
         operation(C3WSTA_STAGE_GOT_IP,
                   esp_event_handler_instance_register(
                       IP_EVENT, IP_EVENT_STA_GOT_IP, event, NULL, NULL))) ||
        (0 !=
         operation(C3WSTA_STAGE_LOST_IP,
                   esp_event_handler_instance_register(
                       IP_EVENT, IP_EVENT_STA_LOST_IP, event, NULL, NULL))) ||
        (0 != operation(C3WSTA_STAGE_INITIALIZE, esp_wifi_init(&initial))) ||
        (0 != operation(C3WSTA_STAGE_STORAGE,
                        esp_wifi_set_storage(WIFI_STORAGE_RAM))) ||
        (0 != operation(C3WSTA_STAGE_MODE, esp_wifi_set_mode(WIFI_MODE_STA))))
    {
        gb_fault = true;
        return -1;
    }
    memcpy(config.sta.ssid, p_ssid, ssid_size);
    memcpy(config.sta.password, p_password, password_size);
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    if ((0 != operation(C3WSTA_STAGE_CONFIGURE,
                        esp_wifi_set_config(WIFI_IF_STA, &config))) ||
        (0 != operation(C3WSTA_STAGE_START, esp_wifi_start())) ||
        (0 != netretry_init(&g_retry, 0U)))
    {
        gb_fault = true;
        return -1;
    }
    gb_started = true;
    g_stage    = C3WSTA_STAGE_READY;
    return 0;
}

/** @brief Associate/retry only after prior disconnect cancellation completes.
 */
int c3wsta_step (uint64_t now_ms)
{
    const uint32_t saved          = c3bport_enter();
    const bool     b_station      = gb_station;
    const bool     b_ip           = gb_ip;
    const bool     b_disconnected = gb_disconnected;
    gb_disconnected               = false;
    c3bport_leave(saved);
    if ((!gb_started) || (gb_fault))
    {
        return -1;
    }
    const uint32_t jitter = esp_random() % 251U;
    if (b_disconnected)
    {
        gb_cancel = false;
        if (NETRETRY_WAITING != g_retry.state)
        {
            if (0 != netretry_result(&g_retry, now_ms, false, jitter))
            {
                gb_fault = true;
                return -1;
            }
        }
    }
    if ((b_ip) && (NETRETRY_ATTEMPT == g_retry.state) && (!gb_cancel))
    {
        if (0 != netretry_result(&g_retry, now_ms, true, jitter))
        {
            return cancel();
        }
    }
    else if ((!b_ip) && (NETRETRY_ONLINE == g_retry.state))
    {
        if (0 != netretry_result(&g_retry, now_ms, false, jitter))
        {
            gb_fault = true;
            return -1;
        }
        return cancel();
    }
    else
    {
        /* No new IP transition is pending. */
    }
    if (gb_cancel)
    {
        return 0;
    }
    const netretry_state_t before = g_retry.state;
    const int result = netretry_poll(&g_retry, now_ms, b_station, jitter);
    if (0 > result)
    {
        gb_fault = true;
        return -1;
    }
    if ((NETRETRY_ATTEMPT == before) && (NETRETRY_WAITING == g_retry.state))
    {
        return cancel();
    }
    if ((1 == result) && (0 != associate()))
    {
        return netretry_result(&g_retry, now_ms, false, jitter);
    }
    return 0;
}

/** @brief IP events alone cannot declare a timed-out attempt online. */
bool c3wsta_online (void)
{
    const uint32_t saved = c3bport_enter();
    const bool     b_ip  = gb_ip;
    c3bport_leave(saved);
    return (gb_started) && (!gb_fault) && (b_ip) && (!gb_cancel) &&
           (NETRETRY_ONLINE == g_retry.state);
}

/** @brief Copy owner state and protected event observations without SDK calls.
 */
int c3wsta_status (c3wsta_status_t * p_status)
{
    if (NULL == p_status)
    {
        return -1;
    }
    *p_status       = (c3wsta_status_t){0};
    p_status->stage = g_stage;
    /* WAITING, ATTEMPT and ONLINE are values 0..2 and fit the wire byte. */
    p_status->retry_state       = (uint8_t)g_retry.state;
    p_status->sdk_error         = g_sdk_error;
    p_status->deadline_ms       = g_retry.deadline_ms;
    const uint32_t saved        = c3bport_enter();
    p_status->disconnect_reason = g_reason;
    memcpy(p_status->address, &g_address, sizeof(p_status->address));
    p_status->flags =
        (uint8_t)((gb_attempted ? 1U : 0U) | (gb_started ? 2U : 0U) |
                  (gb_station ? 4U : 0U) | (gb_ip ? 8U : 0U) |
                  (gb_cancel ? 16U : 0U) | (gb_fault ? 32U : 0U));
    c3bport_leave(saved);
    return 0;
}

/** @brief Copy bounded event observations; never read configuration/queues. */
static void event (void *           p_context,
                   esp_event_base_t base,
                   int32_t          identifier,
                   void *           p_data)
{
    /* Callback context is not used; typed event payloads are copied below. */
    (void)p_context;
    const uint32_t saved = c3bport_enter();
    if ((WIFI_EVENT == base) && (WIFI_EVENT_STA_START == identifier))
    {
        gb_station = true;
    }
    else if ((WIFI_EVENT == base) &&
             (WIFI_EVENT_STA_DISCONNECTED == identifier))
    {
        gb_ip           = false;
        gb_disconnected = true;
        g_address       = 0U;
        if (NULL != p_data)
        {
            const wifi_event_sta_disconnected_t * p_disconnected = p_data;
            g_reason = p_disconnected->reason;
        }
    }
    else if ((WIFI_EVENT == base) && (WIFI_EVENT_STA_STOP == identifier))
    {
        gb_station      = false;
        gb_ip           = false;
        gb_disconnected = true;
        g_address       = 0U;
    }
    else if ((IP_EVENT == base) && (IP_EVENT_STA_GOT_IP == identifier))
    {
        gb_ip = true;
        if (NULL != p_data)
        {
            const ip_event_got_ip_t * p_ip = p_data;
            g_address                      = p_ip->ip_info.ip.addr;
        }
    }
    else if ((IP_EVENT == base) && (IP_EVENT_STA_LOST_IP == identifier))
    {
        gb_ip     = false;
        g_address = 0U;
    }
    else
    {
        /* Other notices do not affect station readiness. */
    }
    c3bport_leave(saved);
}

/** @brief Do not start another association while old completion is pending. */
static int cancel (void)
{
    const int result = esp_wifi_disconnect();
    /* NOT_CONNECT is benign cancellation completion, but remains observable. */
    g_stage     = C3WSTA_STAGE_DISCONNECT;
    g_sdk_error = result;
    if (ESP_ERR_WIFI_NOT_CONNECT == result)
    {
        gb_cancel = false;
    }
    else if (ESP_OK == result)
    {
        gb_cancel = true;
    }
    else
    {
        gb_fault = true;
        return -1;
    }
    return 0;
}

/** @brief Apply an explicit cap after station start, before every attempt. */
static int associate (void)
{
    if ((0 != g_tx_power) &&
        (0 != operation(C3WSTA_STAGE_TX_POWER,
                        esp_wifi_set_max_tx_power(g_tx_power))))
    {
        return -1;
    }
    return operation(C3WSTA_STAGE_CONNECT, esp_wifi_connect());
}

/** @brief Retain the exact SDK stage/result without printing private inputs. */
static int operation (uint8_t stage, int32_t sdk_error)
{
    g_stage     = stage;
    g_sdk_error = sdk_error;
    return (ESP_OK == sdk_error) ? 0 : -1;
}

/** @brief Stop at the terminator instead of reading past short strings. */
static size_t length (const char * p_text, size_t limit)
{
    size_t size = 0U;
    while ((size < limit) && ('\0' != p_text[size]))
    {
        ++size;
    }
    return size;
}
#endif

/* End of file. */

