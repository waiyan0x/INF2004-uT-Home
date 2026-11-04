/** @file c3gatt.c
 * @brief Fixed service definitions with bounded twelve-byte read responses.
 */
#include "c3gatt.h"

#if defined(UTHOME_BLE_GATT_PROBE) && UTHOME_BLE_GATT_PROBE
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_timer.h"
#include "c3bport.h"

static uint32_t g_reads = 0U;
static int read_value (uint16_t                      connection,
                       uint16_t                      attribute,
                       struct ble_gatt_access_ctxt * p_context,
                       void *                        p_argument);
static void put_word (uint8_t * p_data, uint32_t value);

static const ble_uuid128_t           g_service = BLE_UUID128_INIT(0x01,
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
static const ble_uuid128_t           g_value   = BLE_UUID128_INIT(0x02,
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
    {.uuid = &g_value.u, .access_cb = read_value, .flags = BLE_GATT_CHR_F_READ},
    {0}};
static const struct ble_gatt_svc_def g_services[] = {
    {.type            = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid            = &g_service.u,
     .characteristics = g_characteristics},
    {0}};

/** @brief Register fixed definitions; preserve every SDK initialization error.
 */
int c3gatt_init (void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int result = ble_svc_gap_device_name_set("UTHOME-GATT");
    if (0 == result)
    {
        result = ble_gatts_count_cfg(g_services);
    }
    if (0 == result)
    {
        result = ble_gatts_add_svcs(g_services);
    }
    return result;
}

/** @brief Read a saturating observation count without clearing it. */
uint32_t c3gatt_reads (void)
{
    const uint32_t saved = c3bport_enter();
    const uint32_t reads = g_reads;
    c3bport_leave(saved);
    return reads;
}

/** @brief Return UTHG, version one and live uptime modulo 2^32 milliseconds. */
static int read_value (uint16_t                      connection,
                       uint16_t                      attribute,
                       struct ble_gatt_access_ctxt * p_context,
                       void *                        p_argument)
{
    /* The SDK binds the callback only to this characteristic. */
    (void)connection;
    (void)attribute;
    (void)p_argument;
    if ((NULL == p_context) || (NULL == p_context->om))
    {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (BLE_GATT_ACCESS_OP_READ_CHR != p_context->op)
    {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    const int64_t now_us = esp_timer_get_time();
    if (now_us < 0)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }
    uint8_t data[12] = {'U', 'T', 'H', 'G', 0U};
    put_word(&data[4], 1U);
    put_word(&data[8], (uint32_t)(((uint64_t)now_us / 1000U) & UINT32_MAX));
    if (0 != os_mbuf_append(p_context->om, data, sizeof(data)))
    {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    const uint32_t saved = c3bport_enter();
    if (g_reads < UINT32_MAX)
    {
        ++g_reads;
    }
    c3bport_leave(saved);
    return 0;
}

/** @brief Explicit endian encoding, independent of alignment or C struct ABI.
 */
static void put_word (uint8_t * p_data, uint32_t value)
{
    for (uint32_t index = 0U; index < 4U; ++index)
    {
        p_data[index] = (uint8_t)((value >> (index * 8U)) & UINT8_MAX);
    }
}
#endif

/* End of file. */

