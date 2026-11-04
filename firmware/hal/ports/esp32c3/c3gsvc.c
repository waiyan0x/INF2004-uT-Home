/**
 * @file c3gsvc.c
 * @brief Bind cached sensor frames to the ESP NimBLE GATT service.
 */
#include "c3gsvc.h"
#if UTHOME_NETWORK_BLE
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "c3bport.h"
#include <string.h>

static netble_t g_delivery      = {0};
static uint8_t  g_identity[44]  = {0};
static uint8_t  g_status[20]    = {0};
static uint8_t  g_fragment[128] = {0};
static uint8_t  g_name[]        = "UTHOME-SENSOR";
static uint16_t g_value_handle  = 0U;
static uint16_t g_connection    = BLE_HS_CONN_HANDLE_NONE;
static uint32_t g_epoch         = 0U;
static uint32_t g_owner_epoch   = 0U;
static uint64_t g_next_ms       = 0U;
static uint16_t g_source        = 0U;
static int      g_error         = 0;
static uint8_t  g_address_type  = 0U;
static bool     gb_attempted    = false;
static bool     gb_subscribed   = false;
static bool     gb_ready        = false;
static int access_value (uint16_t                      connection,
                         uint16_t                      attribute,
                         struct ble_gatt_access_ctxt * p_context,
                         void *                        p_argument);
static int gap (struct ble_gap_event * p_event, void * p_context);
static void synchronized (void);
static void reset (int reason);
static void host (void * p_context);
static int advertise (void);
static void word (uint8_t * p_output, uint32_t value);
static void update_status (const netread_info_t * p_info);

static const ble_uuid128_t           g_service       = BLE_UUID128_INIT(0x10,
                                                        0x00,
                                                        0x88,
                                                        0x7d,
                                                        0xb7,
                                                        0x75,
                                                        0xb3,
                                                        0x9d,
                                                        0xee,
                                                        0x4a,
                                                        0x4b,
                                                        0x8a,
                                                        0x20,
                                                        0x1e,
                                                        0x44,
                                                        0xec);
static const ble_uuid128_t           g_identity_uuid = BLE_UUID128_INIT(0x11,
                                                              0x00,
                                                              0x88,
                                                              0x7d,
                                                              0xb7,
                                                              0x75,
                                                              0xb3,
                                                              0x9d,
                                                              0xee,
                                                              0x4a,
                                                              0x4b,
                                                              0x8a,
                                                              0x20,
                                                              0x1e,
                                                              0x44,
                                                              0xec);
static const ble_uuid128_t           g_status_uuid   = BLE_UUID128_INIT(0x12,
                                                            0x00,
                                                            0x88,
                                                            0x7d,
                                                            0xb7,
                                                            0x75,
                                                            0xb3,
                                                            0x9d,
                                                            0xee,
                                                            0x4a,
                                                            0x4b,
                                                            0x8a,
                                                            0x20,
                                                            0x1e,
                                                            0x44,
                                                            0xec);
static const ble_uuid128_t           g_sensor_uuid   = BLE_UUID128_INIT(0x13,
                                                            0x00,
                                                            0x88,
                                                            0x7d,
                                                            0xb7,
                                                            0x75,
                                                            0xb3,
                                                            0x9d,
                                                            0xee,
                                                            0x4a,
                                                            0x4b,
                                                            0x8a,
                                                            0x20,
                                                            0x1e,
                                                            0x44,
                                                            0xec);
static const struct ble_gatt_chr_def g_characteristics[] = {
    {.uuid      = &g_identity_uuid.u,
     .access_cb = access_value,
     .flags     = BLE_GATT_CHR_F_READ},
    {.uuid      = &g_status_uuid.u,
     .access_cb = access_value,
     .flags     = BLE_GATT_CHR_F_READ},
    {.uuid       = &g_sensor_uuid.u,
     .access_cb  = access_value,
     .val_handle = &g_value_handle,
     .flags      = BLE_GATT_CHR_F_NOTIFY},
    {0}};
static const struct ble_gatt_svc_def g_services[] = {
    {.type            = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid            = &g_service.u,
     .characteristics = g_characteristics},
    {0}};

