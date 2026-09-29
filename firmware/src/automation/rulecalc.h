/**
 * @file rulecalc.h
 * @brief One bounded threshold rule over a normalized source observation.
 */
#ifndef UTHOME_RULECALC_H
#define UTHOME_RULECALC_H
#include "sampcache.h"

typedef enum
{
    RULECALC_DISTANCE,
    RULECALC_TEMPERATURE,
    RULECALC_PRESSURE,
    RULECALC_TARGETS,
    RULECALC_METRIC_COUNT
} rulecalc_metric_t;

typedef struct
{
    int32_t           threshold;
    uint32_t          hysteresis;
    uint32_t          cooldown_ms;
    uint32_t          max_age_ms;
    rulecalc_metric_t metric;
    uint16_t          source_index;
    uint16_t          duty_permille;
    bool              b_above;
} rulecalc_config_t;

typedef struct
{
    uint64_t last_ms;
    uint64_t changed_ms;
    bool     b_started;
    bool     b_changed;
    bool     b_active;
} rulecalc_state_t;

/* One output worker owns zero-initialized state. No I/O or allocation.
 * Distance is mm, temperature hundredths of C, pressure Pa, targets a count.
 * Entry is a coherent cache copy at now_ms, or NULL if unavailable.
 * Missing/stale/fault data and clock regression request zero immediately,
 * bypass cooldown and restart its holdoff. Ordinary transitions obey cooldown.
 * Return 0 with duty, -1 for invalid arguments/configuration. */
int rulecalc_step (const rulecalc_config_t * p_config,
                   rulecalc_state_t *        p_state,
                   const sampcache_entry_t * p_entry,
                   uint64_t                  now_ms,
                   uint16_t *                p_duty);
#endif
/* End of file. */

