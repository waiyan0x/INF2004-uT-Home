/**
 * @file workmeter.c
 * @brief Reject incomplete or inconsistent measurements instead of zero fill.
 */
#include "workmeter.h"
#include <stddef.h>
#include <string.h>

static bool add_value (uint32_t * p_value, uint64_t amount);

/** @brief Start a nonnested worker-owned measurement without clearing totals.
 */
int workmeter_begin (workmeter_t * p_state, uint64_t now_us)
{
    if ((NULL == p_state) || (p_state->b_active) || (p_state->b_invalid))
    {
        return -1;
    }
    memset(&p_state->step, 0, sizeof(p_state->step));
    p_state->started_us = now_us;
    p_state->b_active   = true;
    return 0;
}

/** @brief Include successful payload bytes and all attempted transfer times. */
int workmeter_bus (workmeter_t * p_state,
                   uint64_t      wait_us,
                   uint64_t      active_us,
                   uint32_t      bytes,
                   bool          b_timeout,
                   bool          b_error)
{
    if ((NULL == p_state) || (!p_state->b_active) || (p_state->b_invalid))
    {
        return -1;
    }
    workcost_step_t * p_step = &p_state->step;
    if (((b_timeout) && (!b_error)) ||
        (!add_value(&p_step->bus_wait_us, wait_us)) ||
        (!add_value(&p_step->bus_active_us, active_us)) ||
        (!add_value(&p_step->transfer_bytes, bytes)) ||
        (!add_value(&p_step->transfers, 1U)) ||
        (!add_value(&p_step->timeouts, b_timeout ? 1U : 0U)) ||
        (!add_value(&p_step->errors, b_error ? 1U : 0U)))
    {
        p_state->b_invalid = true;
        return -1;
    }
    return 0;
}

/** @brief Publish one complete observation; failed windows remain unusable. */
int workmeter_finish (workmeter_t * p_state,
                      uint64_t      now_us,
                      uint64_t      lateness_us,
                      bool          b_error)
{
    if ((NULL == p_state) || (!p_state->b_active))
    {
        return -1;
    }
    p_state->b_active = false;
    if ((p_state->b_invalid) || (now_us < p_state->started_us) ||
        ((now_us - p_state->started_us) > UINT32_MAX) ||
        (lateness_us > UINT32_MAX))
    {
        p_state->b_invalid = true;
        return -1;
    }
    /* Both unsigned conversions are bounded by the preceding checks. */
    p_state->step.elapsed_us  = (uint32_t)(now_us - p_state->started_us);
    p_state->step.lateness_us = (uint32_t)lateness_us;
    if ((b_error) && (0U == p_state->step.errors))
    {
        p_state->step.errors = 1U;
    }
    const int result = workcost_add(&p_state->stats, &p_state->step);
    if (0 != result)
    {
        p_state->b_invalid = true;
    }
    return result;
}

/** @brief Refuse an overflowing partial step before it can label a window. */
static bool add_value (uint32_t * p_value, uint64_t amount)
{
    if (amount > (UINT32_MAX - *p_value))
    {
        return false;
    }
    /* The remaining-capacity bound establishes uint32_t representability. */
    *p_value += (uint32_t)amount;
    return true;
}

/* End of file. */

