/**
 * @file evfake.c
 * @brief Inject native failures and publish events while the adapter waits.
 */
#include "evfake.h"

#include "c3event.h"
#include <stddef.h>

static evfake_config_t  g_config  = {0};
static evfake_metrics_t g_metrics = {0};
static void *           gp_handle = NULL;
static int64_t          g_now     = 10000;
static uint32_t         g_clocks  = 0U;

/** @brief Change only fault controls; preserve adapter and resource lifetime.
 */
void evfake_configure (const evfake_config_t * p_config, void * p_handle)
{
    g_config  = *p_config;
    gp_handle = p_handle;
    g_clocks  = 0U;
}

/** @brief Expose call observations without exposing event adapter internals. */
const evfake_metrics_t * evfake_metrics (void)
{
    return &g_metrics;
}

/** @brief A failed creation consumes no new handle and must not be retried. */
int evfake_create (void)
{
    ++g_metrics.creates;
    if ((g_config.create_fail_at > 0) &&
        ((uint32_t)g_config.create_fail_at == g_metrics.creates))
    {
        return 0;
    }
    /* A valid fixture reserves exactly sixteen handles. */
    if (g_metrics.creates > 16U)
    {
        ++g_metrics.violations;
        return 0;
    }
    return (int)g_metrics.creates;
}

/** @brief Hints are published inside the same critical section as state. */
int evfake_signal (int h_notification)
{
    ++g_metrics.signals;
    if ((0U == g_metrics.depth) || (h_notification < 1) ||
        (h_notification > 16))
    {
        ++g_metrics.violations;
    }
    return g_config.signal_result;
}

/** @brief Advance one millisecond without extending the adapter's deadline. */
int evfake_wait (int h_notification, uint32_t milliseconds)
{
    if ((0U != g_metrics.depth) || (h_notification < 1) ||
        (h_notification > 16) || (g_metrics.waits >= 32U))
    {
        ++g_metrics.violations;
        return C3EWAIT_ERROR;
    }
    g_metrics.budgets[g_metrics.waits] = milliseconds;
    ++g_metrics.waits;
    g_now += 1000;
    if (0U != g_config.stale_wakes)
    {
        --g_config.stale_wakes;
    }
    else if (g_config.b_delete)
    {
        c3event_delete(gp_handle);
    }
    else if (g_config.b_publish)
    {
        (void)c3event_set(gp_handle, 1U);
    }
    else
    {
        /* No event arrives; only the native wait result and clock change. */
    }
    return g_config.wait_result;
}

/** @brief Model nested saved-mask exclusion with an observable depth. */
uint32_t evfake_enter (void)
{
    const uint32_t saved = g_metrics.depth;
    ++g_metrics.depth;
    ++g_metrics.enters;
    return saved;
}

/** @brief Every exit must restore the exact previous nesting depth. */
void evfake_leave (uint32_t saved)
{
    if ((saved + 1U) != g_metrics.depth)
    {
        ++g_metrics.violations;
    }
    g_metrics.depth = saved;
    ++g_metrics.leaves;
}

/** @brief Fail cache checks without creating or modifying native resources. */
bool evfake_available (void)
{
    return g_config.b_cache;
}

/** @brief Supply a controlled verdict from the owned native-context boundary.
 */
int evfake_context (bool b_blocking)
{
    (void)b_blocking;
    return g_config.b_context ? 0 : C3EWAIT_ERROR;
}

/** @brief Fail or reverse one selected clock reading, then remain recoverable.
 */
int64_t evfake_now (void)
{
    ++g_clocks;
    ++g_metrics.clocks;
    if (g_config.clock_fail_at == g_clocks)
    {
        return -1;
    }
    if (g_config.clock_reverse_at == g_clocks)
    {
        return g_now - 1000;
    }
    return g_now;
}

/** @brief The deterministic fixture uses one millisecond SDK ticks. */
uint32_t evfake_tick_hz (void)
{
    return 1000U;
}

/* End of file. */

