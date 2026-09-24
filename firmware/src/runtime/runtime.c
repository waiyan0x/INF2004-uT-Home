#include "uthome/runtime.h"

#include <string.h>


/*
 * Registered device definitions.
 */
static uthome_runtime_device_t
    g_devices[UTHOME_RUNTIME_MAX_DEVICES];


/*
 * Runtime status for each registered device.
 */
static uthome_runtime_status_t
    g_status[UTHOME_RUNTIME_MAX_DEVICES];


/*
 * Number of registered devices.
 */
static size_t g_device_count;


/*
 * Whether each device currently owns
 * a prepared task resource.
 */
static bool
    g_prepared[UTHOME_RUNTIME_MAX_DEVICES];


/*
 * Last-known-good configuration/state
 * used for rollback.
 */
static uthome_runtime_device_t
    g_rollback_devices[UTHOME_RUNTIME_MAX_DEVICES];

static uthome_runtime_state_t
    g_rollback_states[UTHOME_RUNTIME_MAX_DEVICES];

static bool
    g_rollback_prepared[UTHOME_RUNTIME_MAX_DEVICES];

static bool
    g_has_rollback[UTHOME_RUNTIME_MAX_DEVICES];


/*
 * One bounded runtime task resource.
 *
 * This is currently host/platform independent.
 * Later it can hold the actual μT-Kernel task ID.
 */
typedef struct {
    bool in_use;

    size_t device_index;

    uint8_t priority;
    size_t stack_size_bytes;

    uint32_t period_ms;
    uint32_t elapsed_ms;

    uint32_t last_execution_time_ms;
    bool has_execution_time;

} runtime_task_slot_t;


/*
 * Fixed task-resource pool.
 */
static runtime_task_slot_t
    g_tasks[UTHOME_RUNTIME_MAX_DEVICE_TASKS];


/*
 * Find a registered device.
 */
static int find_device(
    const char *id)
{
    if (id == NULL) {
        return -1;
    }

    for (size_t i = 0;
         i < g_device_count;
         ++i) {

        if (strcmp(
                g_devices[i].id,
                id
            ) == 0) {

            return (int)i;
        }
    }

    return -1;
}


/*
 * Find the task belonging to a device.
 */
static int find_task_for_device(
    size_t device_index)
{
    for (size_t i = 0;
         i < UTHOME_RUNTIME_MAX_DEVICE_TASKS;
         ++i) {

        if (g_tasks[i].in_use &&
            g_tasks[i].device_index ==
                device_index) {

            return (int)i;
        }
    }

    return -1;
}


/*
 * Allocate one bounded task slot.
 *
 * A device must never own more than
 * one task slot.
 */
static int allocate_task(
    size_t device_index)
{
    /*
     * Prevent duplicate task allocation.
     */
    int existing_task =
        find_task_for_device(
            device_index
        );

    if (existing_task >= 0) {
        return existing_task;
    }


    for (size_t i = 0;
         i < UTHOME_RUNTIME_MAX_DEVICE_TASKS;
         ++i) {

        if (!g_tasks[i].in_use) {

            g_tasks[i].in_use = true;

            g_tasks[i].device_index =
                device_index;

            g_tasks[i].priority =
                g_devices[device_index].priority;

            g_tasks[i].stack_size_bytes =
                g_devices[device_index]
                    .stack_size_bytes;

            g_tasks[i].period_ms =
                g_devices[device_index]
                    .period_ms;

            g_tasks[i].elapsed_ms = 0;

            g_tasks[i]
                .last_execution_time_ms = 0;

            g_tasks[i]
                .has_execution_time = false;

            return (int)i;
        }
    }

    return -1;
}


/*
 * Release the task owned by a device.
 */
static void release_task(
    size_t device_index)
{
    int task_index =
        find_task_for_device(
            device_index
        );

    if (task_index < 0) {
        return;
    }

    memset(
        &g_tasks[task_index],
        0,
        sizeof(g_tasks[task_index])
    );
}


/*
 * Update an existing task slot using
 * the latest device configuration.
 */
