/**
 * @file netpool.c
 * @brief Fixed leases with absolute waits and no native object creation.
 */
#include "netpool.h"

static uint32_t find_slot (netpool_t const * p_pool, void const * p_key);
static uint32_t begin_use (netpool_t * p_pool, void * p_key);
static void end_use (netpool_t * p_pool, uint32_t index);
static int
make_wait (netpool_t * p_pool, uint32_t ticks, bool b_isr, netwait_t * p_wait);
static int wait_again (netpool_t *       p_pool,
                       uint32_t          index,
                       bool              b_sender,
                       netwait_t const * p_wait);
static int mutex_change (netpool_t * p_pool,
                         uint32_t    index,
                         uint32_t    ticks,
                         bool        b_take,
                         bool        b_recursive);
static int boot_mutex (netpool_slot_t * p_slot,
                       uint32_t         owner,
                       bool             b_take,
                       bool             b_recursive);
static size_t queue_bytes (uint32_t index);
static size_t queue_offset (uint32_t index);

/** @brief Bind a complete port once; caller initially zeroes the pool. */
int netpool_init (netpool_t *           p_pool,
                  netpool_ops_t const * p_ops,
                  uint32_t              tick_hz)
{
    if ((NULL == p_pool) || (NULL == p_ops) || (p_pool->b_ready) ||
        (0U == tick_hz) || (1000000U < tick_hz) || (NULL == p_ops->p_enter) ||
        (NULL == p_ops->p_leave) || (NULL == p_ops->p_clock) ||
        (NULL == p_ops->p_can_block) || (NULL == p_ops->p_owner) ||
        (NULL == p_ops->p_wait) || (NULL == p_ops->p_signal) ||
        (NULL == p_ops->p_lock) || (NULL == p_ops->p_unlock))
    {
        return -1;
    }
    p_pool->ops     = *p_ops;
    p_pool->tick_hz = tick_hz;
    p_pool->b_ready = true;
    return 0;
}

/** @brief Lease fixed storage without changing kernel resource lifetimes. */
void * netpool_create (netpool_t *    p_pool,
                       netpool_kind_t kind,
                       uint32_t       capacity,
                       uint32_t       item_bytes,
                       uint32_t       initial_count,
                       uint8_t *      p_storage,
                       size_t         storage_bytes,
                       void *         p_static_key)
{
    void *   p_key = NULL;
    uint32_t saved = 0U;

    if ((NULL == p_pool) || (!p_pool->b_ready) ||
        ((NETPOOL_QUEUE != kind) && (NETPOOL_SEMAPHORE != kind) &&
         (NETPOOL_MUTEX != kind) && (NETPOOL_RECURSIVE != kind)) ||
        ((NULL != p_storage) && (NULL == p_static_key)))
    {
        return NULL;
    }
    if ((NETPOOL_SEMAPHORE == kind) &&
        ((0U == capacity) || (INT32_MAX < capacity) ||
         (initial_count > capacity) || (0U != item_bytes)))
    {
        return NULL;
    }
    if ((NETPOOL_QUEUE == kind) && (0U != initial_count))
    {
        return NULL;
    }
    uint32_t first = 0U;
    uint32_t limit = NETPOOL_QUEUE_SLOTS;
    if (NETPOOL_SEMAPHORE == kind)
    {
        first = NETPOOL_QUEUE_SLOTS;
        limit = first + NETPOOL_SEMAPHORE_SLOTS;
    }
    else if ((NETPOOL_MUTEX == kind) || (NETPOOL_RECURSIVE == kind))
    {
        first = NETPOOL_QUEUE_SLOTS + NETPOOL_SEMAPHORE_SLOTS;
        limit = NETPOOL_SLOTS;
    }
    else
    {
        /* Queue slots have dedicated fixed payload buffers. */
    }
    saved = p_pool->ops.p_enter(p_pool->ops.p_context);
    if (NETPOOL_SLOTS == find_slot(p_pool, p_static_key))
    {
        for (uint32_t index = first; index < limit; ++index)
        {
            netpool_slot_t * p_slot = &p_pool->slots[index];
            if (!p_slot->b_live)
            {
                int    result = 0;
                void * p_candidate_key =
                    (NULL == p_static_key) ? p_slot : p_static_key;
                if (NETPOOL_SLOTS != find_slot(p_pool, p_candidate_key))
                {
                    break;
                }
                if (NETPOOL_QUEUE == kind)
                {
                    result =
                        netring_init(&p_slot->ring,
                                     (NULL == p_storage)
                                         ? &p_pool->storage[queue_offset(index)]
                                         : p_storage,
                                     (NULL == p_storage) ? queue_bytes(index)
                                                         : storage_bytes,
                                     capacity,
                                     item_bytes);
                }
                if (0 == result)
                {
                    p_slot->p_key =
                        (NULL == p_static_key) ? p_slot : p_static_key;
                    p_slot->kind     = kind;
                    p_slot->capacity = capacity;
                    p_slot->count    = initial_count;
                    p_slot->users    = 0U;
                    p_slot->owner    = 0U;
                    p_slot->depth    = 0U;
                    p_slot->b_live   = true;
                    p_key            = p_slot->p_key;
                    break;
                }
                /* Try the next free fixed-size partition, never a heap
                 * fallback.
                 */
            }
        }
    }
    p_pool->ops.p_leave(p_pool->ops.p_context, saved);
    return p_key;
}

