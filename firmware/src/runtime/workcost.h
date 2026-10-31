/**
 * @file workcost.h
 * @brief Fixed per-worker workload measurements, independent of device type.
 */
#ifndef UTHOME_WORKCOST_H
#define UTHOME_WORKCOST_H

#include <stdint.h>

#define WORKCOST_SATURATED (1U)

typedef struct
{
    uint32_t elapsed_us;
    uint32_t lateness_us;
    uint32_t bus_wait_us;
    uint32_t bus_active_us;
    uint32_t transfer_bytes;
    uint32_t transfers;
    uint32_t timeouts;
    uint32_t errors;
} workcost_step_t;

typedef struct
{
    uint32_t steps;
    uint32_t elapsed_us;
    uint32_t max_elapsed_us;
    uint32_t max_lateness_us;
    uint32_t bus_wait_us;
    uint32_t bus_active_us;
    uint32_t transfer_bytes;
    uint32_t transfers;
    uint32_t timeouts;
    uint32_t errors;
    uint32_t flags;
} workcost_stats_t;

/**
 * @brief Add one completed step; saturate sums and record saturation
 * explicitly.
 * @return Zero on success, -1 for null or inconsistent timing/error counts.
 * @note Caller owns zero-initialized fixed storage, one record per bound
 * worker, and serializes updates/snapshots using the existing runtime shared
 * guard. No allocation, kernel object, clock read, driver I/O or hidden global
 * state. Elapsed is wall time, including waits/preemption; it is not CPU busy
 * time. Bus active time is time in transfer calls, not measured electrical wire
 * time. Lateness is max(0, completion minus scheduled completion), in
 * microseconds. A caller without these measurements must report unavailable,
 * not pass zeros.
 */
int workcost_add (workcost_stats_t * p_stats, const workcost_step_t * p_step);

#endif

/* End of file. */

