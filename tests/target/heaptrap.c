/**
 * @file heaptrap.c
 * @brief Count and refuse heap calls in instrumented parser test objects.
 */
#ifdef NDEBUG
#error "Heap traps require assertions."
#endif
#include "heaptrap.h"
#include <stdint.h>
static size_t g_count = 0U;
static void record_call (void);

/** @brief An allocation call is always a failed no-heap test. */
void * heaptrap_allocate (size_t size)
{
    /* The forbidden operation is detected regardless of its requested size. */
    (void)size;
    record_call();
    return NULL;
}

/** @brief Trap zero-initialized allocation as well as ordinary allocation. */
void * heaptrap_zero (size_t count, size_t size)
{
    /* Counts cannot make a forbidden allocation acceptable. */
    (void)count;
    (void)size;
    record_call();
    return NULL;
}

/** @brief Never fall back to resizing dynamic storage. */
void * heaptrap_resize (void * p_memory, size_t size)
{
    /* No pointer or size is valid for heap use in this test. */
    (void)p_memory;
    (void)size;
    record_call();
    return NULL;
}

/** @brief Fixed pool cleanup must not call the heap deallocator. */
void heaptrap_release (void * p_memory)
{
    /* Even freeing NULL would indicate an unexpected allocator path. */
    (void)p_memory;
    record_call();
}

/** @brief Report every intercepted call, including refused allocations. */
size_t heaptrap_count (void)
{
    return g_count;
}

/** @brief Preserve a nonzero failure count without wrapping to success. */
static void record_call (void)
{
    if (g_count < SIZE_MAX)
    {
        ++g_count;
    }
}

/* End of file. */