/** @brief Start the actual SDK BLE host with immutable identification. */
int c3gsvc_start (const netread_info_t * p_info)
{
    if ((gb_attempted) || (NULL == p_info) ||
        (NULL == memchr(p_info->node_id, '\0', 32U)))
    {
        return -1;
    }
    gb_attempted  = true;
    g_identity[0] = 1U;
    word(&g_identity[4], (uint32_t)(p_info->meta.boot_id & UINT32_MAX));
    word(&g_identity[8], (uint32_t)(p_info->meta.boot_id >> 32U));
    memcpy(&g_identity[12], p_info->node_id, strlen(p_info->node_id));
    update_status(p_info);
    if (0 != nimble_port_init())
    {
        return -1;
    }
    ble_hs_cfg.sync_cb  = synchronized;
    ble_hs_cfg.reset_cb = reset;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if ((0 != ble_svc_gap_device_name_set("UTHOME-SENSOR")) ||
        (0 != ble_gatts_count_cfg(g_services)) ||
        (0 != ble_gatts_add_svcs(g_services)))
    {
        return -1;
    }
    nimble_port_freertos_init(host);
    return 0;
}

/** @brief Pump copied sensor data without accessing driver/FIFO state. */
int c3gsvc_step (uint64_t               now_ms,
                 const netread_info_t * p_info,
                 netread_capture_t *    p_capture,
                 void *                 p_context)
{
    uint32_t saved        = 0U;
    uint32_t epoch        = 0U;
    uint16_t connection   = 0U;
    bool     b_subscribed = false;
    int      error        = 0;
    if ((NULL == p_info) || (!gb_attempted) || (NULL == p_capture))
    {
        return -1;
    }
    update_status(p_info);
    saved        = c3bport_enter();
    epoch        = g_epoch;
    connection   = g_connection;
    b_subscribed = gb_subscribed && gb_ready;
    error        = g_error;
    c3bport_leave(saved);
    if ((epoch != g_owner_epoch) || (!b_subscribed))
    {
        netble_reset(&g_delivery);
        g_owner_epoch = epoch;
        g_next_ms     = now_ms;
    }
    if ((!b_subscribed) || (BLE_HS_CONN_HANDLE_NONE == connection))
    {
        return error;
    }
    if (now_ms >= g_next_ms)
    {
        sampcache_entry_t entry       = {0};
        uint64_t          captured_ms = 0U;
        const int result = p_capture(p_context, g_source, &entry, &captured_ms);
        if (0 == result)
        {
            if (0 !=
                netble_offer(&g_delivery, &entry, &p_info->meta, captured_ms))
            {
                return -1;
            }
        }
        g_source = (uint16_t)((g_source + 1U) % 8U);
        g_next_ms =
            ((UINT64_MAX - now_ms) >= 1000U) ? now_ms + 1000U : UINT64_MAX;
    }
    size_t size = 0U;
    /* Deliberately cap to MTU 128; pending frames remain <=112 bytes. */
    uint16_t mtu = ble_att_mtu(connection);
    if (128U < mtu)
    {
        mtu = 128U;
    }
    const int result = netble_fragment(&g_delivery,
                                       p_info->meta.generation,
                                       mtu,
                                       g_fragment,
                                       sizeof(g_fragment),
                                       &size);
    if (0 != result)
    {
        return (1 == result) ? 0 : -1;
    }
    struct os_mbuf * p_buffer =
        ble_hs_mbuf_from_flat(g_fragment, (uint16_t)size);
    if (NULL == p_buffer)
    {
        return -1;
    }
    /* NimBLE consumes the mbuf on success and failure. Failed enqueue leaves
     * the application fragment pending, for a later finite attempt. */
    if (0 == ble_gatts_notify_custom(connection, g_value_handle, p_buffer))
    {
        return netble_sent(&g_delivery);
    }
    return -1;
}