/** @brief Pending waiters and held mutexes prevent reuse. */
int netpool_release (netpool_t * p_pool, void * p_key)
{
    int result = -1;
    if ((NULL != p_pool) && (p_pool->b_ready))
    {
        const uint32_t saved = p_pool->ops.p_enter(p_pool->ops.p_context);
        const uint32_t index = find_slot(p_pool, p_key);
        if ((index < NETPOOL_SLOTS) && (0U == p_pool->slots[index].users) &&
            (0U == p_pool->slots[index].owner))
        {
            p_pool->slots[index].b_live = false;
            result                      = 0;
        }
        p_pool->ops.p_leave(p_pool->ops.p_context, saved);
    }
    return result;
}

/** @brief A failed/full send preserves data; waits share one deadline. */
int netpool_send (netpool_t *        p_pool,
                  void *             p_key,
                  void const *       p_item,
                  uint32_t           ticks,
                  netring_position_t position,
                  bool               b_isr)
{
    const uint32_t index  = begin_use(p_pool, p_key);
    netwait_t      wait   = {0};
    int            result = -1;

    if (NETPOOL_SLOTS <= index)
    {
        return -1;
    }
    netpool_slot_t * p_slot = &p_pool->slots[index];
    if ((NETPOOL_MUTEX == p_slot->kind) || (NETPOOL_RECURSIVE == p_slot->kind))
    {
        if (!b_isr)
        {
            result = mutex_change(p_pool, index, 0U, false, false);
        }
    }
    else if (0 == make_wait(p_pool, ticks, b_isr, &wait))
    {
        do
        {
            const uint32_t saved = p_pool->ops.p_enter(p_pool->ops.p_context);
            if (NETPOOL_QUEUE == p_slot->kind)
            {
                result = netring_send(&p_slot->ring, p_item, position);
            }
            else if ((NETPOOL_SEMAPHORE == p_slot->kind) &&
                     (p_slot->count < p_slot->capacity))
            {
                ++p_slot->count;
                result = 0;
            }
            else
            {
                /* Full semaphore or invalid kind: preserve the failure. */
            }
            if (0 == result)
            {
                p_pool->ops.p_signal(p_pool->ops.p_context, index, false);
            }
            p_pool->ops.p_leave(p_pool->ops.p_context, saved);
            if ((0 == result) || (NETPOOL_QUEUE != p_slot->kind) ||
                (NULL == p_item) || (NETRING_REPLACE == position) ||
                ((NETRING_BACK != position) && (NETRING_FRONT != position)))
            {
                break;
            }
        } while (0 == wait_again(p_pool, index, true, &wait));
    }
    else
    {
        /* A failed clock/context check cannot manufacture a successful I/O. */
    }
    end_use(p_pool, index);
    return result;
}