static void update_task_configuration(
    size_t device_index)
{
    int task_index =
        find_task_for_device(
            device_index
        );

    if (task_index < 0) {
        return;
    }


    g_tasks[task_index].priority =
        g_devices[device_index].priority;

    g_tasks[task_index].stack_size_bytes =
        g_devices[device_index]
            .stack_size_bytes;

    g_tasks[task_index].period_ms =
        g_devices[device_index]
            .period_ms;


    /*
     * Restart timer tracking after
     * reconfiguration.
     */
    g_tasks[task_index].elapsed_ms = 0;


    /*
     * Reset jitter reference because
     * the period may have changed.
     */
    g_tasks[task_index]
        .last_execution_time_ms = 0;

    g_tasks[task_index]
        .has_execution_time = false;
}


/*
 * Validate a device definition.
 */
static int valid_device(
    const uthome_runtime_device_t *device)
{
    return device != NULL
        &&
        device->id[0] != '\0'
        &&
        memchr(
            device->id,
            '\0',
            sizeof(device->id)
        ) != NULL;
}


/*
 * Initialise runtime.
 */
int uthome_runtime_init(void)
{
    memset(
        g_devices,
        0,
        sizeof(g_devices)
    );

    memset(
        g_status,
        0,
        sizeof(g_status)
    );

    memset(
        g_tasks,
        0,
        sizeof(g_tasks)
    );

    memset(
        g_prepared,
        0,
        sizeof(g_prepared)
    );

    memset(
        g_rollback_devices,
        0,
        sizeof(g_rollback_devices)
    );

    memset(
        g_rollback_states,
        0,
        sizeof(g_rollback_states)
    );

    memset(
        g_rollback_prepared,
        0,
        sizeof(g_rollback_prepared)
    );

    memset(
        g_has_rollback,
        0,
        sizeof(g_has_rollback)
    );

    g_device_count = 0;

    return 0;
}


/*
 * Register one device.
 */
int uthome_runtime_register_device(
    const uthome_runtime_device_t *device)
{
    if (!valid_device(device)) {
        return -1;
    }


    if (g_device_count >=
        UTHOME_RUNTIME_MAX_DEVICES) {

        return -2;
    }


    if (find_device(device->id) >= 0) {
        return -3;
    }


    g_devices[g_device_count] =
        *device;


    g_status[g_device_count].state =
        device->enabled
            ? UTHOME_RUNTIME_READY
            : UTHOME_RUNTIME_STOPPED;


    ++g_device_count;

    return 0;
}


/*
 * Prepare a device.
 *
 * Reserve a bounded task resource without
 * beginning execution.
 */
int uthome_runtime_prepare_device(
    const char *id)
{
    int index =
        find_device(id);


    if (index < 0) {
        return -1;
    }


    if (!g_devices[index].enabled) {
        return -2;
    }


    /*
     * Already prepared.
     */
    if (g_prepared[index]) {
        return 0;
    }


    if (allocate_task(
            (size_t)index
        ) < 0) {

        return -3;
    }


    g_prepared[index] = true;

    g_status[index].state =
        UTHOME_RUNTIME_READY;


    return 0;
}


/*
 * Start a registered device.
 */
int uthome_runtime_start_device(
    const char *id)
{
    int index =
        find_device(id);


    if (index < 0) {
        return -1;
    }


    if (!g_devices[index].enabled) {
        return -2;
    }


    /*
     * Already running.
     */
    if (g_status[index].state ==
        UTHOME_RUNTIME_RUNNING) {

        return 0;
    }


    /*
     * Automatically prepare if needed.
     *
     * IMPORTANT:
     * Do not allocate another task here.
     */
    if (!g_prepared[index]) {

        int result =
            uthome_runtime_prepare_device(
                id
            );

        if (result != 0) {
            return result;
        }
    }


    g_status[index].state =
        UTHOME_RUNTIME_RUNNING;


    return 0;
}


/*
 * Stop a registered device.
 */
int uthome_runtime_stop_device(
    const char *id)
{
    int index =
        find_device(id);


    if (index < 0) {
        return -1;
    }


    release_task(
        (size_t)index
    );


    g_prepared[index] = false;


    g_status[index].state =
        UTHOME_RUNTIME_STOPPED;


    return 0;
}


/*
 * Update an existing device configuration.
 *
 * Saves the current configuration for
 * possible rollback.
 */
