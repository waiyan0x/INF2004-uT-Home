/**
 * @file costmain.c
 * @brief Exercise measurement bounds, ownership, saturation and rejection.
 */
#include "costmain.h"

#include "workcost.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#if defined(NDEBUG)
#error "Assertions must remain enabled for workload measurements"
#endif

/** @brief Check independent caller-owned counters and overflow handling. */
int main (void)
{
    workcost_stats_t stats[4] = {0};
    workcost_step_t  step     = {100U, 20U, 10U, 60U, 30U, 1U, 0U, 0U};

    assert(sizeof(stats) <= 256U);
    assert(-1 == workcost_add(NULL, &step));
    assert(-1 == workcost_add(&stats[0], NULL));
    assert(0 == workcost_add(&stats[0], &step));
    assert(0 == workcost_add(&stats[0], &step));
    assert(2U == stats[0].steps);
    assert(200U == stats[0].elapsed_us);
    assert(100U == stats[0].max_elapsed_us);
    assert(20U == stats[0].max_lateness_us);
    assert(60U == stats[0].transfer_bytes);
    assert(0U == stats[1].steps);

    const workcost_stats_t before = stats[0];
    step.bus_active_us            = 91U;
    assert(-1 == workcost_add(&stats[0], &step));
    assert(0 == memcmp(&before, &stats[0], sizeof(before)));
    step.bus_active_us = 60U;
    step.timeouts      = 1U;
    assert(-1 == workcost_add(&stats[0], &step));
    step.errors = 1U;
    assert(0 == workcost_add(&stats[0], &step));
    assert(1U == stats[0].timeouts);
    assert(1U == stats[0].errors);

    stats[0].elapsed_us = UINT32_MAX - 100U;
    assert(0 == workcost_add(&stats[0], &step));
    assert(UINT32_MAX == stats[0].elapsed_us);
    assert(0U == stats[0].flags);
    assert(0 == workcost_add(&stats[0], &step));
    assert(UINT32_MAX == stats[0].elapsed_us);
    assert(WORKCOST_SATURATED == stats[0].flags);
    stats[0].steps = UINT32_MAX;
    assert(0 == workcost_add(&stats[0], &step));
    assert(UINT32_MAX == stats[0].steps);
    return 0;
}

/* End of file. */