/** @brief Poll/receive/peek with bounded timeout and safe pending ownership. */
int netpool_receive (netpool_t * p_pool,
                     void *      p_key,
                     void *      p_item,
                     uint32_t    ticks,
                     bool        b_peek,
                     bool        b_isr)
{
    const uint32_t index  = begin_use(p_pool, p_key);
    netwait_t      wait   = {0};
    int            result = -1;

    if (NETPOOL_SLOTS <= index)
    {
        return -1;
    }
    netpool_slot_t * p_slot = &p_pool->slots[index];
    if ((NETPOOL_MUTEX == p_slot->kind) || (NETPOOL_RECURSIVE == p_slot->kind))
    {
        if ((!b_isr) && (!b_peek))
        {
            result = mutex_change(p_pool, index, ticks, true, false);
        }
    }
    else if (((!b_peek) ||
              ((NETPOOL_QUEUE == p_slot->kind) && (NULL != p_item))) &&
             (0 == make_wait(p_pool, ticks, b_isr, &wait)))
    {
        do
        {
            const uint32_t saved = p_pool->ops.p_enter(p_pool->ops.p_context);
            if (NETPOOL_QUEUE == p_slot->kind)
            {
                result = b_peek ? netring_peek(&p_slot->ring, p_item)
                                : netring_receive(&p_slot->ring, p_item);
            }
            else if ((NETPOOL_SEMAPHORE == p_slot->kind) &&
                     (0U < p_slot->count))
            {
                --p_slot->count;
                result = 0;
            }
            else
            {
                /* Empty semaphore: the remaining deadline governs retry. */
            }
            if ((0 == result) && (NETPOOL_QUEUE == p_slot->kind))
            {
                p_pool->ops.p_signal(p_pool->ops.p_context, index, !b_peek);
            }
            p_pool->ops.p_leave(p_pool->ops.p_context, saved);
            if (0 == result)
            {
                break;
            }
        } while (0 == wait_again(p_pool, index, false, &wait));
    }
    else
    {
        /* A failed clock/context check cannot manufacture a successful I/O. */
    }
    end_use(p_pool, index);
    return result;
}

/** @brief Task-only recursive/nonrecursive operations share native ownership.
 */
int netpool_mutex (netpool_t * p_pool,
                   void *      p_key,
                   uint32_t    ticks,
                   bool        b_take,
                   bool        b_recursive)
{
    const uint32_t index  = begin_use(p_pool, p_key);
    int            result = -1;

    if (index < NETPOOL_SLOTS)
    {
        result = mutex_change(p_pool, index, ticks, b_take, b_recursive);
        end_use(p_pool, index);
    }
    return result;
}

/** @brief Read counts under the same guard used by ISR operations. */
int netpool_query (netpool_t *      p_pool,
                   void *           p_key,
                   uint32_t *       p_count,
                   uint32_t *       p_space,
                   netpool_kind_t * p_kind)
{
    int result = -1;
    if ((NULL != p_pool) && (p_pool->b_ready) && (NULL != p_count) &&
        (NULL != p_space) && (NULL != p_kind))
    {
        const uint32_t saved = p_pool->ops.p_enter(p_pool->ops.p_context);
        const uint32_t index = find_slot(p_pool, p_key);
        if (index < NETPOOL_SLOTS)
        {
            netpool_slot_t const * p_slot = &p_pool->slots[index];
            *p_kind                       = p_slot->kind;
            if (NETPOOL_QUEUE == p_slot->kind)
            {
                *p_count = p_slot->ring.count;
                *p_space = p_slot->ring.capacity - p_slot->ring.count;
            }
            else if (NETPOOL_SEMAPHORE == p_slot->kind)
            {
                *p_count = p_slot->count;
                *p_space = p_slot->capacity - p_slot->count;
            }
            else
            {
                *p_count = (0U == p_slot->owner) ? 1U : 0U;
                *p_space = 0U;
            }
            result = 0;
        }
        p_pool->ops.p_leave(p_pool->ops.p_context, saved);
    }
    return result;
}

