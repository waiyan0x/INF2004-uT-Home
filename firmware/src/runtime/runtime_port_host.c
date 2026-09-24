#include "runtime_port.h"

#include "uthome/runtime.h"

#include <string.h>


typedef struct {
    bool in_use;
    bool running;

    uthome_runtime_port_task_config_t config;

} host_port_task_t;


static host_port_task_t
    g_port_tasks[UTHOME_RUNTIME_MAX_DEVICE_TASKS];


static host_port_task_t *get_task(
    uthome_runtime_port_task_id_t task_id)
{
    if (task_id < 0 ||
        task_id >= UTHOME_RUNTIME_MAX_DEVICE_TASKS) {

        return NULL;
    }

    host_port_task_t *task =
        &g_port_tasks[task_id];

    if (!task->in_use) {
        return NULL;
    }

    return task;
}


int uthome_runtime_port_init(void)
{
    memset(
        g_port_tasks,
        0,
        sizeof(g_port_tasks)
    );

    return 0;
}


int uthome_runtime_port_task_prepare(
    const uthome_runtime_port_task_config_t *config,
    uthome_runtime_port_task_id_t *task_id)
{
    if (config == NULL ||
        task_id == NULL ||
        config->stack_size_bytes == 0) {

        return -1;
    }


    for (size_t i = 0;
         i < UTHOME_RUNTIME_MAX_DEVICE_TASKS;
         ++i) {

        if (!g_port_tasks[i].in_use) {

            g_port_tasks[i].in_use = true;
            g_port_tasks[i].running = false;

            g_port_tasks[i].config =
                *config;

            *task_id =
                (uthome_runtime_port_task_id_t)i;

            return 0;
        }
    }


    return -2;
}


int uthome_runtime_port_task_start(
    uthome_runtime_port_task_id_t task_id)
{
    host_port_task_t *task =
        get_task(task_id);

    if (task == NULL) {
        return -1;
    }

    task->running = true;

    return 0;
}


int uthome_runtime_port_task_stop(
    uthome_runtime_port_task_id_t task_id)
{
    host_port_task_t *task =
        get_task(task_id);

    if (task == NULL) {
        return -1;
    }

    memset(
        task,
        0,
        sizeof(*task)
    );

    return 0;
}


int uthome_runtime_port_task_reconfigure(
    uthome_runtime_port_task_id_t task_id,
    const uthome_runtime_port_task_config_t *config)
{
    host_port_task_t *task =
        get_task(task_id);

    if (task == NULL ||
        config == NULL ||
        config->stack_size_bytes == 0) {

        return -1;
    }


    task->config =
        *config;

    return 0;
}


int uthome_runtime_port_task_is_prepared(
    uthome_runtime_port_task_id_t task_id,
    bool *prepared)
{
    if (prepared == NULL) {
        return -1;
    }

    if (task_id < 0 ||
        task_id >= UTHOME_RUNTIME_MAX_DEVICE_TASKS) {

        return -1;
    }

    *prepared =
        g_port_tasks[task_id].in_use;

    return 0;
}


int uthome_runtime_port_task_is_running(
    uthome_runtime_port_task_id_t task_id,
    bool *running)
{
    if (running == NULL) {
        return -1;
    }

    host_port_task_t *task =
        get_task(task_id);

    if (task == NULL) {
        return -1;
    }

    *running =
        task->running;

    return 0;
}