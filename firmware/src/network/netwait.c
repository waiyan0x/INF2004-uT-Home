/**
 * @file netwait.c
 * @brief Absolute deadlines for SDK tick waits over a millisecond kernel.
 */
#include "netwait.h"

#include <stddef.h>

/** @brief Preserve SDK tick units without resetting time after wakeups. */
int netwait_init (netwait_t * p_wait,
                  uint64_t    now_us,
                  uint32_t    ticks,
                  uint32_t    tick_hz)
{
    netwait_t candidate   = {0};
    uint64_t  duration_us = 0U;

    if ((NULL == p_wait) || (0U == tick_hz) || (1000000U < tick_hz))
    {
        return -1;
    }
    candidate.started_us = now_us;
    candidate.b_infinite = (UINT32_MAX == ticks);
    if (!candidate.b_infinite)
    {
        duration_us = (((uint64_t)ticks * 1000000U) + tick_hz - 1U) / tick_hz;
        if ((UINT64_MAX - now_us) < duration_us)
        {
            return -1;
        }
        candidate.deadline_us = now_us + duration_us;
    }
    *p_wait = candidate;
    return 0;
}

/** @brief Bound finite native waits while preserving one absolute deadline. */
int netwait_remaining (netwait_t const * p_wait,
                       uint64_t          now_us,
                       uint32_t *        p_milliseconds)
{
    uint64_t remaining_us = 0U;
    uint64_t milliseconds = 0U;

    if ((NULL == p_wait) || (NULL == p_milliseconds) ||
        (now_us < p_wait->started_us) ||
        ((!p_wait->b_infinite) && (p_wait->deadline_us < p_wait->started_us)))
    {
        return -1;
    }
    if (p_wait->b_infinite)
    {
        *p_milliseconds = UINT32_MAX;
    }
    else if (now_us >= p_wait->deadline_us)
    {
        *p_milliseconds = 0U;
    }
    else
    {
        remaining_us = p_wait->deadline_us - now_us;
        milliseconds =
            (remaining_us / 1000U) + ((0U != (remaining_us % 1000U)) ? 1U : 0U);
        if ((uint64_t)INT32_MAX < milliseconds)
        {
            milliseconds = INT32_MAX;
        }
        *p_milliseconds = (uint32_t)milliseconds;
    }
    return 0;
}

/* End of file. */