/** @brief Read small snapshots only; all characteristic writes are denied. */
static int access_value (uint16_t                      connection,
                         uint16_t                      attribute,
                         struct ble_gatt_access_ctxt * p_context,
                         void *                        p_argument)
{
    uint8_t data[44] = {0};
    size_t  size     = 0U;
    /* SDK owns these handle/context values; UUID selects the read value. */
    (void)connection;
    (void)attribute;
    (void)p_argument;
    if ((NULL == p_context) || (NULL == p_context->om) ||
        (BLE_GATT_ACCESS_OP_READ_CHR != p_context->op))
    {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    if (0 == ble_uuid_cmp(p_context->chr->uuid, &g_identity_uuid.u))
    {
        memcpy(data, g_identity, sizeof(g_identity));
        size = sizeof(g_identity);
    }
    else if (0 == ble_uuid_cmp(p_context->chr->uuid, &g_status_uuid.u))
    {
        const uint32_t saved = c3bport_enter();
        memcpy(data, g_status, sizeof(g_status));
        c3bport_leave(saved);
        size = sizeof(g_status);
    }
    else
    {
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }
    return (0 == os_mbuf_append(p_context->om, data, (uint16_t)size))
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/** @brief Update only small callback flags; never parse or save a profile. */
static int gap (struct ble_gap_event * p_event, void * p_context)
{
    bool b_advertise = false;
    /* No application pointer is passed to the permanent SDK callback. */
    (void)p_context;
    if (NULL == p_event)
    {
        return 0;
    }
    const uint32_t saved = c3bport_enter();
    if (BLE_GAP_EVENT_CONNECT == p_event->type)
    {
        ++g_epoch;
        gb_subscribed = false;
        if (0 == p_event->connect.status)
        {
            g_connection = p_event->connect.conn_handle;
        }
        else
        {
            b_advertise = true;
        }
    }
    else if (BLE_GAP_EVENT_DISCONNECT == p_event->type)
    {
        ++g_epoch;
        g_connection  = BLE_HS_CONN_HANDLE_NONE;
        gb_subscribed = false;
        b_advertise   = true;
    }
    else if (BLE_GAP_EVENT_MTU == p_event->type)
    {
        /* A negotiated MTU change invalidates the active fragment layout. */
        ++g_epoch;
    }
    else if ((BLE_GAP_EVENT_SUBSCRIBE == p_event->type) &&
             (g_value_handle == p_event->subscribe.attr_handle))
    {
        ++g_epoch;
        gb_subscribed = 0U != p_event->subscribe.cur_notify;
    }
    else
    {
        /* Other SDK events leave bounded delivery ownership unchanged. */
    }
    c3bport_leave(saved);
    if (b_advertise)
    {
        const int      result = advertise();
        const uint32_t update = c3bport_enter();
        g_error               = result;
        c3bport_leave(update);
    }
    return 0;
}

/** @brief Address synchronization precedes connectable advertising. */
static void synchronized (void)
{
    uint8_t address_type = 0U;
    int     result       = ble_hs_util_ensure_addr(0);
    if (0 == result)
    {
        result = ble_hs_id_infer_auto(0, &address_type);
    }
    const uint32_t saved = c3bport_enter();
    g_address_type       = address_type;
    gb_ready             = 0 == result;
    g_error              = result;
    c3bport_leave(saved);
    if (0 == result)
    {
        const int      status = advertise();
        const uint32_t update = c3bport_enter();
        g_error               = status;
        c3bport_leave(update);
    }
}

/** @brief Discard connection epoch on host reset, allowing later resync. */
static void reset (int reason)
{
    const uint32_t saved = c3bport_enter();
    ++g_epoch;
    gb_ready      = false;
    gb_subscribed = false;
    g_connection  = BLE_HS_CONN_HANDLE_NONE;
    g_error       = reason;
    c3bport_leave(saved);
}

/** @brief Existing SDK host integration; no shared task adapter changes. */
static void host (void * p_context)
{
    /* SDK starts this permanent task with no application argument. */
    (void)p_context;
    nimble_port_run();
    reset(-1);
    c3bport_exit();
}

/** @brief One peripheral connection; advertising restarts after disconnect. */
static int advertise (void)
{
    struct ble_hs_adv_fields  fields     = {0};
    struct ble_gap_adv_params parameters = {0};
    fields.flags            = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name             = g_name;
    fields.name_len         = sizeof(g_name) - 1U;
    fields.name_is_complete = 1U;
    parameters.conn_mode    = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode    = BLE_GAP_DISC_MODE_GEN;
    const int result        = ble_gap_adv_set_fields(&fields);
    return (0 == result) ? ble_gap_adv_start(g_address_type,
                                             NULL,
                                             BLE_HS_FOREVER,
                                             &parameters,
                                             gap,
                                             NULL)
                         : result;
}

/** @brief Atomic small status snapshot; native padding is never sent. */
static void update_status (const netread_info_t * p_info)
{
    uint8_t status[20] = {1U};
    status[1]          = (uint8_t)p_info->runtime_state;
    status[2]          = p_info->b_wifi_online ? 1U : 0U;
    status[3]          = p_info->b_durable_known ? 1U : 0U;
    word(&status[4], p_info->meta.active_revision);
    word(&status[8], p_info->meta.generation);
    word(&status[12], (uint32_t)(p_info->meta.boot_id & UINT32_MAX));
    word(&status[16], (uint32_t)(p_info->meta.boot_id >> 32U));
    const uint32_t saved = c3bport_enter();
    memcpy(g_status, status, sizeof(status));
    c3bport_leave(saved);
}

/** @brief Explicit little-endian encoding, independent of struct padding. */
static void word (uint8_t * p_output, uint32_t value)
{
    for (size_t index = 0U; index < 4U; ++index)
    {
        p_output[index] = (uint8_t)((value >> (index * 8U)) & 0xffU);
    }
}
#endif

/* End of file. */

