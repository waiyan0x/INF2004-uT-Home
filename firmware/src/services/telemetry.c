/** @file telemetry.c
 * @brief telemetry bounded implementation. */
#include "telemetry.h"

#include <float.h>
#include <string.h>
#if (FLT_RADIX != 2) || (FLT_MANT_DIG != 24) || (FLT_MAX_EXP != 128)
#error Telemetry requires binary32 float representation
#endif
static void encode_header (const telemetry_t * p_context,
                           uint32_t            result,
                           uint8_t *           p_data);
static void encode_queue (const shared_capture_t * p_capture,
                          size_t                   index,
                          uint8_t *                p_data);
static void encode_sample (const sampcache_entry_t * p_entry,
                           uint64_t                  now_ms,
                           uint8_t *                 p_data);
static void encode_value (const sampcache_entry_t * p_entry, uint8_t * p_data);
static void put_float (uint8_t * p_data, float value);
static void put_time (uint8_t * p_data, uint64_t value);
static void encode_audit (telemetry_t * p_context, uint8_t * p_data);

/** @brief Return a bounded versioned observation, including explicit failures.
 */
int telemetry_observe (void *    p_context,
                       uint8_t * p_data,
                       size_t    capacity,
                       size_t *  p_size)
{
    telemetry_t * p_state = p_context;
    if ((NULL == p_state) || (NULL == p_state->p_control) ||
        (!p_state->p_control->b_initialized) || (NULL == p_data) ||
        (NULL == p_size) || (capacity < UTHOME_TELEMETRY_MAX) ||
        (sizeof(float) != sizeof(uint32_t)) || (!rt_port_is_owner()))
    {
        return -1;
    }
    memset(p_data, 0, UTHOME_TELEMETRY_MAX);
    memset(&p_state->capture, 0, sizeof(p_state->capture));
    memset(&p_state->runtime, 0, sizeof(p_state->runtime));
    int result = activate_get_status(&p_state->runtime);
    if (0 == result)
    {
        result = shared_capture(&p_state->capture);
    }
    const uint32_t status =
        (0 == result) ? 0U : ((UTHOME_PORT_ERR_BUSY == result) ? 1U : 2U);
    encode_header(p_state, status, p_data);
    *p_size = UTHOME_TELEMETRY_HEADER;
    if (0U != status)
    {
        return 0;
    }
    encode_audit(p_state, p_data);
    const shared_capture_t * p_capture = &p_state->capture;
    for (size_t index = 0U; index < p_capture->queue_count; ++index)
    {
        encode_queue(p_capture, index, &p_data[*p_size]);
        *p_size += UTHOME_TELEMETRY_QUEUE;
    }
    uint32_t count = 0U;
    for (size_t index = 0U; index < UTHOME_SAMPCACHE_ENTRIES; ++index)
    {
        const sampcache_entry_t * p_entry = &p_capture->samples.entries[index];
        if (p_entry->b_expected)
        {
            encode_sample(p_entry, p_capture->timestamp_ms, &p_data[*p_size]);
            *p_size += UTHOME_TELEMETRY_SAMPLE;
            ++count;
        }
    }
    cfgbytes_put(&p_data[56], count);
    return 0;
}

