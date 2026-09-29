/**
 * @file runtime.h
 * @brief Manage bounded configuration-driven component lifecycles.
 */
#ifndef UTHOME_RUNTIME_H
#define UTHOME_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "uthome/component.h"

#define UTHOME_RUNTIME_MAX_DEVICES         8U
#define UTHOME_RUNTIME_MAX_DEVICE_TASKS    4U
#define UTHOME_DEVICE_ID_MAX               32U
#define UTHOME_RUNTIME_DEVICE_STACK_BUDGET 16384U

typedef enum
{
    UTHOME_RUNTIME_STOPPED = 0,
    UTHOME_RUNTIME_READY,
    UTHOME_RUNTIME_RUNNING,
    UTHOME_RUNTIME_ERROR
} runtime_state_t;

typedef struct
{
    char     name[UTHOME_DEVICE_ID_MAX];
    uint32_t period_ms;
    uint8_t  priority;
    size_t   stack_size_bytes;
    bool     b_enabled;
} runtime_device_t;

typedef struct
{
    runtime_state_t state;
    uint32_t        run_count;
    uint32_t        failure_count;
    uint32_t        queue_drop_count;
    uint32_t        restart_count;
    uint32_t        last_jitter_ms;
    uint32_t        max_jitter_ms;
    uint32_t        skipped_periods;
} runtime_status_t;

int runtime_init (void);

int runtime_register_device (const runtime_device_t * p_device);

int runtime_start_device (const char * p_name);

int runtime_stop_device (const char * p_name);

int runtime_update_device (const char *             p_name,
                           const runtime_device_t * p_device);

int runtime_get_status (const char * p_name, runtime_status_t * p_status);

/* Host backend transactions preserve the active configuration on failure.
 * All lifecycle/dispatch calls are serialized. No driver I/O occurs in apply.
 * The target adapter remains fail-closed until its transaction contract exists.
 * Successful whole-plan replacement resets counters. */
int runtime_apply_devices (const runtime_device_t * p_devices, size_t count);

int runtime_get_device (const char * p_name, runtime_device_t * p_device);

typedef struct
{
    size_t registered_devices;
    size_t active_tasks; /* Includes prepared, dormant task resources. */
    size_t max_tasks;
    size_t free_task_slots;
    size_t configured_stack_bytes;
} runtime_resource_snapshot_t;

/** @brief Perform one finite execution step for a bound component. */
typedef int runtime_step_fn_t (component_t * p_component,
                               void *        p_step_context);

/* Components must be created through the compiled driver registry. The runtime
 * copies the binding, but the caller owns component/p_context storage until a
 * successful unbind or replacement. Steps must be finite and must not call
 * lifecycle APIs. Distinct instances must not share a p_context pointer. */
typedef struct
{
    component_t         component;
    runtime_step_fn_t * p_step;
    void *              p_step_context;
} runtime_binding_t;

/* Production integration supplies one registry-resolved binding per instance.
 * NULL bindings are supported only by the host admission compatibility API. */
int runtime_apply_bound_devices (const runtime_device_t *  p_devices,
                                 const runtime_binding_t * p_bindings,
                                 size_t                    count);
int runtime_bind_device (const char *              p_name,
                         const runtime_binding_t * p_binding);
int runtime_dispatch_device (const char * p_name);
int runtime_prepare_device (const char * p_name);
int runtime_reconfigure_device (const char *             p_name,
                                const runtime_device_t * p_device);
int runtime_rollback_device (const char * p_name);
int runtime_record_failure (const char * p_name);
int runtime_restart_device (const char * p_name);
int runtime_tick (uint32_t elapsed_ms);
int runtime_record_execution (const char * p_name, uint32_t now_ms);
int runtime_get_resources (runtime_resource_snapshot_t * p_snapshot);
bool runtime_is_faulted (void);

#define UTHOME_RUNTIME_ERR_BACKEND  (-4)
#define UTHOME_RUNTIME_ERR_RECOVERY (-5)
#define UTHOME_RUNTIME_ERR_BUSY     (-6)
size_t runtime_device_count (void);

#endif

/* End of file. */

