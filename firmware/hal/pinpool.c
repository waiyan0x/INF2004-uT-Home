/**
 * @file pinpool.c
 * @brief Validate all pins before changing any ownership.
 */
#include "pinpool.h"
#include <stddef.h>

/** @brief Reject reserved pins and collisions before recording claims. */
int pinpool_claim (pinpool_t * p_pool, uint32_t mask, const void * p_owner)
{
    if ((NULL == p_pool) || (NULL == p_owner) || (0U == mask) ||
        (0U != (mask & ~p_pool->allowed)))
    {
        return -1;
    }
    for (unsigned pin = 0U; pin < 32U; ++pin)
    {
        if ((0U != (mask & (UINT32_C(1) << pin))) &&
            (NULL != p_pool->p_owners[pin]))
        {
            return -1;
        }
    }
    for (unsigned pin = 0U; pin < 32U; ++pin)
    {
        if (0U != (mask & (UINT32_C(1) << pin)))
        {
            p_pool->p_owners[pin] = p_owner;
        }
    }
    return 0;
}

/** @brief Reject another owner's release without changing any claims. */
int pinpool_release (pinpool_t * p_pool, uint32_t mask, const void * p_owner)
{
    if ((NULL == p_pool) || (NULL == p_owner) || (0U == mask) ||
        (0U != (mask & ~p_pool->allowed)))
    {
        return -1;
    }
    for (unsigned pin = 0U; pin < 32U; ++pin)
    {
        if ((0U != (mask & (UINT32_C(1) << pin))) &&
            (p_owner != p_pool->p_owners[pin]))
        {
            return -1;
        }
    }
    for (unsigned pin = 0U; pin < 32U; ++pin)
    {
        if (0U != (mask & (UINT32_C(1) << pin)))
        {
            p_pool->p_owners[pin] = NULL;
        }
    }
    return 0;
}

/* End of file. */

