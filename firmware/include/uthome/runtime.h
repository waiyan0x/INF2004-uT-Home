#ifndef UTHOME_RUNTIME_H
#define UTHOME_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#define UTHOME_RUNTIME_MAX_DEVICES 8
#define UTHOME_RUNTIME_MAX_DEVICE_TASKS 4
#define UTHOME_DEVICE_ID_MAX 32


/*
 * Runtime lifecycle state.
 */
typedef enum {
    UTHOME_RUNTIME_STOPPED = 0,
    UTHOME_RUNTIME_READY,
    UTHOME_RUNTIME_RUNNING,
    UTHOME_RUNTIME_ERROR

} uthome_runtime_state_t;


/*
 * Generic configured device.
 */
typedef struct {
    char id[UTHOME_DEVICE_ID_MAX];

    uint32_t period_ms;
    uint8_t priority;
    size_t stack_size_bytes;

    bool enabled;

} uthome_runtime_device_t;


/*
 * Runtime status for one device.
 */
typedef struct {
    uthome_runtime_state_t state;

    uint32_t run_count;

    /*
     * Number of runtime failures recorded.
     */
    uint32_t failure_count;

    /*
     * Number of successful runtime restarts.
     */
    uint32_t restart_count;

    uint32_t queue_drop_count;

    /*
     * Scheduling jitter measurements.
     */
    uint32_t last_jitter_ms;
    uint32_t max_jitter_ms;

} uthome_runtime_status_t;


/*
 * Snapshot of bounded runtime resources.
 *
 * configured_stack_bytes is the configured
 * stack budget of active tasks.
 *
 * It is not yet the real μT-Kernel
 * stack high-water measurement.
 */
typedef struct {
    size_t registered_devices;

    size_t active_tasks;
    size_t max_tasks;
    size_t free_task_slots;

    size_t configured_stack_bytes;

} uthome_runtime_resource_snapshot_t;


/*
 * Initialise runtime state.
 */
int uthome_runtime_init(void);


/*
 * Register a device.
 */
int uthome_runtime_register_device(
    const uthome_runtime_device_t *device
);


/*
 * Prepare a device.
 *
 * Reserves a bounded task resource
 * without starting execution.
 */
int uthome_runtime_prepare_device(
    const char *id
);


/*
 * Start a device.
 *
 * If necessary, preparation is performed
 * automatically.
 */
int uthome_runtime_start_device(
    const char *id
);


/*
 * Stop a device and release its
 * task resource.
 */
int uthome_runtime_stop_device(
    const char *id
);


/*
 * Update an existing device configuration.
 */
int uthome_runtime_update_device(
    const char *id,
    const uthome_runtime_device_t *device
);


/*
 * Lifecycle-facing reconfiguration API.
 */
int uthome_runtime_reconfigure_device(
    const char *id,
    const uthome_runtime_device_t *device
);


/*
 * Restore the previous known-good
 * device configuration.
 */
int uthome_runtime_rollback_device(
    const char *id
);


/*
 * Record a runtime failure.
 *
 * Increments failure_count and moves
 * the device into ERROR state.
 */
int uthome_runtime_record_failure(
    const char *id
);


/*
 * Restart a device.
 *
 * Releases the old task, prepares a new
 * task and returns the device to RUNNING.
 */
int uthome_runtime_restart_device(
    const char *id
);


/*
 * Retrieve runtime status.
 */
int uthome_runtime_get_status(
    const char *id,
    uthome_runtime_status_t *status
);


/*
 * Advance the host-side simulated
 * runtime clock.
 */
int uthome_runtime_tick(
    uint32_t elapsed_ms
);


/*
 * Record an actual execution timestamp
 * for jitter measurement.
 */
int uthome_runtime_record_execution(
    const char *id,
    uint32_t now_ms
);


/*
 * Retrieve bounded runtime resource usage.
 */
int uthome_runtime_get_resources(
    uthome_runtime_resource_snapshot_t *snapshot
);


/*
 * Return number of registered devices.
 */
size_t uthome_runtime_device_count(void);


#endif