/**
 * @file c3cost.c
 * @brief Measure bus API wall time using a fixed task-local record.
 */
#include "c3cost.h"
#include "c3clock.h"
#include "workport.h"

/** @brief Start before attempting the permanent protocol lock. */
void c3cost_begin (c3cost_t * p_state)
{
    if (NULL != p_state)
    {
        *p_state         = (c3cost_t){0};
        p_state->b_valid = (0 == c3clock_now(&p_state->started_us));
    }
}

/** @brief Separate successful acquisition from subsequent transfer work. */
void c3cost_acquired (c3cost_t * p_state)
{
    if ((NULL != p_state) && (p_state->b_valid))
    {
        p_state->b_valid = (0 == c3clock_now(&p_state->acquired_us)) &&
                           (p_state->acquired_us >= p_state->started_us);
        p_state->b_acquired = true;
    }
}

/** @brief Publish only ordered clocks and bounded successful byte counts. */
void c3cost_end (const c3cost_t * p_state,
                 size_t           bytes,
                 bool             b_timeout,
                 bool             b_error)
{
    uint64_t now_us = 0U;
    if ((NULL == p_state) || (!p_state->b_valid) || (bytes > UINT32_MAX) ||
        (0 != c3clock_now(&now_us)) || (now_us < p_state->started_us) ||
        ((p_state->b_acquired) && (now_us < p_state->acquired_us)))
    {
        rt_port_cost_invalid();
        return;
    }
    const uint64_t acquired =
        p_state->b_acquired ? p_state->acquired_us : now_us;
    /* The preceding bound proves that the successful payload fits uint32_t. */
    rt_port_cost_bus(acquired - p_state->started_us,
                     now_us - acquired,
                     (uint32_t)bytes,
                     b_timeout,
                     b_error);
}

/* End of file. */

