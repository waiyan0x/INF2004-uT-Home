/**
 * @file netevent.h
 * @brief Fixed event groups with broadcast snapshots and bounded wait records.
 */
#ifndef UTHOME_NETEVENT_H
#define UTHOME_NETEVENT_H

#include <stdbool.h>
#include <stdint.h>

#define NETEVENT_GROUPS  8U
#define NETEVENT_WAITERS 16U
#define NETEVENT_BITS    UINT32_C(0x00ffffff)

typedef struct
{
    void *   p_key;
    uint32_t bits;
    uint32_t users;
    bool     b_live;
} netevent_group_t;

typedef struct
{
    uint32_t group;
    uint32_t wanted;
    uint32_t result;
    bool     b_all;
    bool     b_clear;
    bool     b_used;
    bool     b_ready;
} netevent_waiter_t;

typedef struct
{
    netevent_group_t  groups[NETEVENT_GROUPS];
    netevent_waiter_t waiters[NETEVENT_WAITERS];
} netevent_t;

/**
 * @brief Lease zero-initialized fixed storage; a static key is never
 * dereferenced.
 *
 * The caller serializes every operation, including begin/finish, under one
 * critical section. Waiting and notification delivery are outside this core.
 * A returned wake mask names permanent notification slots, not task identities.
 * Notifications are hints; finish always checks authoritative stored results.
 * Keys and handles must not be used after release. Retired groups cannot be
 * reused until all registered waiters finish, including deletion wakeups.
 */
void * netevent_create (netevent_t * p_pool, void * p_key);
int netevent_release (netevent_t * p_pool, void * p_key, uint32_t * p_wake);
int netevent_set (netevent_t * p_pool,
                  void *       p_key,
                  uint32_t     bits,
                  uint32_t *   p_result,
                  uint32_t *   p_wake);
int netevent_clear (netevent_t * p_pool,
                    void *       p_key,
                    uint32_t     bits,
                    uint32_t *   p_result);
int netevent_get (netevent_t * p_pool, void * p_key, uint32_t * p_result);

/**
 * @brief Atomically set barrier bits and inspect/register a wait.
 *
 * Returns one for an immediate result, zero for a registered wait and negative
 * for invalid input/exhaustion. A zero-tick caller sets b_block false.
 * sync_bits is zero for ordinary waiting; barriers use clear/all with nonzero
 * sync_bits. Every waiter satisfied by one set observes the same pre-clear
 * snapshot.
 */
int netevent_begin (netevent_t * p_pool,
                    void *       p_key,
                    uint32_t     wanted,
                    bool         b_clear,
                    bool         b_all,
                    uint32_t     sync_bits,
                    bool         b_block,
                    uint32_t *   p_token,
                    uint32_t *   p_result,
                    uint32_t *   p_wake);

/**
 * @brief Consume a completed wait, or end it at an absolute deadline/error.
 *
 * Returns zero while pending, one on completion and negative for an invalid
 * token. Timeout returns the current bits; deletion returns zero. Completion
 * releases the wait record exactly once. Callers stop using that token then.
 */
int netevent_finish (netevent_t * p_pool,
                     uint32_t     token,
                     bool         b_timeout,
                     uint32_t *   p_result);

#endif

/* End of file. */

