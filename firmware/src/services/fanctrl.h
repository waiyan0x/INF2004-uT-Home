/** @file fanctrl.h
 * @brief Connect the existing temperature rule evaluator to a digital relay. */
#ifndef UTHOME_FANCTRL_H
#define UTHOME_FANCTRL_H
#include "relayctl.h"
#include "rulecalc.h"

typedef struct
{
    relayctl_t        relay;
    rulecalc_config_t rule;
    rulecalc_state_t  state;
} fanctrl_t;

/* Zero-initialize and use one owner task. No task, pin or queue is allocated.
 * Only an above-temperature rule with duty_permille=1000 is accepted; this
 * value means ON, never PWM. The existing rule controls hysteresis/freshness.
 * Relay configuration adds its own minimum physical switching interval.
 * Opening failure may retain an output; inspect relay.b_open and close it.
 * No production JSON/registry binding is installed by this optional module. */
int fanctrl_open (fanctrl_t *               p_fan,
                  const relayctl_config_t * p_output,
                  const rulecalc_config_t * p_rule,
                  uint64_t                  now_ms);
/* A coherent source-cache copy is borrowed for this call only; NULL means
 * unavailable. Nonzero return must be reported to health by the caller. */
int fanctrl_step (fanctrl_t *               p_fan,
                  const sampcache_entry_t * p_entry,
                  uint64_t                  now_ms);
int fanctrl_close (fanctrl_t * p_fan);
#endif
/* End of file. */