int uthome_runtime_update_device(
    const char *id,
    const uthome_runtime_device_t *device)
{
    int index =
        find_device(id);


    if (index < 0 ||
        !valid_device(device) ||
        strcmp(
            device->id,
            id
        ) != 0) {

        return -1;
    }


    bool was_running =
        g_status[index].state ==
        UTHOME_RUNTIME_RUNNING;

    bool was_prepared =
        g_prepared[index];


    /*
     * Save previous known-good state.
     */
    g_rollback_devices[index] =
        g_devices[index];

    g_rollback_states[index] =
        g_status[index].state;

    g_rollback_prepared[index] =
        g_prepared[index];

    g_has_rollback[index] = true;


    /*
     * Apply new configuration.
     */
    g_devices[index] =
        *device;


    /*
     * Disabled configuration.
     */
    if (!device->enabled) {

        release_task(
            (size_t)index
        );

        g_prepared[index] = false;

        g_status[index].state =
            UTHOME_RUNTIME_STOPPED;

        return 0;
    }


    /*
     * Device already owned a prepared task.
     */
    if (was_running ||
        was_prepared) {

        int task_index =
            find_task_for_device(
                (size_t)index
            );


        if (task_index < 0) {

            if (allocate_task(
                    (size_t)index
                ) < 0) {

                g_status[index].state =
                    UTHOME_RUNTIME_ERROR;

                ++g_status[index]
                    .failure_count;

                return -2;
            }

        } else {

            update_task_configuration(
                (size_t)index
            );
        }


        g_prepared[index] = true;


        g_status[index].state =
            was_running
                ? UTHOME_RUNTIME_RUNNING
                : UTHOME_RUNTIME_READY;


        return 0;
    }


    /*
     * Registered but not prepared.
     */
    g_prepared[index] = false;

    g_status[index].state =
        UTHOME_RUNTIME_READY;


    return 0;
}


/*
 * Lifecycle-facing reconfiguration API.
 */
int uthome_runtime_reconfigure_device(
    const char *id,
    const uthome_runtime_device_t *device)
{
    return uthome_runtime_update_device(
        id,
        device
    );
}


/*
 * Restore last-known-good configuration.
 */
int uthome_runtime_rollback_device(
    const char *id)
{
    int index =
        find_device(id);


    if (index < 0) {
        return -1;
    }


    if (!g_has_rollback[index]) {
        return -2;
    }


    /*
     * Restore old configuration first.
     */
    g_devices[index] =
        g_rollback_devices[index];


    uthome_runtime_state_t restored_state =
        g_rollback_states[index];

    bool restored_prepared =
        g_rollback_prepared[index];


    bool needs_task =
        restored_prepared ||
        restored_state ==
            UTHOME_RUNTIME_RUNNING;


    if (needs_task) {

        int task_index =
            find_task_for_device(
                (size_t)index
            );


        if (task_index < 0) {

            if (allocate_task(
                    (size_t)index
                ) < 0) {

                g_status[index].state =
                    UTHOME_RUNTIME_ERROR;

                ++g_status[index]
                    .failure_count;

                return -3;
            }

        } else {

            update_task_configuration(
                (size_t)index
            );
        }


        g_prepared[index] = true;

    } else {

        release_task(
            (size_t)index
        );

        g_prepared[index] = false;
    }


    g_status[index].state =
        restored_state;


    /*
     * Rollback point has been consumed.
     */
    g_has_rollback[index] = false;


    return 0;
}


/*
 * Record one runtime failure.
 *
 * The device enters ERROR state and
 * failure_count is increased.
 */
int uthome_runtime_record_failure(
    const char *id)
{
    int index =
        find_device(id);


    /*
     * Device does not exist.
     */
    if (index < 0) {
        return -1;
    }


    /*
     * Count runtime failure.
     */
    ++g_status[index].failure_count;


    /*
     * Move device into ERROR state.
     */
    g_status[index].state =
        UTHOME_RUNTIME_ERROR;


    return 0;
}


/*
 * Restart a device after a runtime failure.
 *
 * The old task resource is released,
 * a fresh task is prepared and the
 * device returns to RUNNING.
 */
int uthome_runtime_restart_device(
    const char *id)
{
    int index =
        find_device(id);


    /*
     * Device does not exist.
     */
    if (index < 0) {
        return -1;
    }


    /*
     * Disabled devices cannot restart.
     */
    if (!g_devices[index].enabled) {
        return -2;
    }


    /*
     * Release the old task resource.
     */
    release_task(
        (size_t)index
    );


    g_prepared[index] = false;


    /*
     * Prepare a fresh bounded task slot.
     */
    int result =
        uthome_runtime_prepare_device(
            id
        );


    if (result != 0) {

        g_status[index].state =
            UTHOME_RUNTIME_ERROR;

        return -3;
    }


    /*
     * Start the newly prepared task.
     */
    result =
        uthome_runtime_start_device(
            id
        );


    if (result != 0) {

        /*
         * Clean up the newly prepared
         * resource if starting fails.
         */
        release_task(
            (size_t)index
        );

        g_prepared[index] = false;

        g_status[index].state =
            UTHOME_RUNTIME_ERROR;

        return -3;
    }


    /*
     * Successful restart.
     */
    ++g_status[index].restart_count;


    return 0;
}