/** @brief Encode metadata without making successful-health claims. */
static void
encode_header (const telemetry_t * p_context, uint32_t result, uint8_t * p_data)
{
    const cfgctrl_t * p_control = p_context->p_control;
    cfgbytes_put(p_data, 3U);
    cfgbytes_put(&p_data[4], result);
    if (0U != result)
    {
        return;
    }
    const shared_capture_t *  p_capture = &p_context->capture;
    const health_snapshot_t * p_health  = &p_capture->health;
    const activate_status_t * p_runtime = &p_context->runtime;
    put_time(&p_data[8], p_capture->timestamp_ms);
    cfgbytes_put(&p_data[16], p_control->active_revision);
    cfgbytes_put(&p_data[20], p_control->saved_revision);
    cfgbytes_put(&p_data[24], p_control->active_crc);
    cfgbytes_put(&p_data[28], p_control->saved_crc);
    cfgbytes_put(&p_data[32],
                 (p_control->b_saved_known ? UTHOME_CFG_DURABLE_KNOWN : 0U) |
                     (p_control->b_recovery ? UTHOME_CFG_RECOVERY : 0U) |
                     ((0U != p_control->active_revision)
                          ? UTHOME_CFG_ACTIVE_KNOWN
                          : 0U));
    /* Enums, counts and size_t fields are bounded by the fixed runtime tables.
     * Negative errors use the C-defined modulo uint32 representation on wire.
     */
    cfgbytes_put(&p_data[36], (uint32_t)p_runtime->state);
    cfgbytes_put(&p_data[40], p_runtime->generation);
    cfgbytes_put(&p_data[44], (uint32_t)p_runtime->failed_workers);
    cfgbytes_put(&p_data[48], (uint32_t)p_runtime->retained_instances);
    cfgbytes_put(&p_data[52], (uint32_t)p_context->service_error);
    cfgbytes_put(&p_data[60], (uint32_t)p_capture->queue_count);
    cfgbytes_put(&p_data[64], p_health->total_events);
    cfgbytes_put(&p_data[68], p_health->total_suppressed);
    cfgbytes_put(&p_data[72], p_health->b_has_last_event ? 1U : 0U);
    cfgbytes_put(&p_data[76], (uint32_t)p_health->last_event);
    cfgbytes_put(&p_data[80], p_health->last_event_time_ms);
    cfgbytes_put(&p_data[84], UTHOME_HEALTH_EVENT_COUNT);
    for (size_t index = 0U; index < UTHOME_HEALTH_EVENT_COUNT; ++index)
    {
        cfgbytes_put(&p_data[88U + (index * 4U)], p_health->event_count[index]);
        cfgbytes_put(&p_data[120U + (index * 4U)],
                     p_health->suppressed_count[index]);
    }
    cfgbytes_put(&p_data[152], (uint32_t)p_runtime->apply_error);
    cfgbytes_put(&p_data[156], (uint32_t)p_runtime->recovery_error);
}

/** @brief Publish allocation evidence only when its provider succeeds. */
static void encode_audit (telemetry_t * p_context, uint8_t * p_data)
{
    memaudit_t * p_audit = &p_context->audit;
    if ((NULL == p_context->p_audit) || (0 != p_context->p_audit(p_audit)))
    {
        return;
    }
    cfgbytes_put(&p_data[160],
                 1U | (p_audit->b_sealed ? 2U : 0U) |
                     (p_audit->b_saturated ? 4U : 0U));
    cfgbytes_put(&p_data[168], p_audit->startup.allocations);
    cfgbytes_put(&p_data[172], p_audit->startup.bytes);
    cfgbytes_put(&p_data[176], p_audit->startup.frees);
    cfgbytes_put(&p_data[180], p_audit->normal.allocations);
    cfgbytes_put(&p_data[184], p_audit->normal.bytes);
    cfgbytes_put(&p_data[188], p_audit->normal.frees);
    for (size_t index = 0U; index < UTHOME_AUDIT_KINDS; ++index)
    {
        cfgbytes_put(&p_data[192U + (index * 4U)],
                     p_audit->startup.creates[index]);
        cfgbytes_put(&p_data[236U + (index * 4U)],
                     p_audit->normal.creates[index]);
    }
    for (size_t index = 0U; index < UTHOME_AUDIT_ALLOCATORS; ++index)
    {
        cfgbytes_put(&p_data[288U + (index * 4U)],
                     p_audit->startup.kernel_calls[index]);
        cfgbytes_put(&p_data[304U + (index * 4U)],
                     p_audit->normal.kernel_calls[index]);
    }
    cfgbytes_put(&p_data[320], p_audit->b_application ? 1U : 0U);
    cfgbytes_put(&p_data[324], p_audit->application.allocations);
    cfgbytes_put(&p_data[328], p_audit->application.bytes);
    cfgbytes_put(&p_data[332], p_audit->application.frees);
    for (size_t index = 0U; index < UTHOME_AUDIT_ALLOCATORS; ++index)
    {
        cfgbytes_put(&p_data[336U + (index * 4U)],
                     p_audit->application.kernel_calls[index]);
    }
}

