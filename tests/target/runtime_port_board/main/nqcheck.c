/**
 * @file nqcheck.c
 * @brief Exercise owned queues against real native blocking and ownership.
 */
#include "nqcheck.h"

#include "c3nq.h"
#include "c3event.h"
#include "c3audit.h"
#include "c3clock.h"
#include "workport.h"
#include <tk/tkernel.h>

typedef struct
{
    void * p_data;
    void * p_ack;
    bool   b_done;
    bool   b_send;
    bool   b_event;
} nqcheck_exchange_t;

static nqcheck_exchange_t g_exchange = {0};

static int queue_order (void);
static int large_queue (void);
static int semaphore_mutex (void);
static int event_bits (void);
static int exchange (bool b_send, bool b_event);
static int exchange_step (void * p_context);
static int exchange_cleanup (void * p_context);
static bool same_phase (memaudit_phase_t const * p_before,
                        memaudit_phase_t const * p_after);

/** @brief Check native behavior and unchanged allocation/object counters. */
int nqcheck_run (void)
{
    memaudit_t before = {0};
    memaudit_t after  = {0};
    if ((0 != c3audit_capture(&before)) || (2 != c3nq_scheduler_state()) ||
        (0 != c3nq_init()))
    {
        return -1;
    }
    for (unsigned cycle = 0U; cycle < 128U; ++cycle)
    {
        if ((0 != queue_order()) || (0 != semaphore_mutex()) ||
            (0 != event_bits()))
        {
            return -2;
        }
    }
    if ((0 != large_queue()) || (0 != exchange(true, false)) ||
        (0 != exchange(false, false)) || (0 != exchange(false, true)))
    {
        return -3;
    }
    if ((0 != c3audit_capture(&after)) ||
        (!same_phase(&before.startup, &after.startup)) ||
        (!same_phase(&before.normal, &after.normal)) ||
        (before.b_saturated != after.b_saturated))
    {
        return -4;
    }
    return 0;
}

/** @brief Exact capacity, front/back/peek, static identity and release reuse.
 */
static int queue_order (void)
{
    uint32_t storage[2] = {0};
    uint32_t key        = 91U;
    uint32_t first      = 11U;
    uint32_t second     = 22U;
    uint32_t result     = 0U;
    /* Byte access to live uint32_t storage is permitted by the C object model.
     */
    void * p_queue =
        c3nq_static(2U, sizeof(first), (uint8_t *)storage, &key, 0U);
    if ((p_queue != &key) || (1 != c3nq_send(p_queue, &first, 0U, 0)) ||
        (1 != c3nq_send(p_queue, &second, 0U, 1)) ||
        (0 != c3nq_send(p_queue, &first, 0U, 0)) ||
        (2U != c3nq_waiting(p_queue)) || (0U != c3nq_spaces(p_queue)) ||
        (1 != c3nq_peek(p_queue, &result, 0U)) || (second != result) ||
        (1 != c3nq_receive(p_queue, &result, 0U)) || (second != result) ||
        (1 != c3nq_receive(p_queue, &result, 0U)) || (first != result) ||
        (0 != c3nq_receive(p_queue, &result, 0U)) || (91U != key))
    {
        return -1;
    }
    c3nq_delete(p_queue);
    p_queue = c3nq_create(1U, sizeof(first), 0U);
    if ((NULL == p_queue) || (1 != c3nq_send(p_queue, &first, 0U, 0)) ||
        (1 != c3nq_send(p_queue, &second, 0U, 2)) ||
        (1 != c3nq_receive(p_queue, &result, 0U)) || (second != result) ||
        (1 != c3nq_reset(p_queue)))
    {
        return -2;
    }
    c3nq_delete(p_queue);
    return 0;
}

/** @brief Exercise the actual 200 by 8-byte radio queue shape and reuse. */
static int large_queue (void)
{
    for (uint32_t cycle = 0U; cycle < 2U; ++cycle)
    {
        void * p_queue = c3nq_create(200U, 8U, 0U);
        if ((NULL == p_queue) || (NULL != c3nq_create(257U, 8U, 0U)))
        {
            return -1;
        }
        for (uint32_t index = 0U; index < 200U; ++index)
        {
            const uint32_t value[2] = {cycle, index};
            if (1 != c3nq_send(p_queue, value, 0U, 0))
            {
                return -2;
            }
        }
        uint32_t value[2] = {0};
        if ((200U != c3nq_waiting(p_queue)) ||
            (0 != c3nq_send(p_queue, value, 0U, 0)))
        {
            return -3;
        }
        for (uint32_t index = 0U; index < 200U; ++index)
        {
            if ((1 != c3nq_receive(p_queue, value, 0U)) ||
                (cycle != value[0]) || (index != value[1]))
            {
                return -4;
            }
        }
        c3nq_delete(p_queue);
    }
    return 0;
}

/** @brief Real inherited mutex recursion and counting semaphore capacities. */
static int semaphore_mutex (void)
{
    void * p_count = c3nq_counting(2U, 1U);
    void * p_mutex = c3nq_mutex(4U);
    if ((NULL == p_count) || (NULL == p_mutex) ||
        (1 != c3nq_send(p_count, NULL, 0U, 0)) ||
        (0 != c3nq_send(p_count, NULL, 0U, 0)) ||
        (1 != c3nq_take(p_count, 0U)) || (1 != c3nq_take(p_count, 0U)) ||
        (0 != c3nq_take(p_count, 0U)) ||
        (1 != c3nq_take_recursive(p_mutex, 0U)) ||
        (1 != c3nq_take_recursive(p_mutex, 0U)) ||
        (1 != c3nq_give_recursive(p_mutex)) ||
        (1 != c3nq_give_recursive(p_mutex)) ||
        (0 != c3nq_give_recursive(p_mutex)))
    {
        return -1;
    }
    c3nq_delete(p_mutex);
    c3nq_delete(p_count);
    return 0;
}

