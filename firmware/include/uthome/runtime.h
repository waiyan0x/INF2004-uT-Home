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
    uint32_t failure_count;
    uint32_t queue_drop_count;

    uint32_t last_jitter_ms;
    uint32_t max_jitter_ms;

} uthome_runtime_status_t;


/*
 * Snapshot of bounded runtime resources.
 *
 * configured_stack_bytes represents
 * configured stack budget, not actual
 * μT-Kernel stack high-water usage.
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
 *
 * Returns:
 *   0  success
 *  -1  invalid device
 *  -2  registry full
 *  -3  duplicate device ID
 */
int uthome_runtime_register_device(
    const uthome_runtime_device_t *device
);


/*
 * Prepare a device.
 *
 * Reserves its bounded task resource
 * without starting execution.
 *
 * Returns:
 *   0  success
 *  -1  device not found
 *  -2  device disabled
 *  -3  no task resource available
 */
int uthome_runtime_prepare_device(
    const char *id
);


/*
 * Start a prepared device.
 *
 * If it has not already been prepared,
 * start performs preparation automatically.
 */
int uthome_runtime_start_device(
    const char *id
);


/*
 * Stop a device and release its
 * prepared task resource.
 */
int uthome_runtime_stop_device(
    const char *id
);


/*
 * Update an existing device configuration.
 *
 * Kept for compatibility with the
 * existing runtime API/tests.
 */
int uthome_runtime_update_device(
    const char *id,
    const uthome_runtime_device_t *device
);


/*
 * Lifecycle-facing reconfiguration API.
 *
 * Saves the previous known-good
 * configuration for possible rollback.
 */
int uthome_runtime_reconfigure_device(
    const char *id,
    const uthome_runtime_device_t *device
);


/*
 * Restore the last-known-good
 * configuration.
 *
 * Returns:
 *   0  success
 *  -1  device not found
 *  -2  no rollback configuration available
 *  -3  unable to restore task resource
 */
int uthome_runtime_rollback_device(
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
 * Advance the simulated runtime clock.
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