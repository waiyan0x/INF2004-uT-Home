/**
 * @file aiload.c
 * @brief Bound experimental inference while real peripheral workers run.
 */
#include "aiload.h"

#if defined(UTHOME_MODEL_LOAD_PROBE) && UTHOME_MODEL_LOAD_PROBE
#include "ailite.h"
#include "aivects.h"
#include "c3clock.h"
#include "costview.h"

#define AILOAD_PHASE_US UINT64_C(75000000)

static int predict_batch (uint32_t * p_duration);
static uint64_t g_started_us = 0U;
static uint32_t g_index      = 0U;
static uint32_t g_total      = 0U;
static uint32_t g_mismatches = 0U;
static bool     gb_started   = false;

/** @brief No task, allocation, profile mutation or unsolicited serial output.
 */
int aiload_observe (void *    p_context,
                    uint8_t * p_data,
                    size_t    capacity,
                    size_t *  p_size)
{
    uint64_t now_us      = 0U;
    uint32_t duration_us = 0U;
    if ((NULL == p_context) || (NULL == p_data) || (NULL == p_size) ||
        (capacity < COSTVIEW_BYTES) || (0U != AILITE_DEPLOYABLE) ||
        (0U == aivects_count()) || (0 != c3clock_now(&now_us)))
    {
        return -1;
    }
    if (!gb_started)
    {
        g_started_us = now_us;
        gb_started   = true;
    }
    if (now_us < g_started_us)
    {
        return -1;
    }
    const uint64_t elapsed_us = now_us - g_started_us;
    /* Clamp before narrowing; phase four has no inference and stays done. */
    const uint32_t phase = (elapsed_us >= (4U * AILOAD_PHASE_US))
                               ? 4U
                               : (uint32_t)(elapsed_us / AILOAD_PHASE_US);
    if (((1U == phase) || (3U == phase)) && (0 != predict_batch(&duration_us)))
    {
        return -1;
    }
    if ((0 != costview_observe(p_context, p_data, capacity, p_size)) ||
        (COSTVIEW_BYTES != *p_size) || (2U != cfgbytes_get(p_data)))
    {
        return -1;
    }
    cfgbytes_put(p_data, AILOAD_VERSION);
    cfgbytes_put(&p_data[80], phase);
    cfgbytes_put(&p_data[84], g_total);
    cfgbytes_put(&p_data[88], duration_us);
    cfgbytes_put(&p_data[92], g_mismatches);
    return 0;
}

/** @brief Include clock overhead/preemption; retain each comparison outcome. */
static int predict_batch (uint32_t * p_duration)
{
    uint64_t started_us  = 0U;
    uint64_t finished_us = 0U;
    if ((g_total > (UINT32_MAX - 4U)) || (0 != c3clock_now(&started_us)))
    {
        return -1;
    }
    for (uint32_t candidate = 0U; candidate < 4U; ++candidate)
    {
        int32_t          expected   = 0;
        uint32_t const * p_features = aivects_get(g_index, &expected);
        if (NULL == p_features)
        {
            return -1;
        }
        const int actual = ailite_predict(p_features, AILITE_FEATURE_COUNT);
        g_mismatches += (actual != expected) ? 1U : 0U;
        ++g_total;
        g_index = (g_index + 1U) % aivects_count();
    }
    if ((0 != c3clock_now(&finished_us)) || (finished_us < started_us) ||
        ((finished_us - started_us) > UINT32_MAX))
    {
        return -1;
    }
    /* Explicit range check above makes this elapsed-time narrowing exact. */
    *p_duration = (uint32_t)(finished_us - started_us);
    return 0;
}
#endif

/* End of file. */

