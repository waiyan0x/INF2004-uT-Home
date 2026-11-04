/**
 * @file netretry.c
 * @brief Finite connection attempts without blocking local runtime work.
 */
#include "netretry.h"
#include <stddef.h>

static int schedule (netretry_t * p_retry, uint64_t now_ms, uint32_t jitter_ms);

/** @brief Establish an immediately eligible disconnected state. */
int netretry_init (netretry_t * p_retry, uint64_t now_ms)
{
    if (NULL == p_retry)
    {
        return -1;
    }
    *p_retry = (netretry_t){.last_ms     = now_ms,
                            .deadline_ms = now_ms,
                            .delay_ms    = 1000U,
                            .state       = NETRETRY_WAITING};
    return 0;
}

/** @brief Admit at most one attempt; expiry schedules a later retry. */
int netretry_poll (netretry_t * p_retry,
                   uint64_t     now_ms,
                   bool         b_upstream,
                   uint32_t     jitter_ms)
{
    netretry_t candidate = {0};
    int        result    = 0;
    if ((NULL == p_retry) || (now_ms < p_retry->last_ms) || (250U < jitter_ms))
    {
        return -1;
    }
    candidate = *p_retry;
    if ((!b_upstream) || ((NETRETRY_ATTEMPT == candidate.state) &&
                          (now_ms >= candidate.deadline_ms)))
    {
        if (NETRETRY_WAITING != candidate.state)
        {
            result = schedule(&candidate, now_ms, jitter_ms);
        }
    }
    else if ((NETRETRY_WAITING == candidate.state) &&
             (now_ms >= candidate.deadline_ms))
    {
        if ((UINT64_MAX - now_ms) < 30000U)
        {
            return -1;
        }
        candidate.state       = NETRETRY_ATTEMPT;
        candidate.deadline_ms = now_ms + 30000U;
        result                = 1;
    }
    else
    {
        /* An active or waiting connection needs no transition yet. */
    }
    if (0 > result)
    {
        return -1;
    }
    candidate.last_ms = now_ms;
    *p_retry          = candidate;
    return result;
}

/** @brief Accept readiness or schedule failure with preserved ownership. */
int netretry_result (netretry_t * p_retry,
                     uint64_t     now_ms,
                     bool         b_success,
                     uint32_t     jitter_ms)
{
    netretry_t candidate = {0};
    if ((NULL == p_retry) || (now_ms < p_retry->last_ms) ||
        (250U < jitter_ms) || (NETRETRY_WAITING == p_retry->state))
    {
        return -1;
    }
    candidate = *p_retry;
    if (b_success)
    {
        if ((NETRETRY_ATTEMPT != candidate.state) ||
            (now_ms >= candidate.deadline_ms))
        {
            return -1;
        }
        candidate.state     = NETRETRY_ONLINE;
        candidate.online_ms = now_ms;
    }
    else if (0 != schedule(&candidate, now_ms, jitter_ms))
    {
        return -1;
    }
    else
    {
        /* Failed connection has been scheduled for a bounded retry. */
    }
    candidate.last_ms = now_ms;
    *p_retry          = candidate;
    return 0;
}

/** @brief Grow backoff with overflow-safe absolute time and a fixed cap. */
static int schedule (netretry_t * p_retry, uint64_t now_ms, uint32_t jitter_ms)
{
    uint32_t delay_ms = p_retry->delay_ms;
    uint32_t wait_ms  = 0U;
    if ((NETRETRY_ONLINE == p_retry->state) &&
        ((now_ms - p_retry->online_ms) >= 60000U))
    {
        delay_ms = 1000U;
    }
    wait_ms = delay_ms + jitter_ms;
    if (30000U < wait_ms)
    {
        wait_ms = 30000U;
    }
    if ((UINT64_MAX - now_ms) < wait_ms)
    {
        return -1;
    }
    p_retry->deadline_ms = now_ms + wait_ms;
    p_retry->delay_ms    = (15000U < delay_ms) ? 30000U : delay_ms * 2U;
    p_retry->state       = NETRETRY_WAITING;
    return 0;
}

/* End of file. */

