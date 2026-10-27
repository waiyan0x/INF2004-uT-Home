/**
 * @file workmeter.h
 * @brief Assemble one measured worker step before publishing its counters.
 */
#ifndef UTHOME_WORKMETER_H
#define UTHOME_WORKMETER_H

#include "workcost.h"
#include <stdbool.h>

typedef struct
{
    workcost_stats_t stats;
    workcost_step_t  step;
    uint64_t         started_us;
    bool             b_active;
    bool             b_invalid;
} workmeter_t;

/* Caller owns fixed zero-initialized storage. Begin/transfer fields belong to
 * one worker. Finish updates stats only under the runtime shared guard; a
 * snapshot copies only stats under that same guard. Reset while dormant only.
 * Clock values are monotonic microseconds. Lateness is supplied separately
 * from the scheduler clock and retains that clock's millisecond resolution.
 */
int workmeter_begin (workmeter_t * p_state, uint64_t now_us);
int workmeter_bus (workmeter_t * p_state,
                   uint64_t      wait_us,
                   uint64_t      active_us,
                   uint32_t      bytes,
                   bool          b_timeout,
                   bool          b_error);
int workmeter_finish (workmeter_t * p_state,
                      uint64_t      now_us,
                      uint64_t      lateness_us,
                      bool          b_error);

#endif

/* End of file. */

