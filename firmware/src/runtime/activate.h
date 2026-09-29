/**
 * @file activate.h
 * @brief Apply resolved instance plans using checked target worker ownership.
 */
#ifndef UTHOME_ACTIVATE_H
#define UTHOME_ACTIVATE_H

#include "workport.h"
#include "uthome/component.h"
#include "uthome/msgbus.h"
#include "uthome/runtime.h"

#define UTHOME_ACTIVATE_CONFIG_MAX 128U
#define UTHOME_ACTIVATE_REJECTED   (-10)
#define UTHOME_ACTIVATE_RECOVERY   (-11)

typedef struct
{
    component_t           component;
    rt_port_worker_t      worker;
    rt_port_task_config_t task;
    uint32_t              heartbeat_ms;
    size_t                config_size;
    uint8_t               config[UTHOME_ACTIVATE_CONFIG_MAX];
    uint8_t               consumer_mask;
    bool                  b_enabled;
} activate_device_t;

/* A trusted application adapter resolves descriptors, wiring and drivers
 * before constructing this plan. This is not a JSON/parser or wire ABI.
 * Configuration bytes must contain values, not handles or resource pointers.
 * Configure callbacks copy bytes into aligned driver-owned types before use.
 * Plan storage is copied; callbacks and distinct contexts remain borrowed.
 */

/* Optional plan-level resource owner. Configure runs before any worker is
 * created; release runs only after every worker/device cleanup succeeds.
 * Settings are copied values. The borrowed context remains alive on failure.
 * A zero-initialized resource is absent and preserves existing plan behavior.
 */
typedef struct
{
    component_t            component;
    rt_port_cleanup_fn_t * p_release;
    size_t                 config_size;
    uint8_t                config[UTHOME_ACTIVATE_CONFIG_MAX];
} activate_resource_t;

typedef struct
{
    activate_resource_t   resources;
    size_t                device_count;
    activate_device_t     devices[UTHOME_RUNTIME_MAX_DEVICES];
    size_t                queue_count;
    msgbus_queue_config_t queues[UTHOME_MSGBUS_MAX_QUEUES];
} activate_plan_t;

typedef enum
{
    ACTIVATE_EMPTY,
    ACTIVATE_ACTIVE,
    ACTIVATE_NEEDS_RECOVERY
} activate_state_t;

typedef struct
{
    activate_state_t state;
    uint32_t         generation;
    size_t           retained_instances;
    size_t           failed_workers;
    int              apply_error;
    int              recovery_error;
    int              worker_error;
    bool             b_resources_retained;
} activate_status_t;

/* Single lifecycle owner, established by init; other tasks/ISRs and callback
 * re-entry are rejected. Init never resets a previously initialized manager.
 * apply validates before stopping anything. Zero means the entire new plan
 * was released; REJECTED means rejection or the previous plan was restored;
 * RECOVERY means contexts are retained and only recover may change state.
 * No callback may use raw port lifecycle APIs. Disabled entries own no worker.
 * The initial target supports positive periods, not stream/readiness tasks.
 */
int activate_init (void);
int activate_apply (const activate_plan_t * p_plan);
int activate_recover (void);
/* Explicit safe-empty recovery: no prior plan is restarted. A release failure
 * retains ownership and NEEDS_RECOVERY; retry clear. Only success discards
 * plans.
 */
int activate_clear (void);
int activate_get_status (activate_status_t * p_status);

/* Worker callbacks use shared.h for queues/health, never the unlocked modules.
 * A zero step result records progress; a nonzero result leaves a failed owned
 * worker visible in get_status. ACTIVE describes accepted configuration, not
 * health. Recovery recreates settings, not counters or old samples. Contexts
 * of the previous and candidate plans must survive until apply/recover succeeds
 * or get_status confirms EMPTY with zero retained instances. */

#endif

/* End of file. */

