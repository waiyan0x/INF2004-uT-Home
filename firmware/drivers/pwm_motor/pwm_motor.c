#include "pwm_motor.h"

ugio_status_t pwm_motor_init (pwm_motor_t *  p_motor,
                              const ugio_t * p_io,
                              unsigned       pin,
                              uint32_t       frequency_hz)
{
    if (((NULL == p_motor)) || ((NULL == p_io)) ||
        ((NULL == p_io->p_pwm_set)) || ((0U == frequency_hz)))
    {
        return UG_INVALID;
    }
    *p_motor = (pwm_motor_t){pin, frequency_hz, false};
    ugio_status_t status =
        p_io->p_pwm_set(p_io->p_context, pin, frequency_hz, 0);
    p_motor->b_ready = UG_OK == status;
    return status;
}

ugio_status_t
pwm_motor_set (pwm_motor_t * p_motor, const ugio_t * p_io, uint16_t duty)
{
    if (((NULL == p_motor)) || (!p_motor->b_ready) || ((NULL == p_io)) ||
        ((NULL == p_io->p_pwm_set)) || (duty > 1000))
    {
        return UG_INVALID;
    }
    return p_io->p_pwm_set(
        p_io->p_context, p_motor->pin, p_motor->frequency_hz, duty);
}

ugio_status_t pwm_motor_stop (pwm_motor_t * p_motor, const ugio_t * p_io)
{
    return pwm_motor_set(p_motor, p_io, 0);
}

/* End of file. */

