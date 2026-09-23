#ifndef UTHOME_RUNTIME_H
#define UTHOME_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define UTHOME_RUNTIME_MAX_DEVICES 8
#define UTHOME_RUNTIME_MAX_DEVICE_TASKS 4
#define UTHOME_DEVICE_ID_MAX 32


typedef enum {
    UTHOME_RUNTIME_STOPPED = 0,
    UTHOME_RUNTIME_READY,
    UTHOME_RUNTIME_RUNNING,
    UTHOME_RUNTIME_ERROR

} uthome_runtime_state_t;


typedef struct {
    char id[UTHOME_DEVICE_ID_MAX];

    uint32_t period_ms;
    uint8_t priority;
    size_t stack_size_bytes;

    bool enabled;

} uthome_runtime_device_t;


typedef struct {
    uthome_runtime_state_t state;

    uint32_t run_count;
    uint32_t failure_count;
    uint32_t queue_drop_count;

    uint32_t last_jitter_ms;
    uint32_t max_jitter_ms;

} uthome_runtime_status_t;


/*
 * Snapshot of current bounded runtime resources.
 *
 * Note:
 * configured_stack_bytes is the total configured
 * stack budget of active tasks.
 *
 * It is NOT actual stack high-water usage yet.
 */
typedef struct {
    size_t registered_devices;

    size_t active_tasks;
    size_t max_tasks;
    size_t free_task_slots;

    size_t configured_stack_bytes;

} uthome_runtime_resource_snapshot_t;


/*
 * Initialise the runtime.
 */
int uthome_runtime_init(void);


/*
 * Register one device.
 *
 * Returns:
 *   0  success
 *  -1  invalid device
 *  -2  device registry full
 *  -3  duplicate device ID
 */
int uthome_runtime_register_device(
    const uthome_runtime_device_t *device
);


/*
 * Start a registered device.
 *
 * Returns:
 *   0  success
 *  -1  device not found
 *  -2  device disabled
 *  -3  no task slot available
 */
int uthome_runtime_start_device(
    const char *id
);


/*
 * Stop a registered device.
 */
int uthome_runtime_stop_device(
    const char *id
);


/*
 * Update an existing device.
 *
 * The device ID must remain unchanged.
 */
int uthome_runtime_update_device(
    const char *id,
    const uthome_runtime_device_t *device
);


/*
 * Retrieve runtime status for one device.
 */
int uthome_runtime_get_status(
    const char *id,
    uthome_runtime_status_t *status
);


/*
 * Advance the simulated runtime clock.
 *
 * Used for host-side timer testing.
 */
int uthome_runtime_tick(
    uint32_t elapsed_ms
);


/*
 * Record the actual execution timestamp
 * of a running device.
 *
 * Used to calculate scheduling jitter.
 */
int uthome_runtime_record_execution(
    const char *id,
    uint32_t now_ms
);


/*
 * Retrieve current bounded runtime
 * resource usage.
 */
int uthome_runtime_get_resources(
    uthome_runtime_resource_snapshot_t *snapshot
);


/*
 * Return number of registered devices.
 */
size_t uthome_runtime_device_count(void);


#endif