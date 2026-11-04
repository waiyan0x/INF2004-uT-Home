/**
 * @file netble.c
 * @brief Preserve sensor quality and signed values over bounded BLE frames.
 */
#include "netble.h"
#include "uthome/health.h"
#include <float.h>
#include <math.h>
#include <string.h>
#if (FLT_RADIX != 2) || (FLT_MANT_DIG != 24) || (FLT_MAX_EXP != 128)
#error BLE telemetry requires binary32 float representation
#endif
static void word (uint8_t * p_output, uint32_t value);
static void wide (uint8_t * p_output, uint64_t value);
static int values (sampcache_entry_t const * p_entry, uint8_t * p_output);

/** @brief Build one canonical frame without serializing native padding. */
int netble_encode (sampcache_entry_t const * p_entry,
                   netble_meta_t const *     p_meta,
                   uint64_t                  now_ms,
                   uint8_t *                 p_output,
                   size_t                    capacity)
{
    uint8_t  frame[UTHOME_BLE_FRAME] = {0};
    uint32_t quality                 = 0U;
    uint64_t age                     = UINT32_MAX;
    if ((NULL == p_entry) || (NULL == p_meta) || (NULL == p_output) ||
        (capacity < sizeof(frame)) || (!p_entry->b_expected) ||
        (1U > p_entry->source.type) ||
        (UTHOME_SAMPLE_LIGHT < p_entry->source.type) ||
        (sizeof(float) != sizeof(uint32_t)) ||
        (NULL == memchr(p_entry->source.name, '\0', 32U)))
    {
        return -1;
    }
    quality  = sampcache_quality(p_entry, now_ms);
    frame[0] = 1U;
    frame[1] = (uint8_t)p_entry->source.type;
    frame[2] = (uint8_t)quality;
    frame[3] = p_entry->b_seen ? (uint8_t)p_entry->header.status : 0U;
    if ((p_entry->b_seen) && (UTHOME_SAMPLE_INVALID < p_entry->header.status))
    {
        frame[3] = UTHOME_SAMPLE_INVALID;
    }
    frame[4] = (uint8_t)(p_entry->source.source_index & 0xffU);
    frame[5] = (uint8_t)(p_entry->source.source_index >> 8U);
    word(&frame[12], p_meta->active_revision);
    word(&frame[16], p_meta->generation);
    wide(&frame[20], p_meta->boot_id);
    if (p_entry->b_seen)
    {
        word(&frame[8], p_entry->header.sequence);
        wide(&frame[28], p_entry->header.timestamp_ms);
        if (now_ms >= p_entry->header.timestamp_ms)
        {
            age = now_ms - p_entry->header.timestamp_ms;
        }
    }
    word(&frame[36], (age < UINT32_MAX) ? (uint32_t)age : UINT32_MAX);
    word(&frame[40], p_entry->source.stale_ms);
    memcpy(&frame[44], p_entry->source.name, strlen(p_entry->source.name));
    if ((1U == quality) || (2U == quality))
    {
        if (0 != values(p_entry, &frame[76]))
        {
            frame[2] = 3U;
            frame[3] = UTHOME_SAMPLE_INVALID;
            memset(&frame[76], 0, 36U);
        }
    }
    memcpy(p_output, frame, sizeof(frame));
    return 0;
}

/** @brief Release notification state without any SDK or persistence work. */
void netble_reset (netble_t * p_ble)
{
    if (NULL != p_ble)
    {
        memset(p_ble, 0, sizeof(*p_ble));
    }
}

/** @brief Retain at most one unsent newer sensor frame. */
int netble_offer (netble_t *                p_ble,
                  sampcache_entry_t const * p_entry,
                  netble_meta_t const *     p_meta,
                  uint64_t                  now_ms)
{
    if ((NULL == p_ble) ||
        (0 !=
         netble_encode(
             p_entry, p_meta, now_ms, p_ble->pending, sizeof(p_ble->pending))))
    {
        return -1;
    }
    if ((p_ble->b_pending) && (UINT32_MAX > p_ble->replaced))
    {
        ++p_ble->replaced;
    }
    p_ble->pending_generation = p_meta->generation;
    p_ble->b_pending          = true;
    return 0;
}

