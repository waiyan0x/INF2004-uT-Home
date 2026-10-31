/**
 * @file netevent.c
 * @brief Heap-free event state with atomic broadcast and barrier semantics.
 */
#include "netevent.h"

#include <stddef.h>

static uint32_t find_group (netevent_t const * p_pool, void const * p_key);
static bool matches (uint32_t bits, uint32_t wanted, bool b_all);
static uint32_t publish (netevent_t * p_pool, uint32_t group, uint32_t bits);
static bool key_taken (netevent_t const * p_pool, void const * p_key);

/** @brief Reject duplicate live/retired keys and retain caller-owned storage.
 */
void * netevent_create (netevent_t * p_pool, void * p_key)
{
    uint32_t free_slot = NETEVENT_GROUPS;
    if (NULL == p_pool)
    {
        return NULL;
    }
    for (uint32_t index = 0U; index < NETEVENT_GROUPS; ++index)
    {
        netevent_group_t * p_group = &p_pool->groups[index];
        if ((NULL != p_key) && (p_group->p_key == p_key))
        {
            return NULL;
        }
        if ((!p_group->b_live) && (0U == p_group->users) &&
            ((NULL != p_key) || (!key_taken(p_pool, p_group))) &&
            (NETEVENT_GROUPS == free_slot))
        {
            free_slot = index;
        }
    }
    if (NETEVENT_GROUPS != free_slot)
    {
        netevent_group_t * p_group = &p_pool->groups[free_slot];
        p_group->p_key             = (NULL == p_key) ? p_group : p_key;
        p_group->bits              = 0U;
        p_group->b_live            = true;
        return p_group->p_key;
    }
    return NULL;
}

/** @brief Deletion completes outstanding waits without recycling their group.
 */
int netevent_release (netevent_t * p_pool, void * p_key, uint32_t * p_wake)
{
    const uint32_t group = find_group(p_pool, p_key);
    if ((NETEVENT_GROUPS == group) || (NULL == p_wake))
    {
        return -1;
    }
    *p_wake                      = 0U;
    p_pool->groups[group].b_live = false;
    p_pool->groups[group].bits   = 0U;
    for (uint32_t index = 0U; index < NETEVENT_WAITERS; ++index)
    {
        netevent_waiter_t * p_wait = &p_pool->waiters[index];
        if ((p_wait->b_used) && (p_wait->group == group) && (!p_wait->b_ready))
        {
            p_wait->result  = 0U;
            p_wait->b_ready = true;
            *p_wake |= UINT32_C(1) << index;
        }
    }
    if (0U == p_pool->groups[group].users)
    {
        p_pool->groups[group].p_key = NULL;
    }
    return 0;
}

/** @brief Evaluate all waiters before applying their combined clear masks. */
int netevent_set (netevent_t * p_pool,
                  void *       p_key,
                  uint32_t     bits,
                  uint32_t *   p_result,
                  uint32_t *   p_wake)
{
    const uint32_t group = find_group(p_pool, p_key);
    if ((NETEVENT_GROUPS == group) || (NULL == p_result) || (NULL == p_wake) ||
        (0U != (bits & ~NETEVENT_BITS)))
    {
        return -1;
    }
    *p_wake   = publish(p_pool, group, bits);
    *p_result = p_pool->groups[group].bits;
    return 0;
}

/** @brief Return the pre-clear value; never clear unrelated flags. */
int netevent_clear (netevent_t * p_pool,
                    void *       p_key,
                    uint32_t     bits,
                    uint32_t *   p_result)
{
    const uint32_t group = find_group(p_pool, p_key);
    if ((NETEVENT_GROUPS == group) || (NULL == p_result) ||
        (0U != (bits & ~NETEVENT_BITS)))
    {
        return -1;
    }
    *p_result = p_pool->groups[group].bits;
    p_pool->groups[group].bits &= ~bits;
    return 0;
}

/** @brief Read without changing waiter or event state. */
int netevent_get (netevent_t * p_pool, void * p_key, uint32_t * p_result)
{
    return netevent_clear(p_pool, p_key, 0U, p_result);
}

