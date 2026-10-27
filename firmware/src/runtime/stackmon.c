/**
 * @file stackmon.c
 * @brief Inspect only a reviewed bounded stack's object representation.
 */
#include "stackmon.h"
#include <string.h>

#define STACKMON_PATTERN 0xa5U

/** @brief Establish the sentinel before native task-context construction. */
int stackmon_paint (void * p_storage, size_t size)
{
    if ((NULL == p_storage) || (0U == size) || (size > STACKMON_MAX_BYTES))
    {
        return -1;
    }
    memset(p_storage, STACKMON_PATTERN, size);
    return 0;
}

/** @brief Count untouched bytes from the low end, stopping at the first use. */
int stackmon_unused (const void * p_storage, size_t size, size_t * p_unused)
{
    if ((NULL == p_storage) || (NULL == p_unused) || (0U == size) ||
        (size > STACKMON_MAX_BYTES))
    {
        return -1;
    }
    const unsigned char * p_bytes = p_storage;
    size_t                unused  = 0U;
    while ((unused < size) && (STACKMON_PATTERN == p_bytes[unused]))
    {
        ++unused;
    }
    *p_unused = unused;
    return 0;
}

/* End of file. */

