/** @file sampcache.h
 * @brief sampcache service contract. */
#ifndef UTHOME_SAMPCACHE_H
#define UTHOME_SAMPCACHE_H

#include "uthome/sample.h"
#include "uthome/msgbus.h"
#define UTHOME_SAMPCACHE_SOURCES 8U
#define UTHOME_SAMPCACHE_ENTRIES 12U

typedef union
{
    sample_environment_t   environment;
    sample_target_report_t targets;
    sample_distance_t      distance;
    sample_health_t        health;
} sampcache_value_t;

typedef struct
{
    char              name[32];
    msgbus_queue_id_t h_queue;
    msgbus_type_t     type;
    uint16_t          source_index;
    uint32_t          stale_ms;
} sampcache_source_t;

typedef struct
{
    sampcache_source_t source;
    sample_header_t    header;
    sampcache_value_t  value;
    bool               b_expected;
    bool               b_seen;
} sampcache_entry_t;

typedef struct
{
    sampcache_entry_t entries[UTHOME_SAMPCACHE_ENTRIES];
} sampcache_t;

/* Raw cache calls require the same serialization as msgbus. Production callers
 * use shared.h. Reset and expect run before workers. Record ignores generic
 * queue payloads and unregistered sources; it never consumes queue entries.
 * Native sample objects must obey sample.h (including valid bool objects).
 * Reset invalidates all samples; readers must associate copies with a plan.
 */
void sampcache_reset (void);
int sampcache_expect (const sampcache_source_t * p_source);
void sampcache_record (msgbus_queue_id_t h_queue,
                       msgbus_type_t     type,
                       const void *      p_data,
                       size_t            size,
                       uint64_t          now_ms);
int sampcache_copy (sampcache_t * p_cache);
/* Quality: 0 missing, 1 fresh OK, 2 stale OK, 3 fault/unknown. */
uint32_t sampcache_quality (const sampcache_entry_t * p_entry, uint64_t now_ms);
size_t sampcache_size (msgbus_type_t type);
/* Select page modulo the number of configured sources on the masked queues.
 * Return 0 selected, 1 no source, -1 invalid. Copy only one entry; no dequeue.
 */
int sampcache_select (uint8_t mask, uint64_t page, sampcache_entry_t * p_entry);

/* Copy a particular registered device, without consuming queued samples. */
int sampcache_source (uint16_t source_index, sampcache_entry_t * p_entry);
#endif
/* End of file. */

