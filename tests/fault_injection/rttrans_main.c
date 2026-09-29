/**
 * @file rttrans_main.c
 * @brief Runtime transactions regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "rttrans_main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "uthome/runtime.h"
#include "hostport.h"

static runtime_device_t device (const char * p_id, uint32_t period);
static void unchanged (const runtime_device_t * p_expected, size_t tasks);

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    runtime_device_t old[] = {device("old_a", 200U), device("old_b", 500U)};
    runtime_device_t candidate[]          = {device("new_a", 1000U),
                                             device("new_b", 2000U)};
    const hostport_operation_t failures[] = {
        UTHOME_HOST_STOP, UTHOME_HOST_PREPARE, UTHOME_HOST_START};
    for (size_t operation = 0U;
         operation < sizeof(failures) / sizeof(failures[0]);
         ++operation)
    {
        for (size_t after = 0U; after < sizeof(old) / sizeof(old[0]); ++after)
        {
            assert(0 == runtime_init());
            assert(0 == runtime_apply_devices(old, 2));
            hostport_fail_after(failures[operation], after);
            assert(runtime_apply_devices(candidate, 2) != 0);
            unchanged(&old[0], 2U);
            unchanged(&old[1], 2U);
            runtime_device_t absent = {0};
            assert(runtime_get_device("new_a", &absent) != 0);
            assert(0 == runtime_apply_devices(candidate, 2));
            unchanged(&candidate[0], 2U);
        }
    }
    assert(0 == runtime_init());
    assert(0 == runtime_apply_devices(old, 2));
    hostport_fail_after(UTHOME_HOST_STOP, 0U);
    assert(runtime_stop_device("old_a") != 0);
    unchanged(&old[0], 2U); /* Failed release cannot make this slot reusable. */

    runtime_device_t changed = old[0];
    changed.period_ms        = 700U;
    hostport_fail_after(UTHOME_HOST_RECONFIGURE, 0U);
    assert(runtime_update_device("old_a", &changed) != 0);
    unchanged(&old[0], 2U);
    assert(0 == runtime_update_device("old_a", &changed));
    assert(0 == runtime_rollback_device("old_a"));
    unchanged(&old[0], 2U);

    /* Stack changes recreate resources; each intermediate failure restores old
     * state. */
    changed.stack_size_bytes = 4096U;
    for (size_t operation = 0U;
         operation < sizeof(failures) / sizeof(failures[0]);
         ++operation)
    {
        hostport_fail_after(failures[operation], 0U);
        assert(runtime_update_device("old_a", &changed) != 0);
        unchanged(&old[0], 2U);
    }
    assert(0 == runtime_update_device("old_a", &changed));
    hostport_fail_after(UTHOME_HOST_START, 0U);
    assert(runtime_rollback_device("old_a") != 0);
    unchanged(&changed, 2U);
    assert(0 == runtime_rollback_device("old_a"));
    unchanged(&old[0], 2U);

    /* Failed start from an unprepared instance must not leak a prepared
     * resource. */
    assert(0 == runtime_stop_device("old_a"));
    hostport_fail_after(UTHOME_HOST_START, 0U);
    assert(runtime_start_device("old_a") != 0);
    assert(1 == hostport_task_count());
    assert(0 == runtime_start_device("old_a"));
    unchanged(&old[0], 2U);

    /* Failed start of an already-prepared instance preserves its owned
     * resource. */
    assert(0 == runtime_stop_device("old_a"));
    assert(0 == runtime_prepare_device("old_a"));
    hostport_fail_after(UTHOME_HOST_START, 0U);
    assert(runtime_start_device("old_a") != 0);
    runtime_status_t status = {0};
    assert((0 == runtime_get_status("old_a", &status)) &&
           (UTHOME_RUNTIME_READY == status.state));
    assert(2 == hostport_task_count());
    assert(0 == runtime_start_device("old_a"));

    /* If restoration itself fails, do not present a successful or reusable
     * state. */
    hostport_fail_after(UTHOME_HOST_START, 0U);
    hostport_fail_after(UTHOME_HOST_ROLLBACK, 0U);
    assert(UTHOME_RUNTIME_ERR_RECOVERY == runtime_apply_devices(candidate, 2));
    assert(runtime_is_faulted());
    assert(UTHOME_RUNTIME_ERR_RECOVERY == runtime_apply_devices(old, 2));
    assert(UTHOME_RUNTIME_ERR_RECOVERY == runtime_tick(1000));
    runtime_resource_snapshot_t resources = {0U};
    assert(UTHOME_RUNTIME_ERR_RECOVERY == runtime_get_resources(&resources));
    /* Host reset is explicit; target recovery requires a real board strategy.
     */
    assert(0 == runtime_init());
    assert(0 == hostport_task_count());
    puts("Lifecycle failures preserve state/ownership; failed recovery "
         "quarantines runtime.");
    return 0;
}

/** @brief Device. */
static runtime_device_t device (const char * p_id, uint32_t period)
{
    runtime_device_t result = {.period_ms        = period,
                               .priority         = 10U,
                               .stack_size_bytes = 3072U,
                               .b_enabled        = true};
    strcpy(result.name, p_id);
    return result;
}

/** @brief Unchanged. */
static void unchanged (const runtime_device_t * p_expected, size_t tasks)
{
    runtime_device_t            actual    = {0};
    runtime_status_t            status    = {0};
    runtime_resource_snapshot_t resources = {0U};
    assert(0 == runtime_get_device(p_expected->name, &actual));
    assert((actual.period_ms == p_expected->period_ms) &&
           (actual.stack_size_bytes == p_expected->stack_size_bytes));
    assert((0 == runtime_get_status(p_expected->name, &status)) &&
           (UTHOME_RUNTIME_RUNNING == status.state));
    assert((0 == runtime_get_resources(&resources)) &&
           (resources.active_tasks == tasks));
    assert(hostport_task_count() == tasks);
    assert(!runtime_is_faulted());
}

/* End of file. */

