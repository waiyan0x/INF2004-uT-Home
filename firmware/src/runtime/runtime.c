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
 * Represents one bounded runtime task resource.
 *
 * This is currently platform-independent.
 * Later this structure can hold the real
 * μT-Kernel task ID / task handle.
 */
typedef struct {
    bool in_use;

    size_t device_index;

    uint8_t priority;
    size_t stack_size_bytes;

    /*
     * Periodic scheduling information.
     */
    uint32_t period_ms;
    uint32_t elapsed_ms;

    /*
     * Timing measurement information.
     *
     * Used to calculate execution jitter.
     */
    uint32_t last_execution_time_ms;
    bool has_execution_time;

} runtime_task_slot_t;


/*
 * Fixed number of task resources.
 */
static runtime_task_slot_t
    g_tasks[UTHOME_RUNTIME_MAX_DEVICE_TASKS];


/*
 * Find a registered device using its ID.
 *
 * Returns:
 *   >= 0 : device array index
 *   -1   : device not found / invalid ID
 */
static int find_device(const char *id)
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
 * Find the runtime task belonging
 * to a registered device.
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
 */
static int allocate_task(
    size_t device_index)
{
    for (size_t i = 0;
         i < UTHOME_RUNTIME_MAX_DEVICE_TASKS;
         ++i) {

        if (!g_tasks[i].in_use) {

            /*
             * Claim this task slot.
             */
            g_tasks[i].in_use = true;

            g_tasks[i].device_index =
                device_index;


            /*
             * Copy the configured runtime
             * parameters into the task.
             */
            g_tasks[i].priority =
                g_devices[device_index].priority;

            g_tasks[i].stack_size_bytes =
                g_devices[device_index]
                    .stack_size_bytes;

            g_tasks[i].period_ms =
                g_devices[device_index].period_ms;


            /*
             * Start the periodic timer
             * from zero.
             */
            g_tasks[i].elapsed_ms = 0;


            /*
             * No real execution timestamp
             * has been recorded yet.
             */
            g_tasks[i].last_execution_time_ms = 0;
            g_tasks[i].has_execution_time = false;

            return (int)i;
        }
    }

    /*
     * No task resources available.
     */
    return -1;
}


/*
 * Release a task slot when a device
 * stops or becomes disabled.
 */
