/**
 * @file watchdog.c
 * @brief Track task heartbeats and report each missed-heartbeat episode once.
 */
#include "uthome/watchdog.h"

#include <stdbool.h>
#include <string.h>

#define UTHOME_HEALTH_MAX_WATCHED_TASKS 8U
#define UTHOME_HEALTH_TASK_ID_MAX       32U

typedef struct
{
    bool b_in_use;

    char task_name[UTHOME_HEALTH_TASK_ID_MAX];

    uint32_t timeout_ms;
    uint32_t last_heartbeat_ms;

    bool b_missed_reported;
} health_watchdog_task_t;

static health_watchdog_task_t
    g_watchdog_tasks[UTHOME_HEALTH_MAX_WATCHED_TASKS] = {0};

static int find_watchdog_task (const char * p_task_name);

/**
 * @brief Clear all heartbeat registrations and previous missed-event state.
 * @return Health success, rate-limit indication, or validation error as
 * applicable.
 */
int watchdog_init (void)
{
    memset(g_watchdog_tasks, 0, sizeof(g_watchdog_tasks));

    return UTHOME_HEALTH_OK;
}

/**
 * @brief Register one uniquely named heartbeat source and its timeout.
 * @param p_task_name Null-terminated name of the heartbeat source.
 * @param timeout_ms Maximum permitted heartbeat interval in milliseconds.
 * @param now_ms Monotonic time in milliseconds with unsigned wraparound
 * semantics.
 * @return Health success, rate-limit indication, or validation error as
 * applicable.
 */
int watchdog_watch_task (const char * p_task_name,
                         uint32_t     timeout_ms,
                         uint32_t     now_ms)
{
    if ((NULL == p_task_name) || ('\0' == p_task_name[0]) ||
        (strlen(p_task_name) >= UTHOME_HEALTH_TASK_ID_MAX) ||
        (0U == timeout_ms))
    {
        return UTHOME_HEALTH_ERR_INVALID;
    }

    if (find_watchdog_task(p_task_name) >= 0)
    {
        return UTHOME_HEALTH_ERR_INVALID;
    }

    for (size_t index = 0U; index < UTHOME_HEALTH_MAX_WATCHED_TASKS; ++index)
    {
        if (!g_watchdog_tasks[index].b_in_use)
        {
            health_watchdog_task_t * p_task = &g_watchdog_tasks[index];

            memset(p_task, 0, sizeof(*p_task));

            p_task->b_in_use = true;

            strncpy(
                p_task->task_name, p_task_name, sizeof(p_task->task_name) - 1U);

            p_task->task_name[sizeof(p_task->task_name) - 1U] = '\0';

            p_task->timeout_ms        = timeout_ms;
            p_task->last_heartbeat_ms = now_ms;
            p_task->b_missed_reported = false;

            return UTHOME_HEALTH_OK;
        }
    }

    return UTHOME_HEALTH_ERR_INVALID;
}

/**
 * @brief Record progress and allow a later missed-heartbeat report.
 * @param p_task_name Null-terminated name of the heartbeat source.
 * @param now_ms Monotonic time in milliseconds with unsigned wraparound
 * semantics.
 * @return Health success, rate-limit indication, or validation error as
 * applicable.
 */
int watchdog_heartbeat (const char * p_task_name, uint32_t now_ms)
{
    const int index = find_watchdog_task(p_task_name);

    if (index < 0)
    {
        return UTHOME_HEALTH_ERR_INVALID;
    }

    g_watchdog_tasks[index].last_heartbeat_ms = now_ms;

    /*
     * A fresh heartbeat means the task has
     * recovered, so another future miss may
     * be reported.
     */
    g_watchdog_tasks[index].b_missed_reported = false;

    return UTHOME_HEALTH_OK;
}

/**
 * @brief Report each overdue source once until its next heartbeat.
 * @param now_ms Monotonic time in milliseconds with unsigned wraparound
 * semantics.
 * @return Health success, rate-limit indication, or validation error as
 * applicable.
 */
int watchdog_check (uint32_t now_ms)
{
    int result = UTHOME_HEALTH_OK;

    for (size_t index = 0U; index < UTHOME_HEALTH_MAX_WATCHED_TASKS; ++index)
    {
        health_watchdog_task_t * p_task = &g_watchdog_tasks[index];

        if (!p_task->b_in_use)
        {
            continue;
        }

        const uint32_t elapsed = now_ms - p_task->last_heartbeat_ms;

        if (elapsed <= p_task->timeout_ms)
        {
            continue;
        }

        /*
         * Only report one miss until the task
         * sends another heartbeat.
         */
        if (p_task->b_missed_reported)
        {
            continue;
        }

        int health_result =
            health_report(UTHOME_HEALTH_TASK_HEARTBEAT_MISSED, now_ms);

        p_task->b_missed_reported = true;

        if (UTHOME_HEALTH_OK != health_result)
        {
            result = health_result;
        }
    }

    return result;
}

/**
 * @brief Look up a registered heartbeat source by its task name.
 * @param p_task_name Null-terminated name of the heartbeat source.
 * @return Registered slot index, or -1 when no task matches.
 */
static int find_watchdog_task (const char * p_task_name)
{
    if (NULL == p_task_name)
    {
        return -1;
    }

    for (size_t index = 0U; index < UTHOME_HEALTH_MAX_WATCHED_TASKS; ++index)
    {
        if ((g_watchdog_tasks[index].b_in_use) &&
            (0 == strcmp(g_watchdog_tasks[index].task_name, p_task_name)))
        {
            /* The table has eight entries, so its index fits in int. */
            return (int)index;
        }
    }

    return -1;
}

/* End of file. */