/** @brief Reset queue data while retaining pending callers and kernel IDs. */
int netpool_reset (netpool_t * p_pool, void * p_key)
{
    int result = -1;
    if ((NULL != p_pool) && (p_pool->b_ready))
    {
        const uint32_t saved = p_pool->ops.p_enter(p_pool->ops.p_context);
        const uint32_t index = find_slot(p_pool, p_key);
        if ((index < NETPOOL_SLOTS) &&
            (NETPOOL_QUEUE == p_pool->slots[index].kind))
        {
            result = netring_reset(&p_pool->slots[index].ring);
            if (0 == result)
            {
                p_pool->ops.p_signal(p_pool->ops.p_context, index, true);
            }
        }
        p_pool->ops.p_leave(p_pool->ops.p_context, saved);
    }
    return result;
}

/** @brief Match opaque keys without dereferencing caller-provided pointers. */
static uint32_t find_slot (netpool_t const * p_pool, void const * p_key)
{
    if (NULL != p_key)
    {
        for (uint32_t index = 0U; index < NETPOOL_SLOTS; ++index)
        {
            if ((p_pool->slots[index].b_live) &&
                (p_key == p_pool->slots[index].p_key))
            {
                return index;
            }
        }
    }
    return NETPOOL_SLOTS;
}

/** @brief Pin a lease across blocking work to prevent deletion/rebinding. */
static uint32_t begin_use (netpool_t * p_pool, void * p_key)
{
    uint32_t index = NETPOOL_SLOTS;
    if ((NULL != p_pool) && (p_pool->b_ready))
    {
        const uint32_t saved = p_pool->ops.p_enter(p_pool->ops.p_context);
        index                = find_slot(p_pool, p_key);
        if ((index < NETPOOL_SLOTS) &&
            (UINT32_MAX != p_pool->slots[index].users))
        {
            ++p_pool->slots[index].users;
        }
        else
        {
            index = NETPOOL_SLOTS;
        }
        p_pool->ops.p_leave(p_pool->ops.p_context, saved);
    }
    return index;
}

/** @brief Drop the operation pin only after all its signals complete. */
static void end_use (netpool_t * p_pool, uint32_t index)
{
    const uint32_t saved = p_pool->ops.p_enter(p_pool->ops.p_context);
    --p_pool->slots[index].users;
    p_pool->ops.p_leave(p_pool->ops.p_context, saved);
}

/** @brief Capture context before interrupt exclusion; forbidden waits poll. */
static int
make_wait (netpool_t * p_pool, uint32_t ticks, bool b_isr, netwait_t * p_wait)
{
    uint64_t now_us = 0U;
    if ((0U == ticks) || (b_isr) ||
        (!p_pool->ops.p_can_block(p_pool->ops.p_context)))
    {
        *p_wait = (netwait_t){0};
        return 0;
    }
    if (0 != p_pool->ops.p_clock(p_pool->ops.p_context, &now_us))
    {
        return -1;
    }
    return netwait_init(p_wait, now_us, ticks, p_pool->tick_hz);
}

/** @brief Recompute remaining time after every stale or real notification. */
static int wait_again (netpool_t *       p_pool,
                       uint32_t          index,
                       bool              b_sender,
                       netwait_t const * p_wait)
{
    uint64_t now_us       = 0U;
    uint32_t milliseconds = 0U;
    if (((!p_wait->b_infinite) && (0U == p_wait->deadline_us)) ||
        (0 != p_pool->ops.p_clock(p_pool->ops.p_context, &now_us)) ||
        (0 != netwait_remaining(p_wait, now_us, &milliseconds)) ||
        (0U == milliseconds) ||
        (!p_pool->ops.p_can_block(p_pool->ops.p_context)))
    {
        return -1;
    }
    const int result = p_pool->ops.p_wait(
        p_pool->ops.p_context, index, b_sender, milliseconds);
    return (0 <= result) ? 0 : -1;
}

/** @brief Native mutex ownership supplies priority inheritance; depth is fixed.
 */