/** @brief Serialize bounded queue counters; native size_t never crosses USB. */
static void encode_queue (const shared_capture_t * p_capture,
                          size_t                   index,
                          uint8_t *                p_data)
{
    const msgbus_stats_t * p_stats = &p_capture->stats[index];
    /* Index <=3 and depths <=8 fit the wire's uint32 fields. */
    cfgbytes_put(p_data, (uint32_t)index);
    cfgbytes_put(&p_data[4], p_capture->queues[index].type);
    cfgbytes_put(&p_data[8], p_stats->published_count);
    cfgbytes_put(&p_data[12], p_stats->consumed_count);
    cfgbytes_put(&p_data[16], p_stats->dropped_count);
    cfgbytes_put(&p_data[20], (uint32_t)p_stats->current_depth);
    cfgbytes_put(&p_data[24], (uint32_t)p_stats->high_water_mark);
}

/** @brief Preserve identity and quality even when measurement values are
 * absent. */
static void encode_sample (const sampcache_entry_t * p_entry,
                           uint64_t                  now_ms,
                           uint8_t *                 p_data)
{
    const sample_header_t * p_header = &p_entry->header;
    uint64_t                age      = UINT32_MAX;
    if ((p_entry->b_seen) && (now_ms >= p_header->timestamp_ms))
    {
        age = now_ms - p_header->timestamp_ms;
    }
    /* Queue handles are checked nonnegative; saturated ages fit uint32. */
    cfgbytes_put(p_data, (uint32_t)p_entry->source.h_queue);
    cfgbytes_put(&p_data[4], p_entry->source.type);
    cfgbytes_put(&p_data[8], p_entry->source.source_index);
    cfgbytes_put(&p_data[12], sampcache_quality(p_entry, now_ms));
    put_time(&p_data[16], p_header->timestamp_ms);
    cfgbytes_put(&p_data[24], p_header->sequence);
    cfgbytes_put(&p_data[28], p_header->status);
    cfgbytes_put(&p_data[32], p_entry->source.stale_ms);
    cfgbytes_put(&p_data[36], (age < UINT32_MAX) ? (uint32_t)age : UINT32_MAX);
    memcpy(&p_data[40], p_entry->source.name, strlen(p_entry->source.name));
    if ((p_entry->b_seen) && (UTHOME_SAMPLE_OK == p_header->status))
    {
        encode_value(p_entry, &p_data[72]);
    }
}

/** @brief Encode each measurement explicitly, including signed target fields.
 */
static void encode_value (const sampcache_entry_t * p_entry, uint8_t * p_data)
{
    const sampcache_value_t * p_value = &p_entry->value;
    if (UTHOME_SAMPLE_ENVIRONMENT == p_entry->source.type)
    {
        put_float(p_data, p_value->environment.pressure_pa);
        put_float(&p_data[4], p_value->environment.temperature_c);
    }
    else if (UTHOME_SAMPLE_DISTANCE == p_entry->source.type)
    {
        cfgbytes_put(p_data, p_value->distance.distance_mm);
        cfgbytes_put(&p_data[4], p_value->distance.echo_us);
    }
    else if (UTHOME_SAMPLE_HEALTH == p_entry->source.type)
    {
        cfgbytes_put(p_data, p_value->health.event);
        cfgbytes_put(&p_data[4], p_value->health.occurrences);
    }
    else
    {
        for (size_t index = 0U; index < UTHOME_SAMPLE_MAX_TARGETS; ++index)
        {
            const sample_target_t * p_target = &p_value->targets.targets[index];
            uint8_t *               p_slot   = &p_data[index * 12U];
            /* Signed int16 conversion to uint16 is defined modulo 65536;
             * widen before unsigned shifts to retain the exact wire bits. */
            cfgbytes_put(p_slot,
                         (p_target->b_present ? 1U : 0U) |
                             ((uint32_t)p_target->resolution_mm << 16U));
            cfgbytes_put(&p_slot[4],
                         (uint16_t)p_target->x_mm |
                             ((uint32_t)(uint16_t)p_target->y_mm << 16U));
            cfgbytes_put(&p_slot[8], (uint16_t)p_target->speed_cm_s);
        }
    }
}

/** @brief Copy binary32 bits without aliasing through an incompatible pointer.
 */
static void put_float (uint8_t * p_data, float value)
{
    uint32_t bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    cfgbytes_put(p_data, bits);
}

/** @brief Split a uint64 value into two explicitly little-endian words. */
static void put_time (uint8_t * p_data, uint64_t value)
{
    /* Mask/shift bound both halves to uint32 without losing timestamp bits. */
    cfgbytes_put(p_data, (uint32_t)(value & UINT32_MAX));
    cfgbytes_put(&p_data[4], (uint32_t)(value >> 32U));
}

/* End of file. */

