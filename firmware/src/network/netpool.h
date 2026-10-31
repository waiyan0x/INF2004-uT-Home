/**
 * @file netpool.h
 * @brief Fixed network queue/semaphore/mutex leases over permanent objects.
 */
#ifndef UTHOME_NETPOOL_H
#define UTHOME_NETPOOL_H

#include "netring.h"
#include "netwait.h"

#define NETPOOL_QUEUE_SLOTS     8U
#define NETPOOL_SEMAPHORE_SLOTS 16U
#define NETPOOL_MUTEX_SLOTS     32U
#define NETPOOL_SLOTS                                                          \
    (NETPOOL_QUEUE_SLOTS + NETPOOL_SEMAPHORE_SLOTS + NETPOOL_MUTEX_SLOTS)
#define NETPOOL_STORAGE_BYTES 8192U
#define NETPOOL_BOOT_OWNER    UINT32_MAX

typedef enum
{
    NETPOOL_QUEUE = 0,
    NETPOOL_SEMAPHORE,
    NETPOOL_MUTEX,
    NETPOOL_RECURSIVE
} netpool_kind_t;

typedef uint32_t netpool_enter_fn_t (void * p_context);
typedef void netpool_leave_fn_t (void * p_context, uint32_t saved);
typedef int netpool_clock_fn_t (void * p_context, uint64_t * p_now_us);
typedef bool netpool_block_fn_t (void * p_context);
typedef uint32_t netpool_owner_fn_t (void * p_context);
typedef int netpool_wait_fn_t (void *   p_context,
                               uint32_t slot,
                               bool     b_sender,
                               uint32_t milliseconds);
typedef void
netpool_signal_fn_t (void * p_context, uint32_t slot, bool b_sender);
typedef int
netpool_lock_fn_t (void * p_context, uint32_t slot, uint32_t milliseconds);
typedef int netpool_unlock_fn_t (void * p_context, uint32_t slot);

typedef struct
{
    void *                p_context;
    netpool_enter_fn_t *  p_enter;
    netpool_leave_fn_t *  p_leave;
    netpool_clock_fn_t *  p_clock;
    netpool_block_fn_t *  p_can_block;
    netpool_owner_fn_t *  p_owner;
    netpool_wait_fn_t *   p_wait;
    netpool_signal_fn_t * p_signal;
    netpool_lock_fn_t *   p_lock;
    netpool_unlock_fn_t * p_unlock;
} netpool_ops_t;

typedef struct
{
    netring_t      ring;
    void *         p_key;
    netpool_kind_t kind;
    uint32_t       count;
    uint32_t       capacity;
    uint32_t       users;
    uint32_t       owner;
    uint32_t       depth;
    bool           b_live;
} netpool_slot_t;

typedef struct
{
    netpool_ops_t  ops;
    netpool_slot_t slots[NETPOOL_SLOTS];
    uint8_t        storage[NETPOOL_STORAGE_BYTES];
    uint32_t       tick_hz;
    bool           b_ready;
} netpool_t;

/**
 * @brief Initialize zeroed storage once after native objects exist.
 *
 * Port callbacks and context live as long as the pool. Every slot must already
 * have its kind-specific startup resources: two notifications for each queue,
 * one for each semaphore, or one priority-inheriting mutex. Waits
 * occur outside the critical section. Signal and unlock must be safe inside
 * it and must defer dispatch until the outer section ends. Callbacks never
 * create/delete native objects or allocate memory. Wait/lock return zero for
 * success, one for a native timeout, and negative for an unrecoverable error.
 * Owner zero rejects mutex operations. NETPOOL_BOOT_OWNER denotes exclusively
 * serialized pre-scheduler startup: its locks never call the native port or
 * clock. The port must never return that owner after task execution begins.
 * Every startup lock must be released before tasks start; retained locks fail
 * closed rather than transferring ownership to an unrelated native mutex.
 */
int netpool_init (netpool_t *           p_pool,
                  netpool_ops_t const * p_ops,
                  uint32_t              tick_hz);

/**
 * @brief Lease a slot; queue bytes use the fixed slot or caller storage.
 *
 * A non-null static key is an opaque identity, never dereferenced. For an
 * external queue, storage_bytes is its actual capacity and the key is required.
 * Caller storage and keys remain alive until a successful release. Callers
 * must stop using a handle after release; concurrent release fails while an
 * operation is pending. Queue, semaphore and mutex leases have separate
 * declared fixed limits.
 */
void * netpool_create (netpool_t *    p_pool,
                       netpool_kind_t kind,
                       uint32_t       capacity,
                       uint32_t       item_bytes,
                       uint32_t       initial_count,
                       uint8_t *      p_storage,
                       size_t         storage_bytes,
                       void *         p_static_key);

/** @brief Release only an unowned lease with no pending operations. */
int netpool_release (netpool_t * p_pool, void * p_key);

/** @brief Queue send or semaphore give; ISR requests never wait. */
int netpool_send (netpool_t *        p_pool,
                  void *             p_key,
                  void const *       p_item,
                  uint32_t           ticks,
                  netring_position_t position,
                  bool               b_isr);

/** @brief Queue receive/peek or semaphore take; ISR requests never wait. */
int netpool_receive (netpool_t * p_pool,
                     void *      p_key,
                     void *      p_item,
                     uint32_t    ticks,
                     bool        b_peek,
                     bool        b_isr);

/** @brief Take/give a mutex from task context with explicit recursion mode. */
int netpool_mutex (netpool_t * p_pool,
                   void *      p_key,
                   uint32_t    ticks,
                   bool        b_take,
                   bool        b_recursive);

/** @brief Coherent count/space/kind observation; invalid handles fail. */
int netpool_query (netpool_t *      p_pool,
                   void *           p_key,
                   uint32_t *       p_count,
                   uint32_t *       p_space,
                   netpool_kind_t * p_kind);

/** @brief Empty a queue; preserve resources and notify blocked senders. */
int netpool_reset (netpool_t * p_pool, void * p_key);

#endif

/* End of file. */

