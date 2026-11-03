/**
 * @file netretry.h
 * @brief Caller-driven connection deadlines and capped retry scheduling.
 */
#ifndef UTHOME_NETRETRY_H
#define UTHOME_NETRETRY_H
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    NETRETRY_WAITING = 0,
    NETRETRY_ATTEMPT,
    NETRETRY_ONLINE
} netretry_state_t;

typedef struct
{
    uint64_t         last_ms;
    uint64_t         deadline_ms;
    uint64_t         online_ms;
    uint32_t         delay_ms;
    netretry_state_t state;
} netretry_t;

/* One owner; no SDK calls, waiting or allocation. Initialize before use.
 * All times share one monotonic millisecond clock. Regression/overflow fails
 * without changing state. The caller bounds each actual transport operation. */
int netretry_init (netretry_t * p_retry, uint64_t now_ms);
/* Return 1 exactly when a new 30-second attempt is admitted, 0 otherwise.
 * Upstream loss cancels an attempt/online state. Poll never spins or calls I/O.
 * Supply jitter 0..250 ms from a platform source; it is capped with total
 * delay.
 */
int netretry_poll (netretry_t * p_retry,
                   uint64_t     now_ms,
                   bool         b_upstream,
                   uint32_t     jitter_ms);
/* An attempt succeeds only after IP/broker readiness, not socket creation.
 * Failure schedules 1,2,4,8,16,30-second delays. Online failure resets backoff
 * only after 60 seconds of stable operation. Late success is rejected.
 * Upstream loss must first be polled before delivering stale completion events.
 */
int netretry_result (netretry_t * p_retry,
                     uint64_t     now_ms,
                     bool         b_success,
                     uint32_t     jitter_ms);
#endif

/* End of file. */

