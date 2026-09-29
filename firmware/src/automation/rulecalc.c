/**
 * @file rulecalc.c
 * @brief Evaluate typed samples with hysteresis, cooldown and zero fallback.
 */
#include "rulecalc.h"
#include <math.h>
static bool value (rulecalc_metric_t         metric,
                   const sampcache_entry_t * p_entry,
                   double *                  p_value);

/** @brief Update one rule with no retained sample pointer or hardware access.
 */
int rulecalc_step (const rulecalc_config_t * p_config,
                   rulecalc_state_t *        p_state,
                   const sampcache_entry_t * p_entry,
                   uint64_t                  now_ms,
                   uint16_t *                p_duty)
{
    if (NULL != p_duty)
    {
        *p_duty = 0U;
    }
    if ((NULL == p_config) || (NULL == p_state) || (NULL == p_duty) ||
        (p_config->metric < RULECALC_DISTANCE) ||
        (p_config->metric >= RULECALC_METRIC_COUNT) ||
        (p_config->source_index >= UTHOME_SAMPCACHE_SOURCES) ||
        (p_config->threshold < -1000000000) ||
        (p_config->threshold > 1000000000) ||
        (p_config->hysteresis > 1000000U) || (p_config->cooldown_ms > 60000U) ||
        (0U == p_config->max_age_ms) || (p_config->max_age_ms > 60000U) ||
        (0U == p_config->duty_permille) || (p_config->duty_permille > 1000U))
    {
        return -1;
    }
    double     sample = 0.0;
    const bool b_fresh =
        (NULL != p_entry) &&
        (p_entry->source.source_index == p_config->source_index) &&
        (1U == sampcache_quality(p_entry, now_ms)) &&
        ((now_ms - p_entry->header.timestamp_ms) < p_config->max_age_ms) &&
        (value(p_config->metric, p_entry, &sample));
    if ((!b_fresh) || ((p_state->b_started) && (now_ms < p_state->last_ms)))
    {
        p_state->b_active   = false;
        p_state->b_changed  = true;
        p_state->changed_ms = now_ms;
    }
    else
    {
        /* All configuration integers fit exactly in double; no integer
         * subtraction can overflow at the hysteresis boundary. */
        const double margin = p_state->b_active ? p_config->hysteresis : 0U;
        const bool   b_next =
            p_config->b_above
                  ? (sample > ((double)p_config->threshold - margin))
                  : (sample < ((double)p_config->threshold + margin));
        if ((b_next != p_state->b_active) &&
            ((!p_state->b_changed) ||
             ((now_ms - p_state->changed_ms) >= p_config->cooldown_ms)))
        {
            p_state->b_active   = b_next;
            p_state->b_changed  = true;
            p_state->changed_ms = now_ms;
        }
    }
    p_state->last_ms   = now_ms;
    p_state->b_started = true;
    *p_duty            = p_state->b_active ? p_config->duty_permille : 0U;
    return 0;
}

/** @brief Map shared sample types to metrics without identifying any driver. */
static bool value (rulecalc_metric_t         metric,
                   const sampcache_entry_t * p_entry,
                   double *                  p_value)
{
    const sampcache_value_t * p_sample = &p_entry->value;
    if ((RULECALC_DISTANCE == metric) &&
        (UTHOME_SAMPLE_DISTANCE == p_entry->source.type))
    {
        *p_value = p_sample->distance.distance_mm;
    }
    else if (((RULECALC_TEMPERATURE == metric) ||
              (RULECALC_PRESSURE == metric)) &&
             (UTHOME_SAMPLE_ENVIRONMENT == p_entry->source.type))
    {
        *p_value = (RULECALC_TEMPERATURE == metric)
                       ? ((double)p_sample->environment.temperature_c * 100.0)
                       : p_sample->environment.pressure_pa;
    }
    else if ((RULECALC_TARGETS == metric) &&
             (UTHOME_SAMPLE_TARGETS == p_entry->source.type))
    {
        *p_value = 0.0;
        for (size_t index = 0U; index < UTHOME_SAMPLE_MAX_TARGETS; ++index)
        {
            if (p_sample->targets.targets[index].b_present)
            {
                *p_value += 1.0;
            }
        }
    }
    else
    {
        return false;
    }
    return isfinite(*p_value);
}

/* End of file. */

