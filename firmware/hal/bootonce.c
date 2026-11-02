/**
 * @file bootonce.c
 * @brief Publish readiness only after every permanent resource is created.
 */
#include "bootonce.h"

/** @brief Attempt each resource at most once, retaining a partial failure. */
int bootonce_run (bootonce_t *        p_state,
                  size_t              count,
                  bootonce_create_t * p_create,
                  void *              p_context)
{
    if ((NULL == p_state) || (NULL == p_create) || (0U == count))
    {
        return -1;
    }
    if (p_state->b_attempted)
    {
        return p_state->b_ready ? 0 : -1;
    }
    p_state->b_attempted = true;
    for (size_t index = 0U; index < count; ++index)
    {
        if (0 != p_create(p_context, index))
        {
            return -1;
        }
    }
    p_state->b_ready = true;
    return 0;
}

/** @brief Partial startup never exposes resources to normal operation. */
bool bootonce_ready (const bootonce_t * p_state)
{
    return (NULL != p_state) && (p_state->b_ready);
}

/* End of file. */

