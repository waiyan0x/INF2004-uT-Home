/**
 * @file evfake.h
 * @brief Controlled owned-native boundary for the real event adapter.
 */
#ifndef UTHOME_EVFAKE_H
#define UTHOME_EVFAKE_H

#include "c3ewait.h"

typedef struct
{
    int      create_fail_at;
    int      signal_result;
    int      wait_result;
    uint32_t clock_fail_at;
    uint32_t clock_reverse_at;
    uint32_t stale_wakes;
    bool     b_cache;
    bool     b_context;
    bool     b_publish;
    bool     b_delete;
} evfake_config_t;

typedef struct
{
    uint32_t creates;
    uint32_t signals;
    uint32_t waits;
    uint32_t enters;
    uint32_t leaves;
    uint32_t depth;
    uint32_t violations;
    uint32_t clocks;
    uint32_t budgets[32];
} evfake_metrics_t;

void evfake_configure (const evfake_config_t * p_config, void * p_handle);
const evfake_metrics_t * evfake_metrics (void);
int evfake_create (void);
int evfake_signal (int h_notification);
int evfake_wait (int h_notification, uint32_t milliseconds);
uint32_t evfake_enter (void);
void evfake_leave (uint32_t saved);
bool evfake_available (void);
int evfake_context (bool b_blocking);
int64_t evfake_now (void);
uint32_t evfake_tick_hz (void);

#endif

/* End of file. */

