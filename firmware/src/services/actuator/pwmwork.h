/**
 * @file pwmwork.h
 * @brief Fixed storage for the recurring output worker.
 */
#ifndef UTHOME_PWMWORK_H
#define UTHOME_PWMWORK_H

#include "pwminst.h"
#include "rulecalc.h"

typedef struct
{
    pwminst_t         instance;
    rulecalc_config_t rule;
    rulecalc_state_t  state;
} pwmwork_t;

/* One output worker owns the rule state and initialized PWM instance. Binding
 * supplies a component referring to this instance; both outlive the worker.
 * A step evaluates one copied sample, updates changed duty, and attempts zero
 * duty on evaluation/write failure while reporting the failure to health. */
int pwmwork_step (pwmwork_t * p_output, component_t * p_component);

#endif

/* End of file. */

