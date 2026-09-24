#include "runtime_port.h"

/*
 * μT-Kernel runtime port.
 *
 * IMPORTANT:
 * The actual course-approved μT-Kernel headers/API
 * have not been added to the repository yet.
 *
 * Do not guess tk_cre_tsk(), T_CTSK, task IDs,
 * stack units, or startup behaviour here.
 *
 * Once the approved port is available, this file
 * will implement the runtime_port.h contract.
 */


int uthome_runtime_port_init(void)
{
    /*
     * TODO:
     * Initialise / verify the approved
     * μT-Kernel environment.
     */
    return -1;
}


int uthome_runtime_port_task_prepare(
    const uthome_runtime_port_task_config_t *config,
    uthome_runtime_port_task_id_t *task_id)
{
    (void)config;
    (void)task_id;

    /*
     * TODO:
     * Map to the approved μT-Kernel
     * task creation/preparation API.
     */
    return -1;
}


int uthome_runtime_port_task_start(
    uthome_runtime_port_task_id_t task_id)
{
    (void)task_id;

    /*
     * TODO:
     * Map to approved kernel task-start API.
     */
    return -1;
}


int uthome_runtime_port_task_stop(
    uthome_runtime_port_task_id_t task_id)
{
    (void)task_id;

    /*
     * TODO:
     * Stop/delete/release according to the
     * approved kernel lifecycle.
     */
    return -1;
}


int uthome_runtime_port_task_reconfigure(
    uthome_runtime_port_task_id_t task_id,
    const uthome_runtime_port_task_config_t *config)
{
    (void)task_id;
    (void)config;

    /*
     * TODO:
     * Determine which parameters can safely
     * change in-place on the approved port.
     *
     * Stack-size changes may require task
     * recreation rather than live update.
     */
    return -1;
}


int uthome_runtime_port_task_is_prepared(
    uthome_runtime_port_task_id_t task_id,
    bool *prepared)
{
    (void)task_id;

    if (prepared == NULL) {
        return -1;
    }

    *prepared = false;

    return -1;
}


int uthome_runtime_port_task_is_running(
    uthome_runtime_port_task_id_t task_id,
    bool *running)
{
    (void)task_id;

    if (running == NULL) {
        return -1;
    }

    *running = false;

    return -1;
}