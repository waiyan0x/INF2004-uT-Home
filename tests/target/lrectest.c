/**
 * @file lrectest.c
 * @brief Check exclusion and restoration without simulating SDK networking.
 */
#include "lrectest.h"
#include <assert.h>

#ifdef NDEBUG
#error "Record tests require assertions"
#endif

static uint32_t g_mask  = 0U;
static size_t   g_calls = 0U;

/** @brief Represent nested single-core interrupt exclusion, with a call count.
 */
uint32_t lrectest_enter (void)
{
    const uint32_t saved = g_mask;
    g_mask               = 1U;
    ++g_calls;
    return saved;
}

/** @brief Every leave must restore a saved mask while already excluded. */
void lrectest_leave (uint32_t saved)
{
    assert(1U == g_mask);
    assert(saved <= 1U);
    g_mask = saved;
}

/** @brief Observe the simulated mask after each allocation and release. */
uint32_t lrectest_mask (void)
{
    return g_mask;
}

/** @brief Confirm that each operation crossed the exclusion boundary. */
size_t lrectest_calls (void)
{
    return g_calls;
}

/* End of file. */

