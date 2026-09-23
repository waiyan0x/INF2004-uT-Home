#include "uthome/health.h"
#include "uthome/msgbus.h"


int uthome_health_check_queue(
    uthome_msgbus_queue_id_t queue_id,
    uint32_t now_ms,
    uint32_t *previous_drop_count)
{
    if (previous_drop_count == NULL) {
        return UTHOME_HEALTH_ERR_INVALID;
    }

    uthome_msgbus_stats_t stats;

    if (uthome_msgbus_get_stats(
            queue_id,
            &stats
        ) != UTHOME_MSGBUS_OK) {

        return UTHOME_HEALTH_ERR_INVALID;
    }

    /*
     * A higher dropped_count means at least one
     * queue overflow occurred since the previous
     * health check.
     */
    if (stats.dropped_count >
        *previous_drop_count) {

        *previous_drop_count =
            stats.dropped_count;

        return uthome_health_report(
            UTHOME_HEALTH_QUEUE_OVERFLOW,
            now_ms
        );
    }

    return UTHOME_HEALTH_OK;
}