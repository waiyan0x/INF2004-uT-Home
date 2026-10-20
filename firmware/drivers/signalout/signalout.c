/** @file signalout.c
 * @brief Calculate indicator phase without blocking the owning worker. */
#include "signalout.h"
#include <stddef.h>
static int command (signalout_t * p_signal, bool b_on);

/** @brief Validate timing and write the inactive level before first use. */
int signalout_open (signalout_t *              p_signal,
                    const signalout_config_t * p_config,
                    uint64_t                   now_ms)
{
    if ((NULL == p_signal) || (NULL == p_config) || (p_signal->b_open) ||
        (NULL == p_config->output.p_set) || (0U == p_config->on_ms) ||
        (p_config->on_ms > 60000U) || (p_config->off_ms > 60000U))
    {
        return -1;
    }
    p_signal->config    = *p_config;
    p_signal->epoch_ms  = now_ms;
    p_signal->last_ms   = now_ms;
    p_signal->b_open    = true;
    p_signal->b_enabled = false;
    p_signal->b_fault   = false;
    return command(p_signal, false);
}

/** @brief Start on enable, silence on mute, and skip missed periods. */
int signalout_step (signalout_t * p_signal,
                    bool          b_enabled,
                    bool          b_muted,
                    uint64_t      now_ms)
{
    if ((NULL == p_signal) || (!p_signal->b_open))
    {
        return -1;
    }
    const bool b_backward = now_ms < p_signal->last_ms;
    p_signal->last_ms     = now_ms;
    if ((p_signal->b_fault) || (b_backward))
    {
        const bool b_fault  = p_signal->b_fault;
        p_signal->b_enabled = false;
        const int result    = command(p_signal, false);
        return ((0 != result) || (b_fault)) ? -2 : -3;
    }
    const bool b_enable = (b_enabled) && (!b_muted);
    if ((b_enable) && (!p_signal->b_enabled))
    {
        p_signal->epoch_ms = now_ms;
    }
    p_signal->b_enabled = b_enable;
    const uint64_t period =
        (uint64_t)p_signal->config.on_ms + p_signal->config.off_ms;
    const bool b_on =
        (b_enable) &&
        ((0U == p_signal->config.off_ms) ||
         (((now_ms - p_signal->epoch_ms) % period) < p_signal->config.on_ms));
    return (b_on == p_signal->b_on) ? 0 : command(p_signal, b_on);
}

/** @brief Force inactive and retain the line if the write fails. */
int signalout_close (signalout_t * p_signal)
{
    if ((NULL == p_signal) || (!p_signal->b_open))
    {
        return -1;
    }
    const int result = command(p_signal, false);
    if (0 == result)
    {
        p_signal->b_open = false;
    }
    return result;
}

/** @brief Retain a latched fault even if the immediate OFF retry succeeds. */
static int command (signalout_t * p_signal, bool b_on)
{
    const int result =
        p_signal->config.output.p_set(p_signal->config.output.p_context,
                                      b_on == p_signal->config.b_active_high);
    if (0 != result)
    {
        p_signal->b_fault  = true;
        const int fallback = p_signal->config.output.p_set(
            p_signal->config.output.p_context, !p_signal->config.b_active_high);
        if (0 == fallback)
        {
            p_signal->b_on = false;
        }
        return -2;
    }
    p_signal->b_on = b_on;
    return 0;
}

/* End of file. */

