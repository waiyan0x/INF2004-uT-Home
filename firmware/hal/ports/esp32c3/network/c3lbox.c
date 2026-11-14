/**
 * @file c3lbox.c
 * @brief Reuse fixed queue payloads instead of allocating SDK mailboxes.
 */
#include "c3lbox.h"
#include "c3nq.h"

/* The SDK type has a flexible array, so C99 forbids an array of these objects.
 * These typed, permanent identities are read-only; all queue state and payload
 * live in c3nq. The pinned SDK only takes the address of os_mbox after
 * creation. Both allocating creation and freeing deletion are replaced at link
 * time. */
static const struct sys_mbox_s         g_mailbox0 = {0};
static const struct sys_mbox_s         g_mailbox1 = {0};
static const struct sys_mbox_s         g_mailbox2 = {0};
static const struct sys_mbox_s         g_mailbox3 = {0};
static const struct sys_mbox_s         g_mailbox4 = {0};
static const struct sys_mbox_s         g_mailbox5 = {0};
static const struct sys_mbox_s         g_mailbox6 = {0};
static const struct sys_mbox_s         g_mailbox7 = {0};
static struct sys_mbox_s const * const gp_keys[]  = {&g_mailbox0,
                                                     &g_mailbox1,
                                                     &g_mailbox2,
                                                     &g_mailbox3,
                                                     &g_mailbox4,
                                                     &g_mailbox5,
                                                     &g_mailbox6,
                                                     &g_mailbox7};

/** @brief Try each immutable identity; the pool atomically rejects live keys.
 */
err_t c3lbox_new (struct sys_mbox_s ** pp_box, int size)
{
    if (NULL == pp_box)
    {
        return ERR_ARG;
    }
    *pp_box = NULL;
    if (size <= 0)
    {
        return ERR_MEM;
    }
    for (size_t index = 0U; index < (sizeof(gp_keys) / sizeof(gp_keys[0]));
         ++index)
    {
        /* The foreign ABI lacks const. This cast retains the actual SDK type,
         * alignment and permanent lifetime. Neither c3nq nor the pinned mailbox
         * post/fetch functions dereference or modify this identity's contents.
         * Do not pass it to a native FreeRTOS queue implementation. */
        sys_mbox_t p_candidate = (sys_mbox_t)gp_keys[index];
        /* Positive int fits uint32_t on the supported RV32 and host ABIs. */
        if (NULL !=
            c3nq_bind((uint32_t)size, sizeof(void *), &p_candidate->os_mbox))
        {
            *pp_box = p_candidate;
            return ERR_OK;
        }
    }
    return ERR_MEM;
}

/** @brief Validate identity before using the SDK member-address convention. */
void c3lbox_free (struct sys_mbox_s ** pp_box)
{
    if ((NULL == pp_box) || (NULL == *pp_box))
    {
        return;
    }
    for (size_t index = 0U; index < (sizeof(gp_keys) / sizeof(gp_keys[0]));
         ++index)
    {
        if (*pp_box == gp_keys[index])
        {
            void * p_key = &(*pp_box)->os_mbox;
            if ((0U == c3nq_waiting(p_key)) && (0 == c3nq_release(p_key)))
            {
                *pp_box = NULL;
            }
            break;
        }
    }
}

/* End of file. */

