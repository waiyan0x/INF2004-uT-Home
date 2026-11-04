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

static netretry_t                   g_retry         = {0};
static esp_event_handler_instance_t g_wifi_handler  = NULL;
static esp_event_handler_instance_t g_ip_handler    = NULL;
static esp_event_handler_instance_t g_lost_handler  = NULL;
static bool                         gb_attempted    = false;
static bool                         gb_started      = false;
static bool                         gb_ip           = false;
static bool                         gb_station      = false;
static bool                         gb_disconnected = false;
static bool                         gb_cancel       = false;
static bool                         gb_fault        = false;
static void event (void *           p_context,
                   esp_event_base_t base,
                   int32_t          identifier,
                   void *           p_data);
static int cancel (void);
static size_t length (const char * p_text, size_t limit);

/** @brief Initialize RAM credentials once; retain failed SDK ownership. */
int c3wsta_start (const char * p_ssid, const char * p_password)
{
    wifi_init_config_t initial = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t      config  = {0};
    if ((gb_attempted) || (NULL == p_ssid) || (NULL == p_password) ||
        (32U < length(p_ssid, 33U)) || (63U < length(p_password, 64U)))
    {
        return -1;
    }
    const size_t ssid_size     = strlen(p_ssid);
    const size_t password_size = strlen(p_password);
    if ((0U == ssid_size) || (8U > password_size))
    {
        return -1;
    }
    gb_attempted       = true;
    initial.nvs_enable = false;
    if ((ESP_OK != esp_netif_init()) ||
        (ESP_OK != esp_event_loop_create_default()) ||
        (NULL == esp_netif_create_default_wifi_sta()) ||
        (ESP_OK !=
         esp_event_handler_instance_register(
             WIFI_EVENT, ESP_EVENT_ANY_ID, event, NULL, &g_wifi_handler)) ||
        (ESP_OK !=
         esp_event_handler_instance_register(
             IP_EVENT, IP_EVENT_STA_GOT_IP, event, NULL, &g_ip_handler)) ||
        (ESP_OK !=
         esp_event_handler_instance_register(
             IP_EVENT, IP_EVENT_STA_LOST_IP, event, NULL, &g_lost_handler)) ||
        (ESP_OK != esp_wifi_init(&initial)) ||
        (ESP_OK != esp_wifi_set_storage(WIFI_STORAGE_RAM)) ||
        (ESP_OK != esp_wifi_set_mode(WIFI_MODE_STA)))
    {
        return -1;
    }
    memcpy(config.sta.ssid, p_ssid, ssid_size);
    memcpy(config.sta.password, p_password, password_size);
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    if ((ESP_OK != esp_wifi_set_config(WIFI_IF_STA, &config)) ||
        (ESP_OK != esp_wifi_start()) || (0 != netretry_init(&g_retry, 0U)))
    {
        return -1;
    }
    gb_started = true;
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
    if ((1 == result) && (ESP_OK != esp_wifi_connect()))
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

/** @brief SDK callback copies flags only; never reads configuration/queues. */
static void event (void *           p_context,
                   esp_event_base_t base,
                   int32_t          identifier,
                   void *           p_data)
{
    /* Default event loop supplies unused opaque event metadata. */
    (void)p_context;
    (void)p_data;
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
    }
    else if ((WIFI_EVENT == base) && (WIFI_EVENT_STA_STOP == identifier))
    {
        gb_station      = false;
        gb_ip           = false;
        gb_disconnected = true;
    }
    else if ((IP_EVENT == base) && (IP_EVENT_STA_GOT_IP == identifier))
    {
        gb_ip = true;
    }
    else if ((IP_EVENT == base) && (IP_EVENT_STA_LOST_IP == identifier))
    {
        gb_ip = false;
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

