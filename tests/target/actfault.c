/**
 * @file actfault.c
 * @brief Preserve startup failure identity through rollback and reporting.
 */
#include "actfault.h"

#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include <assert.h>

typedef enum
{
    ACTFAULT_NONE = 0,
    ACTFAULT_RUNNING_QUERY,
    ACTFAULT_RESULT_QUERY,
    ACTFAULT_WORKER,
    ACTFAULT_DORMANT
} actfault_mode_t;

static actfault_mode_t g_mode  = ACTFAULT_NONE;
static int             g_error = 0;

/** @brief Fail one startup query, or report a dormant worker once. */
int actfault_running (rt_port_task_id_t h_task, bool * pb_running)
{
    if (ACTFAULT_RUNNING_QUERY == g_mode)
    {
        g_mode = ACTFAULT_NONE;
        return g_error;
    }
    if (ACTFAULT_DORMANT == g_mode)
    {
        g_mode      = ACTFAULT_NONE;
        *pb_running = false;
        return 0;
    }
    return rt_port_task_is_running(h_task, pb_running);
}

/** @brief Inject a failed result query or a successfully observed worker error.
 */
int actfault_result (rt_port_task_id_t h_task, int * p_result)
{
    if (ACTFAULT_RESULT_QUERY == g_mode)
    {
        g_mode = ACTFAULT_NONE;
        return g_error;
    }
    if (ACTFAULT_WORKER == g_mode)
    {
        g_mode    = ACTFAULT_NONE;
        *p_result = g_error;
        return 0;
    }
    return rt_port_task_result(h_task, p_result);
}

/** @brief Every failure restores the accepted plan and keeps its exact cause.
 */
void actfault_verify (const activate_plan_t * p_plan)
{
    const actfault_mode_t modes[]  = {ACTFAULT_RUNNING_QUERY,
                                      ACTFAULT_RESULT_QUERY,
                                      ACTFAULT_WORKER,
                                      ACTFAULT_DORMANT};
    const int             errors[] = {-71, -72, -73, -1};
    for (size_t index = 0U; index < (sizeof(modes) / sizeof(modes[0])); ++index)
    {
        activate_status_t before = {0};
        activate_status_t after  = {0};
        assert(0 == activate_get_status(&before));
        g_mode  = modes[index];
        g_error = errors[index];
        assert(UTHOME_ACTIVATE_REJECTED == activate_apply(p_plan));
        assert(ACTFAULT_NONE == g_mode);
        assert(0 == activate_get_status(&after));
        assert(errors[index] == after.apply_error);
        assert(0 == after.recovery_error);
        assert(ACTIVATE_ACTIVE == after.state);
        assert(before.retained_instances == after.retained_instances);
        assert(0U == after.failed_workers);
    }
}

/* End of file. */