static void release_task(
    size_t device_index)
{
    int task_index =
        find_task_for_device(device_index);

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
 * Update the runtime configuration
 * of an already-running task.
 */
static void update_task_configuration(
    size_t device_index)
{
    int task_index =
        find_task_for_device(device_index);

    if (task_index < 0) {
        return;
    }


    /*
     * Copy updated configuration.
     */
    g_tasks[task_index].priority =
        g_devices[device_index].priority;

    g_tasks[task_index].stack_size_bytes =
        g_devices[device_index]
            .stack_size_bytes;

    g_tasks[task_index].period_ms =
        g_devices[device_index].period_ms;


    /*
     * Begin a fresh timing interval
     * after configuration changes.
     *
     * Example:
     * period changes from 2000 ms
     * to 5000 ms.
     */
    g_tasks[task_index].elapsed_ms = 0;


    /*
     * Reset execution timing reference
     * because the configured period
     * may have changed.
     */
    g_tasks[task_index]
        .last_execution_time_ms = 0;

    g_tasks[task_index]
        .has_execution_time = false;
}


/*
 * Validate a device configuration.
 */
static int valid_device(
    const uthome_runtime_device_t *device)
{
    return device != NULL

        /*
         * Device ID must not be empty.
         */
        && device->id[0] != '\0'

        /*
         * The fixed-size ID buffer must
         * contain a null terminator.
         */
        && memchr(
            device->id,
            '\0',
            sizeof(device->id)
        ) != NULL;
}


/*
 * Initialise the runtime manager.
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

    g_device_count = 0;

    return 0;
}


/*
 * Register one configured device.
 */
int uthome_runtime_register_device(
    const uthome_runtime_device_t *device)
{
    /*
     * Invalid configuration.
     */
    if (!valid_device(device)) {
        return -1;
    }


    /*
     * Device registry is full.
     */
    if (g_device_count >=
        UTHOME_RUNTIME_MAX_DEVICES) {

        return -2;
    }


    /*
     * Duplicate device IDs are not allowed.
     */
    if (find_device(device->id) >= 0) {
        return -3;
    }


    /*
     * Copy the device configuration into
     * bounded runtime storage.
     */
    g_devices[g_device_count] =
        *device;


    /*
     * Enabled devices begin READY.
     *
     * Disabled devices begin STOPPED.
     */
    g_status[g_device_count].state =
        device->enabled
            ? UTHOME_RUNTIME_READY
            : UTHOME_RUNTIME_STOPPED;


    ++g_device_count;

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


    /*
     * Device does not exist.
     */
    if (index < 0) {
        return -1;
    }


    /*
     * Disabled devices cannot start.
     */
    if (!g_devices[index].enabled) {
        return -2;
    }


    /*
     * Starting an already-running device
     * is treated as successful.
     */
    if (g_status[index].state ==
        UTHOME_RUNTIME_RUNNING) {

        return 0;
    }


    /*
     * Allocate one bounded task resource.
     */
    if (allocate_task(
            (size_t)index
        ) < 0) {

        /*
         * No task slot available.
         */
        return -3;
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


    /*
     * Release its task resource.
     */
    release_task(
        (size_t)index
    );


    g_status[index].state =
        UTHOME_RUNTIME_STOPPED;

    return 0;
}


/*
 * Update a registered device's
 * configuration.
 */
int uthome_runtime_update_device(
    const char *id,
    const uthome_runtime_device_t *device)
{
    int index =
        find_device(id);


    /*
     * Validate:
     *
     * - device exists
     * - new configuration is valid
     * - device ID cannot change during update
     */
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


    /*
     * Store the new configuration.
     */
    g_devices[index] =
        *device;


    /*
     * Disabling a device immediately
     * stops it and releases its task slot.
     */
    if (!device->enabled) {

        release_task(
            (size_t)index
        );

        g_status[index].state =
            UTHOME_RUNTIME_STOPPED;

        return 0;
    }


    /*
     * If the device was already running,
     * update its task configuration.
     */
    if (was_running) {

        update_task_configuration(
            (size_t)index
        );

        g_status[index].state =
            UTHOME_RUNTIME_RUNNING;

        return 0;
    }


    /*
     * Device is enabled but not running.
     */
    g_status[index].state =
        UTHOME_RUNTIME_READY;

    return 0;
}


/*
 * Retrieve the runtime status
 * for one device.
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
 * Advance the simulated runtime clock.
 *
 * This is used for host-side testing.
 *
 * Later, the same behaviour can be
 * implemented using μT-Kernel timer /
 * cyclic-handler facilities.
 */
int uthome_runtime_tick(
    uint32_t elapsed_ms)
{
    for (size_t i = 0;
         i < UTHOME_RUNTIME_MAX_DEVICE_TASKS;
         ++i) {


        /*
         * Ignore unused task slots.
         */
        if (!g_tasks[i].in_use) {
            continue;
        }


        /*
         * period_ms == 0 means there is
         * currently no periodic schedule.
         */
        if (g_tasks[i].period_ms == 0) {
            continue;
        }


        /*
         * Use 64-bit arithmetic so adding
         * elapsed times cannot overflow
         * before division.
         */
        uint64_t total_elapsed =
            (uint64_t)
                g_tasks[i].elapsed_ms
            +
            elapsed_ms;


        /*
         * Determine how many complete
         * periods have elapsed.
         */
        uint32_t executions =
            (uint32_t)(
                total_elapsed /
                g_tasks[i].period_ms
            );


        /*
         * Preserve the remaining partial
         * interval for the next tick.
         */
        g_tasks[i].elapsed_ms =
            (uint32_t)(
                total_elapsed %
                g_tasks[i].period_ms
            );


        /*
         * Record how many executions
         * became due.
         */
        if (executions > 0) {

            size_t device_index =
                g_tasks[i].device_index;

            g_status[device_index]
                .run_count += executions;
        }
    }

    return 0;
}


/*
 * Record the real execution time
 * of a running device.
 *
 * This allows the runtime to calculate
 * scheduling jitter.
 */
int uthome_runtime_record_execution(
    const char *id,
    uint32_t now_ms)
{
    int device_index =
        find_device(id);


    /*
     * Device does not exist.
     */
    if (device_index < 0) {
        return -1;
    }


    /*
     * Timing measurements are only valid
     * for currently running devices.
     */
    if (g_status[device_index].state !=
        UTHOME_RUNTIME_RUNNING) {

        return -2;
    }


    /*
     * Find the task resource associated
     * with this device.
     */
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
     * First execution:
     *
     * There is no previous execution
     * timestamp to compare against,
     * therefore we simply establish
     * the reference time.
     */
    if (!task->has_execution_time) {

        task->last_execution_time_ms =
            now_ms;

        task->has_execution_time = true;

        return 0;
    }


    /*
     * Measure actual time between
     * executions.
     */
    uint32_t actual_interval =
        now_ms -
        task->last_execution_time_ms;


    /*
     * Store current execution time for
     * the next measurement.
     */
    task->last_execution_time_ms =
        now_ms;


    /*
     * Jitter is the absolute difference
     * between:
     *
     * actual execution interval
     *
     * and
     *
     * configured expected period.
     */
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


    /*
     * Record the latest jitter value.
     */
    g_status[device_index]
        .last_jitter_ms =
        jitter_ms;


    /*
     * Record worst observed jitter.
     */
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
 * Retrieve current bounded runtime
 * resource usage.
 */
int uthome_runtime_get_resources(
    uthome_runtime_resource_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return -1;
    }

    size_t active_tasks = 0;
    size_t configured_stack_bytes = 0;

    /*
     * Count active task slots and
     * total configured stack budget.
     */
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
 * Return the number of registered devices.
 */
size_t uthome_runtime_device_count(void)
{
    return g_device_count;
}