static int mutex_change (netpool_t * p_pool,
                         uint32_t    index,
                         uint32_t    ticks,
                         bool        b_take,
                         bool        b_recursive)
{
    netpool_slot_t * p_slot       = &p_pool->slots[index];
    netwait_t        wait         = {0};
    uint64_t         now_us       = 0U;
    uint32_t         milliseconds = 0U;
    uint32_t         saved        = 0U;
    uint32_t         owner        = 0U;
    int              result       = -1;

    if (p_slot->kind != (b_recursive ? NETPOOL_RECURSIVE : NETPOOL_MUTEX))
    {
        return -1;
    }
    owner = p_pool->ops.p_owner(p_pool->ops.p_context);
    if (0U == owner)
    {
        return -1;
    }
    saved = p_pool->ops.p_enter(p_pool->ops.p_context);
    if ((NETPOOL_BOOT_OWNER == owner) || (NETPOOL_BOOT_OWNER == p_slot->owner))
    {
        result = boot_mutex(p_slot, owner, b_take, b_recursive);
        p_pool->ops.p_leave(p_pool->ops.p_context, saved);
        return result;
    }
    p_pool->ops.p_leave(p_pool->ops.p_context, saved);
    if (!p_pool->ops.p_can_block(p_pool->ops.p_context))
    {
        return -1;
    }
    saved = p_pool->ops.p_enter(p_pool->ops.p_context);
    if (owner == p_slot->owner)
    {
        if ((b_take) && (b_recursive) && (UINT32_MAX != p_slot->depth))
        {
            ++p_slot->depth;
            result = 0;
        }
        else if (!b_take)
        {
            if (1U < p_slot->depth)
            {
                --p_slot->depth;
                result = 0;
            }
            else if (0 == p_pool->ops.p_unlock(p_pool->ops.p_context, index))
            {
                p_slot->owner = 0U;
                p_slot->depth = 0U;
                result        = 0;
            }
            else
            {
                /* Failed native unlock retains logical ownership. */
            }
        }
        else
        {
            /* Reject a nonrecursive re-take or a saturated recursion depth. */
        }
        p_pool->ops.p_leave(p_pool->ops.p_context, saved);
        return result;
    }
    p_pool->ops.p_leave(p_pool->ops.p_context, saved);
    if ((b_take) && (0 == make_wait(p_pool, ticks, false, &wait)))
    {
        int locked = -1;
        do
        {
            if ((0 != p_pool->ops.p_clock(p_pool->ops.p_context, &now_us)) ||
                (0 != netwait_remaining(&wait, now_us, &milliseconds)))
            {
                break;
            }
            locked =
                p_pool->ops.p_lock(p_pool->ops.p_context, index, milliseconds);
            if (0 == locked)
            {
                saved         = p_pool->ops.p_enter(p_pool->ops.p_context);
                p_slot->owner = owner;
                p_slot->depth = 1U;
                p_pool->ops.p_leave(p_pool->ops.p_context, saved);
                result = 0;
            }
        } while ((1 == locked) && (0U < milliseconds));
    }
    return result;
}

/** @brief Serialized startup owns logical locks without a native task ID. */
static int boot_mutex (netpool_slot_t * p_slot,
                       uint32_t         owner,
                       bool             b_take,
                       bool             b_recursive)
{
    if ((NETPOOL_BOOT_OWNER != owner) ||
        ((0U != p_slot->owner) && (NETPOOL_BOOT_OWNER != p_slot->owner)))
    {
        return -1;
    }
    if (b_take)
    {
        if ((0U == p_slot->owner) ||
            ((b_recursive) && (UINT32_MAX != p_slot->depth)))
        {
            p_slot->owner = NETPOOL_BOOT_OWNER;
            ++p_slot->depth;
            return 0;
        }
    }
    else if ((NETPOOL_BOOT_OWNER == p_slot->owner) && (0U < p_slot->depth))
    {
        --p_slot->depth;
        if (0U == p_slot->depth)
        {
            p_slot->owner = 0U;
        }
        return 0;
    }
    else
    {
        /* Unowned or mismatched release preserves the lock state. */
    }
    return -1;
}

/** @brief Eight partitions share the unchanged 8 KiB payload reservation. */
static size_t queue_bytes (uint32_t index)
{
    return (index < 2U) ? 512U : ((index < 7U) ? 1024U : 2048U);
}

/** @brief Private callers provide queue indices 0..7 under the pool guard. */
static size_t queue_offset (uint32_t index)
{
    return (index < 2U) ? ((size_t)index * 512U)
                        : (1024U + ((size_t)(index - 2U) * 1024U));
}

/* End of file. */

