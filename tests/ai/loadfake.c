/**
 * @file loadfake.c
 * @brief Exercise diagnostic timing without USB, tasks or board access.
 */
#include "loadfake.h"
#include "cfgbytes.h"
#include <string.h>

static uint64_t g_now_us = 0U;

/** @brief Set deterministic test time, including intentional reversals. */
void loadfake_set (uint64_t now_us)
{
    g_now_us = now_us;
}

/** @brief Charge five microseconds per read for observable timing overhead. */
int loadfake_now (uint64_t * p_now_us)
{
    if ((NULL == p_now_us) || (g_now_us > (UINT64_MAX - 5U)))
    {
        return -1;
    }
    *p_now_us = g_now_us;
    g_now_us += 5U;
    return 0;
}

/** @brief Produce only the real observer's success shape for wrapper tests. */
int loadfake_cost (void *    p_context,
                   uint8_t * p_data,
                   size_t    capacity,
                   size_t *  p_size)
{
    if ((NULL == p_context) || (NULL == p_data) || (NULL == p_size) ||
        (384U > capacity))
    {
        return -1;
    }
    memset(p_data, 0, 384U);
    cfgbytes_put(p_data, 2U);
    *p_size = 384U;
    return 0;
}

/* End of file. */

