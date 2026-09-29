/** @file statusmon.h
 * @brief statusmon service contract. */
#ifndef UTHOME_STATUSMON_H
#define UTHOME_STATUSMON_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint64_t checked_ms;
    uint64_t reported_ms;
    uint32_t interval_ms;
    uint32_t sequence;
    uint8_t  queue_mask;
    bool     b_ready;
} statusmon_t;

/* Owner only. Configure before workers; copied settings must be restored on
 * rollback. Call step regularly. It checks heartbeats every 100 ms and reports
 * the last historical event at the configured interval, never inventing a
 * 'healthy' event. A successful record describes the report, not node health.
 * No restart, heap, task or output control. Failed shared calls retry later. */
int statusmon_configure (statusmon_t * p_monitor,
                         uint32_t      interval_ms,
                         uint8_t       queue_mask,
                         uint64_t      now_ms);
int statusmon_step (statusmon_t * p_monitor, uint64_t now_ms);

#endif
/* End of file. */

