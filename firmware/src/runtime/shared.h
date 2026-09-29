/**
 * @file shared.h
 * @brief Serialize target queue and health access through the worker port.
 */
#ifndef UTHOME_SHARED_H
#define UTHOME_SHARED_H

#include "uthome/msgbus.h"
#include "uthome/health.h"
#include "sampcache.h"

typedef struct
{
    sampcache_t           samples;
    health_snapshot_t     health;
    msgbus_queue_config_t queues[UTHOME_MSGBUS_MAX_QUEUES];
    msgbus_stats_t        stats[UTHOME_MSGBUS_MAX_QUEUES];
    size_t                queue_count;
    uint64_t              timestamp_ms;
} shared_capture_t;

/* Owner registers copied source metadata before worker release. Capture copies
 * samples, queue counters, health and time in one guard; no queue is consumed.
 * Output is usable only on success; BUSY requires a later retry. */
int shared_expect (const sampcache_source_t * p_source);
int shared_capture (shared_capture_t * p_capture);
/* Display observation: rotate configured sources every 2 seconds. Copy one
 * entry and its capture time under the guard; never consume a queue. Return
 * 0 selected, 1 no source, negative error/BUSY (output then unusable). */
/* Rule observation uses an exact instance rather than a rotating page. */
int shared_source (uint16_t            source_index,
                   sampcache_entry_t * p_entry,
                   uint64_t *          p_now);
int shared_sample (uint8_t mask, sampcache_entry_t * p_entry, uint64_t * p_now);

/* Reset requires the controller and zero owned workers. Queue handles are
 * array indices and valid only for this plan. Raw msgbus/health/watchdog APIs
 * must not run concurrently with these wrappers. Calls can return the port's
 * ERR_BUSY; retry later, never spin in a higher-priority task. */
int shared_reset (const msgbus_queue_config_t * p_queues, size_t count);
int shared_publish (msgbus_queue_id_t h_queue,
                    msgbus_type_t     type,
                    const void *      p_item,
                    size_t            size);
int shared_consume (msgbus_queue_id_t h_queue,
                    msgbus_type_t     type,
                    void *            p_item,
                    size_t            capacity);
int shared_queue_stats (msgbus_queue_id_t h_queue, msgbus_stats_t * p_stats);
int shared_report (health_event_t event);
int shared_snapshot (health_snapshot_t * p_snapshot);
int shared_watch (const char * p_name, uint32_t timeout_ms);
int shared_heartbeat (const char * p_name);
int shared_check (void);

#endif

/* End of file. */

