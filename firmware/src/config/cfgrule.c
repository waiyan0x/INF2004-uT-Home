/** @file cfgrule.c
 * @brief Admit one deterministic rule per enabled output, with typed sources.
 */
#include "cfgrule.h"
static bool rule (const cfgjson_value_t * p_value,
                  cfgplan_t *             p_plan,
                  cfgplan_rule_t *        p_rule);
static size_t device (const cfgjson_value_t * p_value,
                      const cfgplan_t *       p_plan);
static bool metric (const cfgjson_value_t * p_value,
                    cfgplan_driver_t        driver,
                    rulecalc_metric_t *     p_metric);

/** @brief New rule fields require schema 2.1; schema 2.0 remains sensor-only.
 */
bool cfgrule_read (const cfgjson_value_t * p_root, cfgplan_t * p_plan)
{
    const cfgjson_value_t * p_rules = cfgjson_get(p_root, "rules");
    const bool              b_version =
        cfgjson_is(cfgjson_get(p_root, "schema_version"), "2.1.0");
    if ((NULL != p_rules) &&
        ((!b_version) || (!cfgjson_array(p_rules, 4U, &p_plan->rule_count))))
    {
        return false;
    }
    uint32_t outputs = 0U;
    for (size_t index = 0U; index < p_plan->rule_count; ++index)
    {
        cfgplan_rule_t * p_rule = &p_plan->rules[index];
        if (!rule(cfgjson_at(p_rules, index), p_plan, p_rule))
        {
            return false;
        }
        const uint32_t bit = UINT32_C(1) << p_rule->output_index;
        if (0U != (outputs & bit))
        {
            return false;
        }
        outputs |= bit;
    }
    for (size_t index = 0U; index < p_plan->device_count; ++index)
    {
        const cfgplan_device_t * p_device = &p_plan->devices[index];
        if ((CFGPLAN_PWM == p_device->driver) &&
            ((!b_version) || ((p_device->b_enabled) &&
                              (0U == (outputs & (UINT32_C(1) << index))))))
        {
            return false;
        }
    }
    return true;
}

/** @brief Resolve names to stable plan indices, without modifying live state.
 */
static size_t device (const cfgjson_value_t * p_value, const cfgplan_t * p_plan)
{
    size_t index = 0U;
    while ((index < p_plan->device_count) &&
           (!cfgjson_is(p_value, p_plan->devices[index].name)))
    {
        ++index;
    }
    return index;
}

/** @brief Resolve reviewed metric spelling and its required sample producer. */
static bool metric (const cfgjson_value_t * p_value,
                    cfgplan_driver_t        driver,
                    rulecalc_metric_t *     p_metric)
{
    static const char * const p_names[] = {
        "distance_mm", "temperature_centi_c", "pressure_pa", "target_count"};
    const cfgplan_driver_t types[] = {
        CFGPLAN_DISTANCE, CFGPLAN_ENV, CFGPLAN_ENV, CFGPLAN_RADAR};
    for (size_t index = 0U; index < RULECALC_METRIC_COUNT; ++index)
    {
        if ((types[index] == driver) && (cfgjson_is(p_value, p_names[index])))
        {
            *p_metric =
                (rulecalc_metric_t)index; /* Bounded metric table index. */
            return true;
        }
    }
    return false;
}

/** @brief Copy only bounded scalar rule settings and reject ambiguous routing.
 */
static bool rule (const cfgjson_value_t * p_value,
                  cfgplan_t *             p_plan,
                  cfgplan_rule_t *        p_rule)
{
    static const char * const p_keys[] = {"source",
                                          "target",
                                          "metric",
                                          "comparison",
                                          "threshold",
                                          "hysteresis",
                                          "cooldown_ms",
                                          "max_age_ms",
                                          "duty_permille"};
    if (!cfgjson_object(p_value, p_keys, 9U, 511U))
    {
        return false;
    }
    const size_t source = device(cfgjson_get(p_value, "source"), p_plan);
    const size_t output = device(cfgjson_get(p_value, "target"), p_plan);
    if ((source >= p_plan->device_count) || (output >= p_plan->device_count) ||
        (CFGPLAN_PWM != p_plan->devices[output].driver) ||
        ((p_plan->devices[output].b_enabled) &&
         (!p_plan->devices[source].b_enabled)))
    {
        return false;
    }
    /* Both copied indices fit the eight-instance plan. */
    p_rule->output_index                 = (uint8_t)output;
    p_rule->config.source_index          = (uint16_t)source;
    rulecalc_config_t *     p_config     = &p_rule->config;
    uint32_t                duty         = 0U;
    const cfgjson_value_t * p_comparison = cfgjson_get(p_value, "comparison");
    p_config->b_above                    = cfgjson_is(p_comparison, "above");
    const bool b_ok =
        (metric(cfgjson_get(p_value, "metric"),
                p_plan->devices[source].driver,
                &p_config->metric)) &&
        ((p_config->b_above) || (cfgjson_is(p_comparison, "below"))) &&
        (cfgjson_int(cfgjson_get(p_value, "threshold"),
                     -1000000000,
                     1000000000,
                     &p_config->threshold)) &&
        (cfgjson_uint(cfgjson_get(p_value, "hysteresis"),
                      0U,
                      1000000U,
                      &p_config->hysteresis)) &&
        (cfgjson_uint(cfgjson_get(p_value, "cooldown_ms"),
                      0U,
                      60000U,
                      &p_config->cooldown_ms)) &&
        (cfgjson_uint(cfgjson_get(p_value, "max_age_ms"),
                      1U,
                      60000U,
                      &p_config->max_age_ms)) &&
        (cfgjson_uint(cfgjson_get(p_value, "duty_permille"), 1U, 1000U, &duty));
    p_config->duty_permille =
        (uint16_t)duty; /* Zero or the validated 1..1000. */
    return b_ok;
}

/* End of file. */

