/**
 * @file netwait.h
 * @brief Absolute deadlines for SDK tick waits over a millisecond kernel.
 */
#ifndef UTHOME_NETWAIT_H
#define UTHOME_NETWAIT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint64_t started_us;
    uint64_t deadline_us;
    bool     b_infinite;
} netwait_t;

/** @brief Convert once, rounding a positive fractional microsecond upwards. */
int netwait_init (netwait_t * p_wait,
                  uint64_t    now_us,
                  uint32_t    ticks,
                  uint32_t    tick_hz);

/**
 * @brief Return remaining milliseconds; UINT32_MAX denotes an infinite wait.
 *
 * Finite waits are capped at INT32_MAX milliseconds for a signed native TMO.
 * Call again after any wakeup, including a stale notification. A backwards
 * clock fails rather than extending the deadline. Zero means poll/expired.
 */
int netwait_remaining (netwait_t const * p_wait,
                       uint64_t          now_us,
                       uint32_t *        p_milliseconds);

#endif

/* End of file. */

