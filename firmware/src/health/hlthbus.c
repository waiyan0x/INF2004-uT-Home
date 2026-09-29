/**
 * @file hlthbus.c
 * @brief Translate queue drop counters into rate-limited health events.
 */
#include "uthome/hlthbus.h"
#include "uthome/msgbus.h"

/**
 * @brief Report newly observed queue drops through the health service.
 * @param h_queue Handle of the queue to inspect or operate on.
 * @param now_ms Monotonic time in milliseconds with unsigned wraparound
 * semantics.
 * @param p_previous_drop_count Caller-owned last observed drop count, updated
 * after a new drop.
 * @return Health success, rate-limit indication, or validation error as
 * applicable.
 */
int hlthbus_check_queue (msgbus_queue_id_t h_queue,
                         uint32_t          now_ms,
                         uint32_t *        p_previous_drop_count)
{
    if (NULL == p_previous_drop_count)
    {
        return UTHOME_HEALTH_ERR_INVALID;
    }

    msgbus_stats_t stats = {0U};

    if (UTHOME_MSGBUS_OK != msgbus_get_stats(h_queue, &stats))
    {
        return UTHOME_HEALTH_ERR_INVALID;
    }

    /*
     * A higher dropped_count means at least one
     * queue overflow occurred since the previous
     * health check.
     */
    if (stats.dropped_count > *p_previous_drop_count)
    {
        *p_previous_drop_count = stats.dropped_count;

        return health_report(UTHOME_HEALTH_QUEUE_OVERFLOW, now_ms);
    }

    return UTHOME_HEALTH_OK;
}

/* End of file. */