/** @brief Static identity, any/all waits and clear masks preserve unrelated
 * bits. */
static int event_bits (void)
{
    uint32_t key     = 71U;
    void *   p_event = c3event_static(&key);
    if ((p_event != &key) || (9U != c3event_set(p_event, 9U)) ||
        (9U != c3event_wait(p_event, 3U, 1, 1, 0U)) ||
        (9U != c3event_get(p_event)) ||
        (9U != c3event_wait(p_event, 1U, 1, 0, 0U)) ||
        (8U != c3event_get(p_event)) || (8U != c3event_clear(p_event, 8U)) ||
        (0U != c3event_get(p_event)) ||
        (3U != c3event_sync(p_event, 3U, 3U, 0U)) ||
        (0U != c3event_get(p_event)) || (71U != key))
    {
        return -1;
    }
    c3event_delete(p_event);
    return 0;
}

/** @brief Real blocked queue transfers and two-task barrier handoff. */
static int exchange (bool b_send, bool b_event)
{
    const rt_port_task_config_t config = {
        .priority = 10U, .stack_size_bytes = 2048U, .period_ms = 10U};
    const rt_port_worker_t worker = {.p_step    = exchange_step,
                                     .p_cleanup = exchange_cleanup,
                                     .p_context = &g_exchange};
    rt_port_task_id_t      h_task = 0;
    uint64_t               start  = 0U;
    uint64_t               finish = 0U;
    uint32_t               value  = 17U;
    uint32_t               result = 0U;
    int                    status = -1;
    g_exchange = (nqcheck_exchange_t){.b_send = b_send, .b_event = b_event};
    g_exchange.p_data =
        b_event ? c3event_create() : c3nq_create(1U, sizeof(value), 0U);
    g_exchange.p_ack = c3nq_create(1U, sizeof(value), 0U);
    if ((NULL == g_exchange.p_data) || (NULL == g_exchange.p_ack) ||
        ((!b_event) && (!b_send) &&
         (1 != c3nq_send(g_exchange.p_data, &value, 0U, 0))) ||
        (0 != rt_port_task_prepare_worker(&config, &worker, &h_task)) ||
        (0 != rt_port_gate_set(true)) || (0 != rt_port_task_start(h_task)))
    {
        return -1;
    }
    if (0 == c3clock_now(&start))
    {
        int32_t operation = 0;
        if (b_event)
        {
            operation =
                (3U == c3event_sync(g_exchange.p_data, 1U, 3U, 100U)) ? 1 : 0;
        }
        else
        {
            operation = b_send ? c3nq_receive(g_exchange.p_data, &result, 100U)
                               : c3nq_send(g_exchange.p_data, &value, 100U, 0);
        }
        if ((1 == operation) && (0 == c3clock_now(&finish)) &&
            (finish >= start) && ((finish - start) >= 20000U) &&
            ((finish - start) < 500000U) &&
            (1 == c3nq_receive(g_exchange.p_ack, &result, 100U)) &&
            (17U == result))
        {
            status = 0;
        }
    }
    /* Stop joins the borrowed callback before resetting or releasing its data.
     */
    if (0 != rt_port_task_stop(h_task))
    {
        return -2;
    }
    if (b_event)
    {
        c3event_delete(g_exchange.p_data);
    }
    else
    {
        c3nq_delete(g_exchange.p_data);
    }
    c3nq_delete(g_exchange.p_ack);
    return status;
}

/** @brief One bounded peer operation; state is read again only after join. */
static int exchange_step (void * p_context)
{
    nqcheck_exchange_t * p_exchange = p_context;
    uint32_t             value      = 17U;
    if (!p_exchange->b_done)
    {
        p_exchange->b_done = true;
        if (E_OK != tk_dly_tsk(30))
        {
            return -1;
        }
        int32_t result = 0;
        if (p_exchange->b_event)
        {
            result =
                (3U == c3event_sync(p_exchange->p_data, 2U, 3U, 100U)) ? 1 : 0;
        }
        else
        {
            result = p_exchange->b_send
                         ? c3nq_send(p_exchange->p_data, &value, 0U, 0)
                         : c3nq_receive(p_exchange->p_data, &value, 0U);
        }
        if ((1 != result) || (17U != value) ||
            (1 != c3nq_send(p_exchange->p_ack, &value, 0U, 0)))
        {
            return -2;
        }
    }
    return 0;
}

/** @brief Owner releases queues only after the runtime confirms dormancy. */
static int exchange_cleanup (void * p_context)
{
    /* Callback data remains in static test storage until the next joined run.
     */
    (void)p_context;
    return 0;
}

/** @brief Compare fields rather than structure padding in audit snapshots. */
static bool same_phase (memaudit_phase_t const * p_before,
                        memaudit_phase_t const * p_after)
{
    if ((p_before->allocations != p_after->allocations) ||
        (p_before->bytes != p_after->bytes) ||
        (p_before->frees != p_after->frees))
    {
        return false;
    }
    for (size_t index = 0U; index < UTHOME_AUDIT_KINDS; ++index)
    {
        if (p_before->creates[index] != p_after->creates[index])
        {
            return false;
        }
    }
    for (size_t index = 0U; index < UTHOME_AUDIT_ALLOCATORS; ++index)
    {
        if (p_before->kernel_calls[index] != p_after->kernel_calls[index])
        {
            return false;
        }
    }
    return true;
}

/* End of file. */

