/** @file fanctrl.c
 * @brief Reuse temperature rules without changing shared rules or drivers. */
#include "fanctrl.h"
#include <math.h>
#include <stddef.h>

/** @brief Validate a rule without I/O, then start the relay inactive. */
int fanctrl_open (fanctrl_t *               p_fan,
                  const relayctl_config_t * p_output,
                  const rulecalc_config_t * p_rule,
                  uint64_t                  now_ms)
{
    if ((NULL == p_fan) || (NULL == p_rule) || (p_fan->relay.b_open) ||
        (RULECALC_TEMPERATURE != p_rule->metric) || (!p_rule->b_above) ||
        (1000U != p_rule->duty_permille))
    {
        return -1;
    }
    rulecalc_state_t probe = {0};
    uint16_t         duty  = 0U;
    if (0 != rulecalc_step(p_rule, &probe, NULL, now_ms, &duty))
    {
        return -1;
    }
    p_fan->rule  = *p_rule;
    p_fan->state = (rulecalc_state_t){0};
    return relayctl_open(&p_fan->relay, p_output, now_ms);
}

/** @brief Bypass relay dwell on unusable data, retaining normal hysteresis. */
int fanctrl_step (fanctrl_t *               p_fan,
                  const sampcache_entry_t * p_entry,
                  uint64_t                  now_ms)
{
    if ((NULL == p_fan) || (!p_fan->relay.b_open))
    {
        return -1;
    }
    uint16_t  duty = 0U;
    const int result =
        rulecalc_step(&p_fan->rule, &p_fan->state, p_entry, now_ms, &duty);
    const bool b_valid =
        (0 == result) && (NULL != p_entry) &&
        (p_entry->source.source_index == p_fan->rule.source_index) &&
        (UTHOME_SAMPLE_ENVIRONMENT == p_entry->source.type) &&
        (1U == sampcache_quality(p_entry, now_ms)) &&
        ((now_ms - p_entry->header.timestamp_ms) < p_fan->rule.max_age_ms) &&
        (isfinite(p_entry->value.environment.temperature_c));
    const int applied =
        relayctl_step(&p_fan->relay, 0U != duty, b_valid, now_ms);
    return (0 != result) ? -1 : applied;
}

/** @brief Stop the relay before its lifecycle owner releases the line. */
int fanctrl_close (fanctrl_t * p_fan)
{
    return (NULL == p_fan) ? -1 : relayctl_close(&p_fan->relay);
}

/* End of file. */

