#include "uthome/runtime.h"

#include <string.h>


static uthome_runtime_device_t g_devices[UTHOME_RUNTIME_MAX_DEVICES];
static uthome_runtime_status_t g_status[UTHOME_RUNTIME_MAX_DEVICES];
static size_t g_device_count;


/*
 * Represents one bounded runtime task resource.
 *
 * This is not yet a real μT-Kernel task.
 * Later, this can hold the μT-Kernel task ID/handle.
 */
typedef struct {
    bool in_use;

    size_t device_index;

    uint8_t priority;
    size_t stack_size_bytes;

    uint32_t period_ms;
    uint32_t elapsed_ms;
} runtime_task_slot_t;


static runtime_task_slot_t
    g_tasks[UTHOME_RUNTIME_MAX_DEVICE_TASKS];


/*
 * Find a registered device using its ID.
 */
static int find_device(const char *id)
{
    if (id == NULL) {
        return -1;
    }

    for (size_t i = 0; i < g_device_count; ++i) {
        if (strcmp(g_devices[i].id, id) == 0) {
            return (int)i;
        }
    }

    return -1;
}


/*
 * Find the task belonging to a device.
 */
static int find_task_for_device(size_t device_index)
{
    for (size_t i = 0;
         i < UTHOME_RUNTIME_MAX_DEVICE_TASKS;
         ++i) {

        if (g_tasks[i].in_use &&
            g_tasks[i].device_index == device_index) {

            return (int)i;
        }
    }

    return -1;
}


/*
 * Allocate one bounded task slot.
 */
static int allocate_task(size_t device_index)
{
    for (size_t i = 0;
         i < UTHOME_RUNTIME_MAX_DEVICE_TASKS;
         ++i) {

        if (!g_tasks[i].in_use) {

            g_tasks[i].in_use = true;
            g_tasks[i].device_index = device_index;

            g_tasks[i].priority =
                g_devices[device_index].priority;

            g_tasks[i].stack_size_bytes =
                g_devices[device_index].stack_size_bytes;

            g_tasks[i].period_ms =
                g_devices[device_index].period_ms;

            g_tasks[i].elapsed_ms = 0;

            return (int)i;
        }
    }

    return -1;
}


/*
 * Release a task slot when a device stops.
 */
static void release_task(size_t device_index)
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
 * Update an existing task slot when configuration changes.
 */
static void update_task_configuration(
    size_t device_index)
{
    int task_index =
        find_task_for_device(device_index);

    if (task_index < 0) {
        return;
    }

    g_tasks[task_index].priority =
        g_devices[device_index].priority;

    g_tasks[task_index].stack_size_bytes =
        g_devices[device_index].stack_size_bytes;

    g_tasks[task_index].period_ms =
        g_devices[device_index].period_ms;

    /*
     * Reset the elapsed time whenever
     * the runtime configuration changes.
     *
     * Example:
     * 2000 ms period -> 5000 ms period
     *
     * The new 5000 ms interval begins
     * from zero.
     */
    g_tasks[task_index].elapsed_ms = 0;
}


/*
 * Basic validation for a device definition.
 */
static int valid_device(
    const uthome_runtime_device_t *device)
{
    return device != NULL
        && device->id[0] != '\0'
        && memchr(
            device->id,
            '\0',
            sizeof(device->id)
        ) != NULL;
}


int uthome_runtime_init(void)
{
    memset(g_devices, 0, sizeof(g_devices));
    memset(g_status, 0, sizeof(g_status));
    memset(g_tasks, 0, sizeof(g_tasks));

    g_device_count = 0;

    return 0;
}


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

    g_devices[g_device_count] = *device;

    g_status[g_device_count].state =
        device->enabled
            ? UTHOME_RUNTIME_READY
            : UTHOME_RUNTIME_STOPPED;

    ++g_device_count;

    return 0;
}


int uthome_runtime_start_device(const char *id)
{
    int index = find_device(id);

    if (index < 0) {
        return -1;
    }

    if (!g_devices[index].enabled) {
        return -2;
    }

    /*
     * Starting an already-running device
     * is treated as success.
     */
    if (g_status[index].state ==
        UTHOME_RUNTIME_RUNNING) {

        return 0;
    }

    /*
     * No task resource available.
     */
    if (allocate_task((size_t)index) < 0) {
        return -3;
    }

    g_status[index].state =
        UTHOME_RUNTIME_RUNNING;

    return 0;
}


int uthome_runtime_stop_device(const char *id)
{
    int index = find_device(id);

    if (index < 0) {
        return -1;
    }

    release_task((size_t)index);

    g_status[index].state =
        UTHOME_RUNTIME_STOPPED;

    return 0;
}


int uthome_runtime_update_device(
    const char *id,
    const uthome_runtime_device_t *device)
{
    int index = find_device(id);

    /*
     * Validate:
     * - device exists
     * - new configuration is valid
     * - ID cannot change during update
     */
    if (index < 0 ||
        !valid_device(device) ||
        strcmp(device->id, id) != 0) {

        return -1;
    }

    bool was_running =
        g_status[index].state ==
        UTHOME_RUNTIME_RUNNING;

    /*
     * Store the new configuration.
     */
    g_devices[index] = *device;


    /*
     * Disabling a device stops it
     * and releases its task resource.
     */
    if (!device->enabled) {

        release_task((size_t)index);

        g_status[index].state =
            UTHOME_RUNTIME_STOPPED;

        return 0;
    }


    /*
     * If the device was already running,
     * update the task configuration.
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
     * Enabled but not currently running.
     */
    g_status[index].state =
        UTHOME_RUNTIME_READY;

    return 0;
}


int uthome_runtime_get_status(
    const char *id,
    uthome_runtime_status_t *status)
{
    int index = find_device(id);

    if (index < 0 || status == NULL) {
        return -1;
    }

    *status = g_status[index];

    return 0;
}


/*
 * Advance the simulated runtime clock.
 *
 * For every running task:
 * - accumulate elapsed time
 * - determine how many periods expired
 * - increase run_count accordingly
 */
int uthome_runtime_tick(uint32_t elapsed_ms)
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
         * period_ms == 0 means this task
         * currently has no periodic schedule.
         */
        if (g_tasks[i].period_ms == 0) {
            continue;
        }

        /*
         * Use 64-bit arithmetic here so the
         * addition does not overflow uint32_t
         * before the division takes place.
         */
        uint64_t total_elapsed =
            (uint64_t)g_tasks[i].elapsed_ms +
            elapsed_ms;

        /*
         * Determine how many full periods
         * have passed.
         */
        uint32_t executions =
            (uint32_t)(
                total_elapsed /
                g_tasks[i].period_ms
            );

        /*
         * Keep the remaining partial period.
         */
        g_tasks[i].elapsed_ms =
            (uint32_t)(
                total_elapsed %
                g_tasks[i].period_ms
            );

        if (executions > 0) {

            size_t device_index =
                g_tasks[i].device_index;

            g_status[device_index].run_count +=
                executions;
        }
    }

    return 0;
}


size_t uthome_runtime_device_count(void)
{
    return g_device_count;
}