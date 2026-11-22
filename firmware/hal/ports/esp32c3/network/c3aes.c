/**
 * @file c3aes.c
 * @brief Lease typed AES contexts across concurrent cipher operations.
 */
#include "c3aes.h"

#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include <stdbool.h>
#include <stddef.h>
#include "c3bport.h"
#include "mbedtls/private/aes.h"

#define C3AES_SLOTS 2U

static mbedtls_aes_context g_contexts[C3AES_SLOTS] = {0};
static bool                gb_used[C3AES_SLOTS]    = {false};

/** @brief The caller initializes the reserved context before using it. */
void * c3aes_take (void)
{
    void *         p_result = NULL;
    const uint32_t saved    = c3bport_enter();
    for (size_t index = 0U; index < C3AES_SLOTS; ++index)
    {
        if (!gb_used[index])
        {
            gb_used[index] = true;
            p_result       = &g_contexts[index];
            break;
        }
    }
    c3bport_leave(saved);
    return p_result;
}

/** @brief The SDK wipes the context before making its slot reusable. */
void c3aes_drop (void * p_context)
{
    const uint32_t saved = c3bport_enter();
    for (size_t index = 0U; index < C3AES_SLOTS; ++index)
    {
        if (p_context == &g_contexts[index])
        {
            gb_used[index] = false;
            break;
        }
    }
    c3bport_leave(saved);
}
#endif

/* End of file. */