/** @brief Register the wait and barrier contribution in one caller lock. */
int netevent_begin (netevent_t * p_pool,
                    void *       p_key,
                    uint32_t     wanted,
                    bool         b_clear,
                    bool         b_all,
                    uint32_t     sync_bits,
                    bool         b_block,
                    uint32_t *   p_token,
                    uint32_t *   p_result,
                    uint32_t *   p_wake)
{
    const uint32_t group       = find_group(p_pool, p_key);
    uint32_t       token       = NETEVENT_WAITERS;
    uint32_t       snapshot    = 0U;
    bool           b_satisfied = false;
    if ((NETEVENT_GROUPS == group) || (NULL == p_token) || (NULL == p_result) ||
        (NULL == p_wake) || (0U == wanted) ||
        (0U != ((wanted | sync_bits) & ~NETEVENT_BITS)))
    {
        return -1;
    }
    snapshot    = p_pool->groups[group].bits | sync_bits;
    b_satisfied = matches(snapshot, wanted, b_all);
    if ((!b_satisfied) && b_block)
    {
        for (uint32_t index = 0U; index < NETEVENT_WAITERS; ++index)
        {
            if (!p_pool->waiters[index].b_used)
            {
                token = index;
                break;
            }
        }
        if (NETEVENT_WAITERS == token)
        {
            return -1;
        }
    }
    *p_wake   = publish(p_pool, group, sync_bits);
    *p_token  = NETEVENT_WAITERS;
    *p_result = snapshot;
    if (b_satisfied || (!b_block))
    {
        if (b_satisfied && b_clear)
        {
            p_pool->groups[group].bits &= ~wanted;
        }
        return 1;
    }
    p_pool->waiters[token] = (netevent_waiter_t){.group   = group,
                                                 .wanted  = wanted,
                                                 .b_all   = b_all,
                                                 .b_clear = b_clear,
                                                 .b_used  = true};
    ++p_pool->groups[group].users;
    *p_token = token;
    return 0;
}

/** @brief Release completed records while pinning retired group identities. */
int netevent_finish (netevent_t * p_pool,
                     uint32_t     token,
                     bool         b_timeout,
                     uint32_t *   p_result)
{
    netevent_waiter_t * p_wait  = NULL;
    netevent_group_t *  p_group = NULL;
    if ((NULL == p_pool) || (NETEVENT_WAITERS <= token) || (NULL == p_result) ||
        (!p_pool->waiters[token].b_used))
    {
        return -1;
    }
    p_wait  = &p_pool->waiters[token];
    p_group = &p_pool->groups[p_wait->group];
    if ((!p_wait->b_ready) && (!b_timeout))
    {
        return 0;
    }
    *p_result      = p_wait->b_ready ? p_wait->result : p_group->bits;
    p_wait->b_used = false;
    --p_group->users;
    if ((!p_group->b_live) && (0U == p_group->users))
    {
        p_group->p_key = NULL;
    }
    return 1;
}

/** @brief Opaque identities are compared, never dereferenced or subtracted. */
static bool key_taken (netevent_t const * p_pool, void const * p_key)
{
    for (uint32_t index = 0U; index < NETEVENT_GROUPS; ++index)
    {
        if (p_pool->groups[index].p_key == p_key)
        {
            return true;
        }
    }
    return false;
}

/** @brief Find only live groups; retired identities remain pinned separately.
 */
static uint32_t find_group (netevent_t const * p_pool, void const * p_key)
{
    if ((NULL != p_pool) && (NULL != p_key))
    {
        for (uint32_t index = 0U; index < NETEVENT_GROUPS; ++index)
        {
            if ((p_pool->groups[index].b_live) &&
                (p_pool->groups[index].p_key == p_key))
            {
                return index;
            }
        }
    }
    return NETEVENT_GROUPS;
}

/** @brief The usable 24 bits retain independent any/all wait semantics. */
static bool matches (uint32_t bits, uint32_t wanted, bool b_all)
{
    return b_all ? ((bits & wanted) == wanted) : (0U != (bits & wanted));
}

/** @brief Broadcast one snapshot, then clear only satisfied wait masks. */
static uint32_t publish (netevent_t * p_pool, uint32_t group, uint32_t bits)
{
    uint32_t           clear   = 0U;
    uint32_t           wake    = 0U;
    netevent_group_t * p_group = &p_pool->groups[group];
    p_group->bits |= bits;
    for (uint32_t index = 0U; index < NETEVENT_WAITERS; ++index)
    {
        netevent_waiter_t * p_wait = &p_pool->waiters[index];
        if ((p_wait->b_used) && (!p_wait->b_ready) &&
            (p_wait->group == group) &&
            (matches(p_group->bits, p_wait->wanted, p_wait->b_all)))
        {
            p_wait->result  = p_group->bits;
            p_wait->b_ready = true;
            wake |= UINT32_C(1) << index;
            if (p_wait->b_clear)
            {
                clear |= p_wait->wanted;
            }
        }
    }
    p_group->bits &= ~clear;
    return wake;
}

/* End of file. */

