/**
 * @file pwminst.h
 * @brief Expose a generic PWM component with zero-duty cleanup.
 */
#ifndef UTHOME_PWMINST_H
#define UTHOME_PWMINST_H

#include "pwmio.h"
#include "uthome/drvreg.h"

typedef struct
{
    uint16_t duty_permille;
} pwminst_command_t;

typedef struct
{
    pwmio_t * p_port;
    bool      b_configured;
} pwminst_t;

/* Zero-initialize, set the borrowed p_port, then bind with no I/O.
 * Configure with pwmio_config_t; write with pwminst_command_t.
 * One command owner serializes writes with lifecycle/cleanup. No automatic
 * motion, feedback control, direction or arbitrary actuator policy is implied.
 */
const drvreg_descriptor_t * pwminst_descriptor (void);
int pwminst_cleanup (void * p_context);

#endif
/* End of file. */

