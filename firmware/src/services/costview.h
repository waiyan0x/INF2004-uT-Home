/**
 * @file costview.h
 * @brief Encode bounded worker costs and empirical stack observations.
 */
#ifndef UTHOME_COSTVIEW_H
#define UTHOME_COSTVIEW_H

#include "activate.h"
#include "cfgctrl.h"

#define COSTVIEW_HEADER 96U
#define COSTVIEW_ROW    72U
#define COSTVIEW_BYTES  384U

typedef int costview_stack_fn_t (uint32_t * p_unused);

typedef struct
{
    uint64_t elapsed_ms;
    uint32_t idle_ms;
    uint32_t tick_ms;
} costview_cpu_t;

typedef int costview_cpu_fn_t (costview_cpu_t * p_sample);

typedef struct
{
    const cfgctrl_t *     p_control;
    rt_port_clock_fn_t *  p_clock;
    costview_stack_fn_t * p_controller;
    costview_stack_fn_t * p_idle;
    costview_cpu_fn_t *   p_cpu;
    rt_port_cost_t        costs[UTHOME_RUNTIME_MAX_DEVICE_TASKS];
    rt_port_stack_t       stacks[UTHOME_RUNTIME_MAX_DEVICE_TASKS];
    activate_status_t     runtime;
} costview_t;

/* Static caller-owned context, serialized controller only. No configuration
 * mutation, I/O, allocation or object creation. Version 1 uses a 96-byte header
 * and four 72-byte rows. An optional CPU provider selects version 2 and fills
 * reserved header words with coherent scheduler/idle counters. This is tick
 * sampling, not cycle accounting. Each callback is bounded and cannot reenter
 * the pump. A failed CPU provider leaves that observation explicitly absent.
 * Failure returns a zero-filled header with result 1 busy or 2 unavailable.
 * Stack sentinels are empirical observations, not worst-case proofs. */
int costview_observe (void *    p_context,
                      uint8_t * p_data,
                      size_t    capacity,
                      size_t *  p_size);

#endif

/* End of file. */

