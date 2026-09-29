/**
 * @file shared.c
 * @brief Keep bounded queue, health and heartbeat operations mutually
 * exclusive.
 */
#include "shared.h"
#include <string.h>

#include "workport.h"
#include "uthome/watchdog.h"

typedef enum
{
    SHARED_RESET,
    SHARED_PUBLISH,
    SHARED_CONSUME,
    SHARED_QUEUE_STATS,
    SHARED_REPORT,
    SHARED_SNAPSHOT,
    SHARED_WATCH,
    SHARED_HEARTBEAT,
    SHARED_CHECK,
    SHARED_EXPECT,
    SHARED_CAPTURE,
    SHARED_SAMPLE,
    SHARED_SOURCE
} shared_operation_t;

typedef struct
{
    shared_operation_t            operation;
    const msgbus_queue_config_t * p_queues;
    size_t                        size;
    msgbus_queue_id_t             h_queue;
    msgbus_type_t                 type;
    const void *                  p_input;
    void *                        p_output;
    health_event_t                event;
    const char *                  p_name;
    uint32_t                      timeout_ms;
    uint64_t *                    p_time;
} shared_request_t;

static msgbus_queue_config_t g_queues[UTHOME_MSGBUS_MAX_QUEUES] = {0};
static size_t                g_queue_count                      = 0U;
static int capture (shared_capture_t * p_capture, uint64_t now_ms);
static int publish (const shared_request_t * p_request, uint64_t now_ms);
static int expect (const sampcache_source_t * p_source);
static int perform (void * p_context);
static int reset_queues (const shared_request_t * p_request);

/** @brief Reset the plan's shared state only after all workers are released. */
int shared_reset (const msgbus_queue_config_t * p_queues, size_t count)
{
    size_t owned = 0U;
    if ((0 != rt_port_owned_count(&owned)) || (0U != owned) ||
        (count > UTHOME_MSGBUS_MAX_QUEUES) ||
        ((0U != count) && (NULL == p_queues)))
    {
        return -1;
    }
    for (size_t index = 0U; index < count; ++index)
    {
        const msgbus_queue_config_t * p_queue = &p_queues[index];
        if ((0U == p_queue->item_size) ||
            (p_queue->item_size > UTHOME_MSGBUS_MAX_ITEM_SIZE) ||
            (0U == p_queue->depth) ||
            (p_queue->depth > UTHOME_MSGBUS_MAX_DEPTH) ||
            ((UTHOME_MSGBUS_DROP_OLDEST != p_queue->overflow_policy) &&
             (UTHOME_MSGBUS_REJECT_NEWEST != p_queue->overflow_policy)))
        {
            return -1;
        }
    }
    shared_request_t request = {
        .operation = SHARED_RESET, .p_queues = p_queues, .size = count};
    return rt_port_shared_call(perform, &request);
}

/** @brief Copy one typed sample; a busy return means no sample was queued. */
int shared_publish (msgbus_queue_id_t h_queue,
                    msgbus_type_t     type,
                    const void *      p_item,
                    size_t            size)
{
    shared_request_t request = {.operation = SHARED_PUBLISH,
                                .h_queue   = h_queue,
                                .type      = type,
                                .p_input   = p_item,
                                .size      = size};
    return rt_port_shared_call(perform, &request);
}

/** @brief Consume one FIFO item without competing unsynchronized access. */
int shared_consume (msgbus_queue_id_t h_queue,
                    msgbus_type_t     type,
                    void *            p_item,
                    size_t            capacity)
{
    shared_request_t request = {.operation = SHARED_CONSUME,
                                .h_queue   = h_queue,
                                .type      = type,
                                .p_output  = p_item,
                                .size      = capacity};
    return rt_port_shared_call(perform, &request);
}

/** @brief Copy consistent queue counters. */
int shared_queue_stats (msgbus_queue_id_t h_queue, msgbus_stats_t * p_stats)
{
    shared_request_t request = {.operation = SHARED_QUEUE_STATS,
                                .h_queue   = h_queue,
                                .p_output  = p_stats};
    return rt_port_shared_call(perform, &request);
}

