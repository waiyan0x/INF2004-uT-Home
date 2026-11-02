/**
 * @file adfeat.h
 * @brief Derive protocol model inputs from an independently admitted plan.
 */
#ifndef UTHOME_ADFEAT_H
#define UTHOME_ADFEAT_H

#include "cfgplan.h"
#include <stddef.h>
#include <stdint.h>

#define ADFEAT_COUNT 18U

/**
 * @brief Produce the version 0.1 protocol vector, or clear it and abstain.
 *
 * One serialized controller supplies a stable candidate from cfgplan_read.
 * target is an enabled periodic producer; goal is observed sample age in ms.
 * Every candidate must have passed normal admission with its actual interval.
 * This function neither admits a plan nor applies it. The output must not alias
 * the plan. Unsupported drivers/rules fail here without affecting
 * configuration. Build, catalogue, session and radio compatibility are separate
 * adviser gates.
 */
int adfeat_build (cfgplan_t const * p_plan,
                  size_t            target,
                  uint32_t          goal_ms,
                  uint32_t          p_features[ADFEAT_COUNT]);

#endif

/* End of file. */

