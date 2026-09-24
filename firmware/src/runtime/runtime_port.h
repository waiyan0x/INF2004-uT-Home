#ifndef UTHOME_RUNTIME_PORT_H
#define UTHOME_RUNTIME_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/*
 * Internal runtime-port task identifier.
 *
 * This is intentionally NOT a μT-Kernel
 * task type. The real kernel implementation
 * will map this to its own task object.
 */
typedef int32_t uthome_runtime_port_task_id_t;


/*
 * Configuration required to prepare
 * one runtime task.
 */
typedef struct {
    uint8_t priority;
    size_t stack_size_bytes;
    uint32_t period_ms;

} uthome_runtime_port_task_config_t;


/*
 * Initialise the selected runtime port.
 */
int uthome_runtime_port_init(void);


/*
 * Prepare/create one task resource.
 *
 * The task must not begin execution yet.
 */
int uthome_runtime_port_task_prepare(
    const uthome_runtime_port_task_config_t *config,
    uthome_runtime_port_task_id_t *task_id
);


/*
 * Start a prepared task.
 */
int uthome_runtime_port_task_start(
    uthome_runtime_port_task_id_t task_id
);


/*
 * Stop and release a task.
 */
int uthome_runtime_port_task_stop(
    uthome_runtime_port_task_id_t task_id
);


/*
 * Update safe runtime parameters.
 */
int uthome_runtime_port_task_reconfigure(
    uthome_runtime_port_task_id_t task_id,
    const uthome_runtime_port_task_config_t *config
);


/*
 * Query whether a task exists.
 */
int uthome_runtime_port_task_is_prepared(
    uthome_runtime_port_task_id_t task_id,
    bool *prepared
);


/*
 * Query whether a task is running.
 */
int uthome_runtime_port_task_is_running(
    uthome_runtime_port_task_id_t task_id,
    bool *running
);


#endif