/** @brief Count a health event even if its notification is rate-limited. */
int shared_report (health_event_t event)
{
    shared_request_t request = {.operation = SHARED_REPORT, .event = event};
    return rt_port_shared_call(perform, &request);
}

/** @brief Copy a consistent health snapshot. */
int shared_snapshot (health_snapshot_t * p_snapshot)
{
    shared_request_t request = {.operation = SHARED_SNAPSHOT,
                                .p_output  = p_snapshot};
    return rt_port_shared_call(perform, &request);
}

/** @brief Enroll an instance before releasing the activation barrier. */
int shared_watch (const char * p_name, uint32_t timeout_ms)
{
    if (!rt_port_is_owner())
    {
        return -1;
    }
    shared_request_t request = {
        .operation = SHARED_WATCH, .p_name = p_name, .timeout_ms = timeout_ms};
    return rt_port_shared_call(perform, &request);
}

/** @brief Record actual completed work, not an independent timer pulse. */
int shared_heartbeat (const char * p_name)
{
    shared_request_t request = {.operation = SHARED_HEARTBEAT,
                                .p_name    = p_name};
    return rt_port_shared_call(perform, &request);
}

/** @brief Check heartbeat deadlines from a task-context supervisor. */
int shared_check (void)
{
    shared_request_t request = {.operation = SHARED_CHECK};
    return rt_port_shared_call(perform, &request);
}

/** @brief Register a producer while all workers remain dormant. */
int shared_expect (const sampcache_source_t * p_source)
{
    if (!rt_port_is_owner())
    {
        return -1;
    }
    shared_request_t request = {.operation = SHARED_EXPECT,
                                .p_input   = p_source};
    return rt_port_shared_call(perform, &request);
}

/** @brief Read the observation state without draining any consumer queue. */
int shared_capture (shared_capture_t * p_capture)
{
    shared_request_t request = {.operation = SHARED_CAPTURE,
                                .p_output  = p_capture};
    return rt_port_shared_call(perform, &request);
}

/** @brief Copy one rotating source with its coherent freshness reference. */
int shared_sample (uint8_t mask, sampcache_entry_t * p_entry, uint64_t * p_now)
{
    if (NULL == p_now)
    {
        return -1;
    }
    shared_request_t request = {.operation = SHARED_SAMPLE,
                                .type      = mask,
                                .p_output  = p_entry,
                                .p_time    = p_now};
    return rt_port_shared_call(perform, &request);
}

/** @brief Copy an exact source under the same bounded snapshot guard. */
int shared_source (uint16_t            source_index,
                   sampcache_entry_t * p_entry,
                   uint64_t *          p_now)
{
    if (NULL == p_now)
    {
        return -1;
    }
    shared_request_t request = {.operation = SHARED_SOURCE,
                                .type      = source_index,
                                .p_output  = p_entry,
                                .p_time    = p_now};
    return rt_port_shared_call(perform, &request);
}

/** @brief Execute one short operation under the port's nonblocking guard. */
static int perform (void * p_context)
{
    const shared_request_t * p_request = p_context;
    uint64_t                 now       = 0U;
    if (0 != rt_port_now_ms(&now))
    {
        return -1;
    }
    /* Existing health APIs use modular 32-bit milliseconds by contract. */
    const uint32_t clock_ms = (uint32_t)(now & UINT32_MAX);
    switch (p_request->operation)
    {
    case SHARED_RESET:
    {
        return reset_queues(p_request);
    }
    case SHARED_PUBLISH:
    {
        return publish(p_request, now);
    }
    case SHARED_CONSUME:
    {
        return msgbus_consume(p_request->h_queue,
                              p_request->type,
                              p_request->p_output,
                              p_request->size);
    }
    case SHARED_QUEUE_STATS:
    {
        return msgbus_get_stats(p_request->h_queue, p_request->p_output);
    }
    case SHARED_REPORT:
    {
        return health_report(p_request->event, clock_ms);
    }
    case SHARED_SNAPSHOT:
    {
        return health_get_snapshot(p_request->p_output);
    }
    case SHARED_WATCH:
    {
        return watchdog_watch_task(
            p_request->p_name, p_request->timeout_ms, clock_ms);
    }
    case SHARED_HEARTBEAT:
    {
        return watchdog_heartbeat(p_request->p_name, clock_ms);
    }
    case SHARED_EXPECT:
    {
        return expect(p_request->p_input);
    }
    case SHARED_SOURCE:
    {
        *p_request->p_time = now;
        return sampcache_source(p_request->type, p_request->p_output);
    }
    case SHARED_SAMPLE:
    {
        *p_request->p_time = now;
        /* shared_sample supplies an eight-bit mask without widening its range.
         */
        return sampcache_select(
            (uint8_t)p_request->type, now / 2000U, p_request->p_output);
    }
    case SHARED_CAPTURE:
    {
        return capture(p_request->p_output, now);
    }
    case SHARED_CHECK:
    {
        return watchdog_check(clock_ms);
    }
    default:
    {
        return -1;
    }
    }
}

