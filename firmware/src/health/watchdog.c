#include "uthome/health.h"

#include <stdbool.h>
#include <string.h>


#define UTHOME_HEALTH_MAX_WATCHED_TASKS 8
#define UTHOME_HEALTH_TASK_ID_MAX       32


typedef struct {
    bool in_use;

    char task_id[UTHOME_HEALTH_TASK_ID_MAX];

    uint32_t timeout_ms;
    uint32_t last_heartbeat_ms;

    bool missed_reported;
} health_watchdog_task_t;


static health_watchdog_task_t
    g_watchdog_tasks[UTHOME_HEALTH_MAX_WATCHED_TASKS];


static int find_watchdog_task(const char *task_id)
{
    if (task_id == NULL) {
        return -1;
    }

    for (size_t i = 0;
         i < UTHOME_HEALTH_MAX_WATCHED_TASKS;
         ++i) {

        if (g_watchdog_tasks[i].in_use &&
            strcmp(
                g_watchdog_tasks[i].task_id,
                task_id
            ) == 0) {

            return (int)i;
        }
    }

    return -1;
}


int uthome_health_watchdog_init(void)
{
    memset(
        g_watchdog_tasks,
        0,
        sizeof(g_watchdog_tasks)
    );

    return UTHOME_HEALTH_OK;
}


int uthome_health_watch_task(
    const char *task_id,
    uint32_t timeout_ms,
    uint32_t now_ms)
{
    if (task_id == NULL ||
        task_id[0] == '\0' ||
        timeout_ms == 0) {

        return UTHOME_HEALTH_ERR_INVALID;
    }

    if (find_watchdog_task(task_id) >= 0) {
        return UTHOME_HEALTH_ERR_INVALID;
    }

    for (size_t i = 0;
         i < UTHOME_HEALTH_MAX_WATCHED_TASKS;
         ++i) {

        if (!g_watchdog_tasks[i].in_use) {

            health_watchdog_task_t *task =
                &g_watchdog_tasks[i];

            memset(
                task,
                0,
                sizeof(*task)
            );

            task->in_use = true;

            strncpy(
                task->task_id,
                task_id,
                sizeof(task->task_id) - 1
            );

            task->task_id[
                sizeof(task->task_id) - 1
            ] = '\0';

            task->timeout_ms = timeout_ms;
            task->last_heartbeat_ms = now_ms;
            task->missed_reported = false;

            return UTHOME_HEALTH_OK;
        }
    }

    return UTHOME_HEALTH_ERR_INVALID;
}


int uthome_health_task_heartbeat(
    const char *task_id,
    uint32_t now_ms)
{
    int index =
        find_watchdog_task(task_id);

    if (index < 0) {
        return UTHOME_HEALTH_ERR_INVALID;
    }

    g_watchdog_tasks[index].last_heartbeat_ms =
        now_ms;

    /*
     * A fresh heartbeat means the task has
     * recovered, so another future miss may
     * be reported.
     */
    g_watchdog_tasks[index].missed_reported =
        false;

    return UTHOME_HEALTH_OK;
}


int uthome_health_check_heartbeats(
    uint32_t now_ms)
{
    int result = UTHOME_HEALTH_OK;

    for (size_t i = 0;
         i < UTHOME_HEALTH_MAX_WATCHED_TASKS;
         ++i) {

        health_watchdog_task_t *task =
            &g_watchdog_tasks[i];

        if (!task->in_use) {
            continue;
        }

        uint32_t elapsed =
            now_ms -
            task->last_heartbeat_ms;

        if (elapsed <= task->timeout_ms) {
            continue;
        }

        /*
         * Only report one miss until the task
         * sends another heartbeat.
         */
        if (task->missed_reported) {
            continue;
        }

        int health_result =
            uthome_health_report(
                UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
                now_ms
            );

        task->missed_reported = true;

        if (health_result != UTHOME_HEALTH_OK) {
            result = health_result;
        }
    }

    return result;
}