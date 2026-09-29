/**
 * @file watchdog.h
 * @brief Monitor bounded task heartbeat deadlines.
 */
#ifndef UTHOME_WATCHDOG_H
#define UTHOME_WATCHDOG_H
#include "uthome/health.h"

/*
 * Initialise bounded task-watchdog resources.
 */
int watchdog_init (void);

/*
 * Register one task for heartbeat monitoring.
 */
int watchdog_watch_task (const char * p_task_id,
                         uint32_t     timeout_ms,
                         uint32_t     now_ms);

/*
 * Record a heartbeat from a watched task.
 */
int watchdog_heartbeat (const char * p_task_id, uint32_t now_ms);

/*
 * Check all watched tasks for missed
 * heartbeat deadlines.
 */
int watchdog_check (uint32_t now_ms);

#endif

/* End of file. */

