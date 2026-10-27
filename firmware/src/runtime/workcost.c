/**
 * @file workcost.c
 * @brief Accumulate bounded, caller-owned step and transfer measurements.
 */
#include "workcost.h"

#include <stddef.h>

static void add_count (uint32_t * p_total, uint32_t value, uint32_t * p_flags);

/** @brief Reject invalid observations before changing the aggregate. */
int workcost_add (workcost_stats_t * p_stats, const workcost_step_t * p_step)
{
    if ((NULL == p_stats) || (NULL == p_step))
    {
        return -1;
    }
    if ((p_step->bus_wait_us > p_step->elapsed_us) ||
        (p_step->bus_active_us > (p_step->elapsed_us - p_step->bus_wait_us)) ||
        (p_step->timeouts > p_step->errors))
    {
        return -1;
    }
    add_count(&p_stats->steps, 1U, &p_stats->flags);
    add_count(&p_stats->elapsed_us, p_step->elapsed_us, &p_stats->flags);
    add_count(&p_stats->bus_wait_us, p_step->bus_wait_us, &p_stats->flags);
    add_count(&p_stats->bus_active_us, p_step->bus_active_us, &p_stats->flags);
    add_count(
        &p_stats->transfer_bytes, p_step->transfer_bytes, &p_stats->flags);
    add_count(&p_stats->transfers, p_step->transfers, &p_stats->flags);
    add_count(&p_stats->timeouts, p_step->timeouts, &p_stats->flags);
    add_count(&p_stats->errors, p_step->errors, &p_stats->flags);
    if (p_step->elapsed_us > p_stats->max_elapsed_us)
    {
        p_stats->max_elapsed_us = p_step->elapsed_us;
    }
    if (p_step->lateness_us > p_stats->max_lateness_us)
    {
        p_stats->max_lateness_us = p_step->lateness_us;
    }
    return 0;
}

/** @brief Saturate without unsigned wrap; a saturated window is unusable. */
static void add_count (uint32_t * p_total, uint32_t value, uint32_t * p_flags)
{
    if (value > (UINT32_MAX - *p_total))
    {
        *p_total = UINT32_MAX;
        *p_flags |= WORKCOST_SATURATED;
    }
    else
    {
        *p_total += value;
    }
}

/* End of file. */

