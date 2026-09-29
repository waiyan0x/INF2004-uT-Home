/** @file statusmon.c
 * @brief statusmon bounded implementation. */
#include "statusmon.h"

#include "shared.h"
#include <string.h>

/** @brief Register global health destinations before activation releases tasks.
 */
int statusmon_configure (statusmon_t * p_monitor,
                         uint32_t      interval_ms,
                         uint8_t       queue_mask,
                         uint64_t      now_ms)
{
    if ((NULL == p_monitor) || (0U == interval_ms) || (interval_ms > 60000U) ||
        (queue_mask > 15U))
    {
        return -1;
    }
    *p_monitor = (statusmon_t){.interval_ms = interval_ms,
                               .queue_mask  = queue_mask,
                               .checked_ms  = now_ms,
                               .reported_ms = now_ms};
    for (uint8_t index = 0U; index < UTHOME_MSGBUS_MAX_QUEUES; ++index)
    {
        if (0U != (queue_mask & (UINT32_C(1) << index)))
        {
            const sampcache_source_t source = {
                .name         = "health",
                .h_queue      = index,
                .type         = UTHOME_SAMPLE_HEALTH,
                .source_index = UTHOME_SAMPLE_NO_SOURCE,
                .stale_ms     = (2U * interval_ms) + 100U};
            if (0 != shared_expect(&source))
            {
                return -1;
            }
        }
    }
    p_monitor->b_ready = true;
    return 0;
}

/** @brief Supervise progress independently of whether a USB client is present.
 */
int statusmon_step (statusmon_t * p_monitor, uint64_t now_ms)
{
    if ((NULL == p_monitor) || (!p_monitor->b_ready))
    {
        return -1;
    }
    if ((now_ms < p_monitor->checked_ms) || (now_ms < p_monitor->reported_ms))
    {
        return -1;
    }
    if ((now_ms - p_monitor->checked_ms) >= 100U)
    {
        const int result = shared_check();
        if (result < 0)
        {
            return result;
        }
        p_monitor->checked_ms = now_ms;
    }
    if ((now_ms - p_monitor->reported_ms) < p_monitor->interval_ms)
    {
        return 0;
    }
    health_snapshot_t health = {0};
    const int         result = shared_snapshot(&health);
    if (0 != result)
    {
        return result;
    }
    sample_health_t sample = {
        .header = {.timestamp_ms = now_ms,
                   .sequence     = p_monitor->sequence++,
                   .source_index = UTHOME_SAMPLE_NO_SOURCE,
                   .status       = UTHOME_SAMPLE_OK}};
    if (health.b_has_last_event)
    {
        /* The stable health enum has eight nonnegative values. */
        sample.event       = (uint16_t)health.last_event;
        sample.occurrences = health.event_count[health.last_event];
        for (uint8_t index = 0U; index < UTHOME_MSGBUS_MAX_QUEUES; ++index)
        {
            if (0U != (p_monitor->queue_mask & (UINT32_C(1) << index)))
            {
                const int published = shared_publish(
                    index, UTHOME_SAMPLE_HEALTH, &sample, sizeof(sample));
                if (0 != published)
                {
                    return published;
                }
            }
        }
    }
    p_monitor->reported_ms = now_ms;
    return 0;
}

/* End of file. */