/*
 * Retrieve status for one device.
 */
int uthome_runtime_get_status(
    const char *id,
    uthome_runtime_status_t *status)
{
    int index =
        find_device(id);


    if (index < 0 ||
        status == NULL) {

        return -1;
    }


    *status =
        g_status[index];


    return 0;
}


/*
 * Advance simulated runtime time.
 */
int uthome_runtime_tick(
    uint32_t elapsed_ms)
{
    for (size_t i = 0;
         i < UTHOME_RUNTIME_MAX_DEVICE_TASKS;
         ++i) {


        if (!g_tasks[i].in_use) {
            continue;
        }


        size_t device_index =
            g_tasks[i].device_index;


        /*
         * Prepared-but-not-running tasks
         * must not execute.
         */
        if (g_status[device_index].state !=
            UTHOME_RUNTIME_RUNNING) {

            continue;
        }


        if (g_tasks[i].period_ms == 0) {
            continue;
        }


        uint64_t total_elapsed =
            (uint64_t)
                g_tasks[i].elapsed_ms
            +
            elapsed_ms;


        uint32_t executions =
            (uint32_t)(
                total_elapsed /
                g_tasks[i].period_ms
            );


        g_tasks[i].elapsed_ms =
            (uint32_t)(
                total_elapsed %
                g_tasks[i].period_ms
            );


        if (executions > 0) {

            g_status[device_index]
                .run_count += executions;
        }
    }


    return 0;
}


/*
 * Record an actual task execution time
 * and calculate scheduling jitter.
 */
int uthome_runtime_record_execution(
    const char *id,
    uint32_t now_ms)
{
    int device_index =
        find_device(id);


    if (device_index < 0) {
        return -1;
    }


    if (g_status[device_index].state !=
        UTHOME_RUNTIME_RUNNING) {

        return -2;
    }


    int task_index =
        find_task_for_device(
            (size_t)device_index
        );


    if (task_index < 0) {
        return -3;
    }


    runtime_task_slot_t *task =
        &g_tasks[task_index];


    /*
     * First execution establishes
     * the reference timestamp.
     */
    if (!task->has_execution_time) {

        task->last_execution_time_ms =
            now_ms;

        task->has_execution_time = true;

        return 0;
    }


    uint32_t actual_interval =
        now_ms -
        task->last_execution_time_ms;


    task->last_execution_time_ms =
        now_ms;


    uint32_t jitter_ms;


    if (actual_interval >=
        task->period_ms) {

        jitter_ms =
            actual_interval -
            task->period_ms;

    } else {

        jitter_ms =
            task->period_ms -
            actual_interval;
    }


    g_status[device_index]
        .last_jitter_ms =
        jitter_ms;


    if (jitter_ms >
        g_status[device_index]
            .max_jitter_ms) {

        g_status[device_index]
            .max_jitter_ms =
            jitter_ms;
    }


    return 0;
}


/*
 * Retrieve bounded runtime-resource usage.
 */
int uthome_runtime_get_resources(
    uthome_runtime_resource_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return -1;
    }


    size_t active_tasks = 0;
    size_t configured_stack_bytes = 0;


    for (size_t i = 0;
         i < UTHOME_RUNTIME_MAX_DEVICE_TASKS;
         ++i) {

        if (!g_tasks[i].in_use) {
            continue;
        }


        ++active_tasks;


        configured_stack_bytes +=
            g_tasks[i].stack_size_bytes;
    }


    snapshot->registered_devices =
        g_device_count;

    snapshot->active_tasks =
        active_tasks;

    snapshot->max_tasks =
        UTHOME_RUNTIME_MAX_DEVICE_TASKS;

    snapshot->free_task_slots =
        UTHOME_RUNTIME_MAX_DEVICE_TASKS -
        active_tasks;

    snapshot->configured_stack_bytes =
        configured_stack_bytes;


    return 0;
}


/*
 * Return number of registered devices.
 */
size_t uthome_runtime_device_count(void)
{
    return g_device_count;
}