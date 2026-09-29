/**
 * @file rt_port.h
 * @brief Declare the shared lifecycle ABI implemented by one selected backend.
 */
#ifndef UTHOME_RT_PORT_H
#define UTHOME_RT_PORT_H

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
typedef int32_t rt_port_task_id_t;

/*
 * Configuration required to prepare
 * one runtime task.
 */
typedef struct
{
    uint8_t  priority;
    size_t   stack_size_bytes;
    uint32_t period_ms;

} rt_port_task_config_t;

/*
 * Initialise the selected runtime port.
 */
int rt_port_init (void);

/*
 * Prepare/create one task resource.
 *
 * The task must not begin execution yet.
 */
int rt_port_task_prepare (const rt_port_task_config_t * p_config,
                          rt_port_task_id_t *           ph_task_id);

/*
 * Start a prepared task.
 */
int rt_port_task_start (rt_port_task_id_t h_task_id);

/*
 * Stop and release a task.
 */
int rt_port_task_stop (rt_port_task_id_t h_task_id);

/*
 * Update safe runtime parameters.
 */
int rt_port_task_reconfigure (rt_port_task_id_t             h_task_id,
                              const rt_port_task_config_t * p_config);

/*
 * Query whether a task exists.
 */
int rt_port_task_is_prepared (rt_port_task_id_t h_task_id, bool * pb_prepared);

/*
 * Query whether a task is running.
 */
int rt_port_task_is_running (rt_port_task_id_t h_task_id, bool * pb_running);

/* Host checkpoint contract: all task-resource metadata must be restored on
 * rollback, including tasks released or created since begin. Not a hardware
 * rollback implementation; unsupported backends reject begin before mutation.
 */
int rt_port_transaction_begin (void);
int rt_port_transaction_commit (void);
int rt_port_transaction_rollback (void);

#endif

/* End of file. */