/** @brief Rebuild the whole fixed queue table after validation and quiescence.
 */
static int reset_queues (const shared_request_t * p_request)
{
    if ((0 != msgbus_init()) || (0 != health_init()) || (0 != watchdog_init()))
    {
        return -1;
    }
    sampcache_reset();
    g_queue_count = p_request->size;
    for (size_t index = 0U; index < p_request->size; ++index)
    {
        g_queues[index]           = p_request->p_queues[index];
        msgbus_queue_id_t h_queue = -1;
        const int         result =
            msgbus_create_queue(&p_request->p_queues[index], &h_queue);
        if (0 != result)
        {
            return result;
        }
    }
    return 0;
}

/** @brief Require a real queue with the matching native sample contract. */
static int expect (const sampcache_source_t * p_source)
{
    /* Short-circuit rejects negative handles before the uint32 conversion. */
    if ((NULL == p_source) || (p_source->h_queue < 0) ||
        ((uint32_t)p_source->h_queue >= UTHOME_MSGBUS_MAX_QUEUES))
    {
        return -1;
    }
    /* The checked nonnegative queue handle fits size_t. */
    const size_t index = (size_t)p_source->h_queue;
    if ((index >= g_queue_count) || (g_queues[index].type != p_source->type) ||
        (g_queues[index].item_size != sampcache_size(p_source->type)))
    {
        return -1;
    }
    return sampcache_expect(p_source);
}

/** @brief Observe successful publications and count each actual dropped item.
 */
static int publish (const shared_request_t * p_request, uint64_t now_ms)
{
    msgbus_stats_t before   = {0};
    msgbus_stats_t after    = {0};
    const int before_result = msgbus_get_stats(p_request->h_queue, &before);
    const int result        = msgbus_publish(p_request->h_queue,
                                      p_request->type,
                                      p_request->p_input,
                                      p_request->size);
    if (0 == result)
    {
        sampcache_record(p_request->h_queue,
                         p_request->type,
                         p_request->p_input,
                         p_request->size,
                         now_ms);
    }
    if ((0 == before_result) &&
        (0 == msgbus_get_stats(p_request->h_queue, &after)) &&
        (before.dropped_count != after.dropped_count))
    {
        /* Rate limiting affects notifications only; health still counts drops.
         * The health clock is modular uint32 by its established contract. */
        (void)health_report(UTHOME_HEALTH_QUEUE_OVERFLOW,
                            (uint32_t)(now_ms & UINT32_MAX));
    }
    return result;
}

/** @brief Copy all observations at one timestamp inside the existing guard. */
static int capture (shared_capture_t * p_capture, uint64_t now_ms)
{
    if (NULL == p_capture)
    {
        return -1;
    }
    memset(p_capture, 0, sizeof(*p_capture));
    p_capture->timestamp_ms = now_ms;
    p_capture->queue_count  = g_queue_count;
    if ((0 != sampcache_copy(&p_capture->samples)) ||
        (0 != health_get_snapshot(&p_capture->health)))
    {
        return -1;
    }
    for (size_t index = 0U; index < g_queue_count; ++index)
    {
        p_capture->queues[index] = g_queues[index];
        /* The fixed table has at most four signed queue handles. */
        if (0 != msgbus_get_stats((msgbus_queue_id_t)index,
                                  &p_capture->stats[index]))
        {
            return -1;
        }
    }
    return 0;
}

/* End of file. */

