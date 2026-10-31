#ifndef UG_PWM_MOTOR_H
#define UG_PWM_MOTOR_H
#include "ugio.h"

/* Unidirectional, active-high external motor driver; never drive a motor
 * directly from GPIO. Runtime owns watchdog/staleness and calls stop. */
typedef struct
{
    unsigned pin;
    uint32_t frequency_hz;
    bool     b_ready;
} pwm_motor_t;

ugio_status_t
pwm_motor_init (pwm_motor_t *, const ugio_t *, unsigned, uint32_t);
ugio_status_t
pwm_motor_set (pwm_motor_t *, const ugio_t *, uint16_t duty_permille);
ugio_status_t pwm_motor_stop (pwm_motor_t *, const ugio_t *);
#endif

/* End of file. */

