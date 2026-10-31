/**
 * @file aiprobe.c
 * @brief Exercise a frozen experimental model without deploying advice.
 */
#include "aiprobe.h"

#if defined(UTHOME_MODEL_PROBE) && UTHOME_MODEL_PROBE
#include "aicases.h"
#include "aifrozen.h"
#include "c3audit.h"
#include "c3clock.h"

static int elapsed (uint64_t started_us, uint32_t * p_elapsed_us);
static bool same_phase (memaudit_phase_t const * p_before,
                        memaudit_phase_t const * p_after);

/** @brief Measure predictions with timer overhead included, never subtracted.
 */
int aiprobe_run (aiprobe_report_t * p_report)
{
    if ((NULL == p_report) || (0U != AIFROZEN_DEPLOYABLE) ||
        (0U == aicases_count()))
    {
        return -1;
    }
    *p_report = (aiprobe_report_t){0};
    if ((0 != c3audit_seal()) || (0 != c3audit_capture(&p_report->before)))
    {
        return -2;
    }
    for (uint32_t index = 0U; index < aicases_count(); ++index)
    {
        int32_t          expected    = 0;
        uint32_t         duration_us = 0U;
        uint64_t         started_us  = 0U;
        uint32_t const * p_features  = aicases_get(index, &expected);
        if ((NULL == p_features) || (0 != c3clock_now(&started_us)))
        {
            return -3;
        }
        const int actual = aifrozen_predict(p_features, AIFROZEN_FEATURE_COUNT);
        if ((0 != elapsed(started_us, &duration_us)) ||
            (duration_us > (UINT32_MAX - p_report->total_us)))
        {
            return -4;
        }
        ++p_report->vectors;
        p_report->mismatches += (actual != expected) ? 1U : 0U;
        p_report->total_us += duration_us;
        if (duration_us > p_report->maximum_us)
        {
            p_report->maximum_us = duration_us;
        }
    }
    for (uint32_t cycle = 0U; cycle < 256U; ++cycle)
    {
        uint64_t started_us  = 0U;
        uint32_t duration_us = 0U;
        if ((0 != c3clock_now(&started_us)) ||
            (0 != elapsed(started_us, &duration_us)))
        {
            return -5;
        }
        if (duration_us > p_report->clock_maximum_us)
        {
            p_report->clock_maximum_us = duration_us;
        }
        if (0 != c3clock_now(&started_us))
        {
            return -6;
        }
        for (uint32_t candidate = 0U; candidate < 4U; ++candidate)
        {
            int32_t          expected   = 0;
            uint32_t const * p_features = aicases_get(
                ((cycle * 4U) + candidate) % aicases_count(), &expected);
            const int result =
                aifrozen_predict(p_features, AIFROZEN_FEATURE_COUNT);
            if ((result < -1) || (result > 1) || (result != expected))
            {
                return -7;
            }
            /* The validated result plus one is nonnegative and at most two. */
            p_report->checksum += (uint32_t)(result + 1);
        }
        if (0 != elapsed(started_us, &duration_us))
        {
            return -8;
        }
        if (duration_us > p_report->batch_maximum_us)
        {
            p_report->batch_maximum_us = duration_us;
        }
    }
    if ((-1 != aifrozen_predict(NULL, AIFROZEN_FEATURE_COUNT)) ||
        (0 != c3audit_capture(&p_report->after)) ||
        (p_report->before.b_saturated) || (p_report->after.b_saturated) ||
        (!same_phase(&p_report->before.startup, &p_report->after.startup)) ||
        (!same_phase(&p_report->before.normal, &p_report->after.normal)))
    {
        return -9;
    }
    return ((0U == p_report->mismatches) &&
            (10000U >= p_report->batch_maximum_us))
               ? 0
               : -10;
}

/** @brief Reject clock reversal and narrowing overflow. */
static int elapsed (uint64_t started_us, uint32_t * p_elapsed_us)
{
    uint64_t now_us = 0U;
    if ((0 != c3clock_now(&now_us)) || (now_us < started_us) ||
        ((now_us - started_us) > UINT32_MAX))
    {
        return -1;
    }
    *p_elapsed_us = (uint32_t)(now_us - started_us);
    return 0;
}

/** @brief Compare fields explicitly; structure padding is not evidence. */
static bool same_phase (memaudit_phase_t const * p_before,
                        memaudit_phase_t const * p_after)
{
    if ((p_before->allocations != p_after->allocations) ||
        (p_before->bytes != p_after->bytes) ||
        (p_before->frees != p_after->frees))
    {
        return false;
    }
    for (uint32_t index = 0U; index < UTHOME_AUDIT_KINDS; ++index)
    {
        if (p_before->creates[index] != p_after->creates[index])
        {
            return false;
        }
    }
    for (uint32_t index = 0U; index < UTHOME_AUDIT_ALLOCATORS; ++index)
    {
        if (p_before->kernel_calls[index] != p_after->kernel_calls[index])
        {
            return false;
        }
    }
    return true;
}
#endif

/* End of file. */

