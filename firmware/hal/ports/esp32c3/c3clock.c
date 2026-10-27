/**
 * @file c3clock.c
 * @brief Preserve the SDK clock's range and explicit read failure.
 */
#include "c3clock.h"
#include "esp_timer.h"
#include <stddef.h>

/** @brief Supply monotonic wall time; this is not task CPU busy time. */
int c3clock_now (uint64_t * p_now_us)
{
    if (NULL == p_now_us)
    {
        return -1;
    }
    const int64_t now_us = esp_timer_get_time();
    if (now_us < 0)
    {
        return -1;
    }
    /* Nonnegative signed time is representable in uint64_t. */
    *p_now_us = (uint64_t)now_us;
    return 0;
}

/* End of file. */

