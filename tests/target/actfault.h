/**
 * @file actfault.h
 * @brief One-shot startup observation faults for the real activator.
 */
#ifndef UTHOME_ACTFAULT_H
#define UTHOME_ACTFAULT_H

#include "activate.h"

int actfault_running (rt_port_task_id_t h_task, bool * pb_running);
int actfault_result (rt_port_task_id_t h_task, int * p_result);
void actfault_verify (const activate_plan_t * p_plan);

#endif

/* End of file. */

