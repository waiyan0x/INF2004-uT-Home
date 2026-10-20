/** @file relayctl.c
 * @brief Apply digital relay levels without PWM, sleeps or allocation. */
#include "relayctl.h"
#include <stddef.h>
static int command (relayctl_t * p_relay, bool b_on);

/** @brief Copy a validated configuration and establish the OFF command. */
int relayctl_open (relayctl_t *              p_relay,
                   const relayctl_config_t * p_config,
                   uint64_t                  now_ms)
{
    if ((NULL == p_relay) || (NULL == p_config) || (p_relay->b_open) ||
        (NULL == p_config->output.p_set) || (p_config->minimum_ms < 100U) ||
        (p_config->minimum_ms > 60000U))
    {
        return -1;
    }
    p_relay->config     = *p_config;
    p_relay->last_ms    = now_ms;
    p_relay->changed_ms = now_ms;
    p_relay->b_open     = true;
    p_relay->b_fault    = false;
    return command(p_relay, false);
}

/** @brief Apply a caller-owned decision with bounded switching frequency. */
int relayctl_step (relayctl_t * p_relay,
                   bool         b_requested,
                   bool         b_permitted,
                   uint64_t     now_ms)
{
    if ((NULL == p_relay) || (!p_relay->b_open))
    {
        return -1;
    }
    const bool b_backward = now_ms < p_relay->last_ms;
    p_relay->last_ms      = now_ms;
    if ((!b_permitted) || (b_backward) || (p_relay->b_fault))
    {
        const bool b_fault  = p_relay->b_fault;
        p_relay->changed_ms = now_ms;
        const int result    = command(p_relay, false);
        if ((0 != result) || (b_fault))
        {
            return -2;
        }
        return b_backward ? -3 : 0;
    }
    if ((b_requested != p_relay->b_on) &&
        ((now_ms - p_relay->changed_ms) >= p_relay->config.minimum_ms))
    {
        p_relay->changed_ms = now_ms;
        return command(p_relay, b_requested);
    }
    return 0;
}

/** @brief Attempt OFF before allowing the caller to release its output. */
int relayctl_close (relayctl_t * p_relay)
{
    if ((NULL == p_relay) || (!p_relay->b_open))
    {
        return -1;
    }
    const int result = command(p_relay, false);
    if (0 == result)
    {
        p_relay->b_open = false;
    }
    return result;
}

/** @brief Record successful commands; on failure attempt OFF and latch fault.
 */
static int command (relayctl_t * p_relay, bool b_on)
{
    const bool b_level = b_on == p_relay->config.b_active_high;
    const int  result =
        p_relay->config.output.p_set(p_relay->config.output.p_context, b_level);
    if (0 != result)
    {
        p_relay->b_fault   = true;
        const int fallback = p_relay->config.output.p_set(
            p_relay->config.output.p_context, !p_relay->config.b_active_high);
        if (0 == fallback)
        {
            p_relay->b_on = false;
        }
        return -2;
    }
    p_relay->b_on = b_on;
    return 0;
}

/* End of file. */