/** @brief Stage a repeatable ATT payload without advancing on SDK failure. */
int netble_fragment (netble_t * p_ble,
                     uint32_t   generation,
                     uint16_t   mtu,
                     uint8_t *  p_output,
                     size_t     capacity,
                     size_t *   p_size)
{
    size_t payload = 0U;
    size_t offset  = 0U;
    size_t count   = 0U;
    size_t length  = 0U;
    if ((NULL == p_ble) || (NULL == p_output) || (NULL == p_size) ||
        (23U > mtu) || (517U < mtu))
    {
        return -1;
    }
    *p_size = 0U;
    payload = mtu - 3U - UTHOME_BLE_FRAGMENT_HEADER;
    if ((p_ble->b_active) && (p_ble->active_generation != generation))
    {
        p_ble->b_active   = false;
        p_ble->b_fragment = false;
    }
    if ((p_ble->b_pending) && (p_ble->pending_generation != generation))
    {
        p_ble->b_pending = false;
    }
    if ((!p_ble->b_active) && (!p_ble->b_pending))
    {
        return 1;
    }
    if ((p_ble->b_active) && (payload != p_ble->payload_bytes))
    {
        return -1;
    }
    offset = p_ble->b_active ? (size_t)p_ble->index * payload : 0U;
    if (UTHOME_BLE_FRAME <= offset)
    {
        return -1;
    }
    length = UTHOME_BLE_FRAME - offset;
    if (payload < length)
    {
        length = payload;
    }
    if (capacity < (length + UTHOME_BLE_FRAGMENT_HEADER))
    {
        return -1;
    }
    if (!p_ble->b_active)
    {
        memcpy(p_ble->active, p_ble->pending, UTHOME_BLE_FRAME);
        p_ble->active_generation = p_ble->pending_generation;
        p_ble->payload_bytes     = (uint16_t)payload;
        p_ble->index             = 0U;
        p_ble->b_pending         = false;
        p_ble->b_active          = true;
        ++p_ble->sequence;
    }
    count       = (UTHOME_BLE_FRAME + payload - 1U) / payload;
    p_output[0] = 1U;
    p_output[1] = 1U;
    p_output[2] = (uint8_t)(p_ble->sequence & 0xffU);
    p_output[3] = (uint8_t)(p_ble->sequence >> 8U);
    p_output[4] = p_ble->index;
    p_output[5] = (uint8_t)count;
    p_output[6] = UTHOME_BLE_FRAME;
    p_output[7] = 0U;
    memcpy(&p_output[8], &p_ble->active[offset], length);
    *p_size           = length + UTHOME_BLE_FRAGMENT_HEADER;
    p_ble->b_fragment = true;
    return 0;
}

/** @brief Advance only after the transport successfully queues a notification.
 */
int netble_sent (netble_t * p_ble)
{
    if ((NULL == p_ble) || (!p_ble->b_active) || (!p_ble->b_fragment))
    {
        return -1;
    }
    ++p_ble->index;
    p_ble->b_fragment = false;
    if (((size_t)p_ble->index * p_ble->payload_bytes) >= UTHOME_BLE_FRAME)
    {
        p_ble->b_active = false;
    }
    return 0;
}

/** @brief Encode typed measurement fields with defined signed conversions. */
static int values (sampcache_entry_t const * p_entry, uint8_t * p_output)
{
    const sampcache_value_t * p_value = &p_entry->value;
    if (UTHOME_SAMPLE_ENVIRONMENT == p_entry->source.type)
    {
        uint32_t pressure    = 0U;
        uint32_t temperature = 0U;
        if ((!isfinite(p_value->environment.pressure_pa)) ||
            (!isfinite(p_value->environment.temperature_c)))
        {
            return -1;
        }
        memcpy(&pressure, &p_value->environment.pressure_pa, sizeof(pressure));
        memcpy(&temperature,
               &p_value->environment.temperature_c,
               sizeof(temperature));
        word(p_output, pressure);
        word(&p_output[4], temperature);
    }
    else if (UTHOME_SAMPLE_DISTANCE == p_entry->source.type)
    {
        word(p_output, p_value->distance.distance_mm);
        word(&p_output[4], p_value->distance.echo_us);
    }
    else if (UTHOME_SAMPLE_LIGHT == p_entry->source.type)
    {
        if (UINT32_C(200000000) < p_value->light.millilux)
        {
            return -1;
        }
        word(p_output, p_value->light.millilux);
    }
    else if (UTHOME_SAMPLE_HEALTH == p_entry->source.type)
    {
        if (UTHOME_HEALTH_EVENT_COUNT <= p_value->health.event)
        {
            return -1;
        }
        word(p_output, p_value->health.event);
        word(&p_output[4], p_value->health.occurrences);
    }
    else
    {
        for (size_t index = 0U; index < UTHOME_SAMPLE_MAX_TARGETS; ++index)
        {
            const sample_target_t * p_target = &p_value->targets.targets[index];
            uint8_t *               p_slot   = &p_output[index * 12U];
            if (p_target->b_present)
            {
                word(p_slot, 1U | ((uint32_t)p_target->resolution_mm << 16U));
                word(&p_slot[4],
                     (uint16_t)p_target->x_mm |
                         ((uint32_t)(uint16_t)p_target->y_mm << 16U));
                word(&p_slot[8], (uint16_t)p_target->speed_cm_s);
            }
        }
    }
    return 0;
}

/** @brief Emit one little-endian word without unaligned integer stores. */
static void word (uint8_t * p_output, uint32_t value)
{
    for (size_t index = 0U; index < 4U; ++index)
    {
        p_output[index] = (uint8_t)((value >> (index * 8U)) & 0xffU);
    }
}

/** @brief Preserve the full boot and measurement timestamp ranges. */
static void wide (uint8_t * p_output, uint64_t value)
{
    word(p_output, (uint32_t)(value & UINT32_MAX));
    word(&p_output[4], (uint32_t)(value >> 32U));
}

/* End of file. */

