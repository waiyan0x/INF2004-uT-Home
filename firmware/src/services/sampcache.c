/** @file sampcache.c
 * @brief sampcache bounded implementation. */
#include "sampcache.h"

#include "uthome/health.h"
#include <math.h>
#include <string.h>
static sampcache_t g_cache = {0};
static bool valid_value (msgbus_type_t type, const sampcache_value_t * p_value);

/** @brief Clear expectations as well as measurements at each plan boundary. */
void sampcache_reset (void)
{
    memset(&g_cache, 0, sizeof(g_cache));
}

/** @brief Return native sizes only for the explicitly supported sample types.
 */
size_t sampcache_size (msgbus_type_t type)
{
    const size_t sizes[] = {0U,
                            sizeof(sample_environment_t),
                            sizeof(sample_target_report_t),
                            sizeof(sample_distance_t),
                            sizeof(sample_health_t)};
    return (type <= UTHOME_SAMPLE_HEALTH) ? sizes[type] : 0U;
}

/** @brief Enroll one expected source, including an explicit missing state. */
int sampcache_expect (const sampcache_source_t * p_source)
{
    /* Short-circuit rejects negative handles before the uint32 conversion. */
    if ((NULL == p_source) || (p_source->h_queue < 0) ||
        ((uint32_t)p_source->h_queue >= UTHOME_MSGBUS_MAX_QUEUES) ||
        (0U == sampcache_size(p_source->type)) || (0U == p_source->stale_ms) ||
        ('\0' == p_source->name[0]) ||
        (NULL == memchr(p_source->name, '\0', sizeof(p_source->name))))
    {
        return -1;
    }
    size_t index = p_source->source_index;
    if (UTHOME_SAMPLE_NO_SOURCE == index)
    {
        if (UTHOME_SAMPLE_HEALTH != p_source->type)
        {
            return -1;
        }
        /* Queue validity above bounds this nonnegative conversion to 0..3. */
        index = UTHOME_SAMPCACHE_SOURCES + (size_t)p_source->h_queue;
    }
    else if (index >= UTHOME_SAMPCACHE_SOURCES)
    {
        return -1;
    }
    else
    {
        /* Device source indices already select their own cache slot. */
    }
    if (g_cache.entries[index].b_expected)
    {
        return -1;
    }
    g_cache.entries[index].source     = *p_source;
    g_cache.entries[index].b_expected = true;
    return 0;
}

/** @brief Observe only successful publications, preserving per-source order. */
void sampcache_record (msgbus_queue_id_t h_queue,
                       msgbus_type_t     type,
                       const void *      p_data,
                       size_t            size,
                       uint64_t          now_ms)
{
    if ((NULL == p_data) || (0U == size) || (sampcache_size(type) != size))
    {
        return;
    }
    sample_header_t header = {0};
    memcpy(&header, p_data, sizeof(header));
    for (size_t index = 0U; index < UTHOME_SAMPCACHE_ENTRIES; ++index)
    {
        sampcache_entry_t * p_entry = &g_cache.entries[index];
        if ((!p_entry->b_expected) || (p_entry->source.h_queue != h_queue) ||
            (p_entry->source.type != type) ||
            (p_entry->source.source_index != header.source_index))
        {
            continue;
        }
        const uint32_t advance = header.sequence - p_entry->header.sequence;
        if ((p_entry->b_seen) &&
            ((0U == advance) || (advance > UINT32_C(0x7FFFFFFF)) ||
             (header.timestamp_ms < p_entry->header.timestamp_ms)))
        {
            return;
        }
        memset(&p_entry->value, 0, sizeof(p_entry->value));
        memcpy(&p_entry->value, p_data, size);
        if ((header.timestamp_ms > now_ms) ||
            (header.status > UTHOME_SAMPLE_INVALID) ||
            ((UTHOME_SAMPLE_OK == header.status) &&
             (!valid_value(type, &p_entry->value))))
        {
            header.status       = UTHOME_SAMPLE_INVALID;
            header.timestamp_ms = now_ms;
        }
        if (UTHOME_SAMPLE_OK != header.status)
        {
            memset(&p_entry->value, 0, sizeof(p_entry->value));
        }
        p_entry->header = header;
        p_entry->b_seen = true;
        return;
    }
}

/** @brief Copy all entries under the caller's existing bounded guard. */
int sampcache_copy (sampcache_t * p_cache)
{
    if (NULL == p_cache)
    {
        return -1;
    }
    *p_cache = g_cache;
    return 0;
}

/** @brief Never treat missing, invalid or backward-clock data as usable. */
uint32_t sampcache_quality (const sampcache_entry_t * p_entry, uint64_t now_ms)
{
    if ((NULL == p_entry) || (!p_entry->b_expected) || (!p_entry->b_seen))
    {
        return 0U;
    }
    if ((UTHOME_SAMPLE_OK != p_entry->header.status) ||
        (now_ms < p_entry->header.timestamp_ms))
    {
        return 3U;
    }
    return ((now_ms - p_entry->header.timestamp_ms) >= p_entry->source.stale_ms)
               ? 2U
               : 1U;
}

/** @brief Select an exact instance; never substitute another queue producer. */
int sampcache_source (uint16_t source_index, sampcache_entry_t * p_entry)
{
    if ((NULL == p_entry) || (source_index >= UTHOME_SAMPCACHE_SOURCES))
    {
        return -1;
    }
    memset(p_entry, 0, sizeof(*p_entry));
    if (!g_cache.entries[source_index].b_expected)
    {
        return 1;
    }
    *p_entry = g_cache.entries[source_index];
    return 0;
}

/** @brief Select one source without allocating another whole-cache buffer. */
int sampcache_select (uint8_t mask, uint64_t page, sampcache_entry_t * p_entry)
{
    if ((NULL == p_entry) || (0U == mask) || (mask > 15U))
    {
        return -1;
    }
    size_t indices[UTHOME_SAMPCACHE_ENTRIES] = {0};
    size_t count                             = 0U;
    memset(p_entry, 0, sizeof(*p_entry));
    for (size_t index = 0U; index < UTHOME_SAMPCACHE_ENTRIES; ++index)
    {
        const sampcache_entry_t * p_source = &g_cache.entries[index];
        if ((p_source->b_expected) &&
            (0U != (mask & (UINT32_C(1) << p_source->source.h_queue))))
        {
            indices[count++] = index;
        }
    }
    if (0U == count)
    {
        return 1;
    }
    /* The remainder is smaller than the 12-entry table on every platform. */
    *p_entry = g_cache.entries[indices[(size_t)(page % count)]];
    return 0;
}

/** @brief Check semantic values without interpreting native padding bytes. */
static bool valid_value (msgbus_type_t type, const sampcache_value_t * p_value)
{
    if (UTHOME_SAMPLE_ENVIRONMENT == type)
    {
        return (isfinite(p_value->environment.pressure_pa)) &&
               (isfinite(p_value->environment.temperature_c));
    }
    if (UTHOME_SAMPLE_HEALTH == type)
    {
        return p_value->health.event < UTHOME_HEALTH_EVENT_COUNT;
    }
    if (UTHOME_SAMPLE_TARGETS == type)
    {
        for (size_t index = 0U; index < UTHOME_SAMPLE_MAX_TARGETS; ++index)
        {
            const sample_target_t * p_target = &p_value->targets.targets[index];
            if ((!p_target->b_present) &&
                ((0 != p_target->x_mm) || (0 != p_target->y_mm) ||
                 (0 != p_target->speed_cm_s) ||
                 (0U != p_target->resolution_mm)))
            {
                return false;
            }
        }
    }
    return true;
}

/* End of file. */

