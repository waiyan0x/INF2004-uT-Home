#include "pwm_motor.h"
ug_status ug_motor_init(ug_motor *m, const uart_gpio_io *io, unsigned pin, uint32_t hz) {
    if (!m || !io || !io->pwm_set || !hz) return UG_INVALID;
    *m = (ug_motor){pin, hz, false};
    ug_status s = io->pwm_set(io->ctx, pin, hz, 0);
    m->ready = s == UG_OK;
    return s;
}
ug_status ug_motor_set(ug_motor *m, const uart_gpio_io *io, uint16_t duty) {
    if (!m || !m->ready || !io || !io->pwm_set || duty > 1000) return UG_INVALID;
    return io->pwm_set(io->ctx, m->pin, m->hz, duty);
}
ug_status ug_motor_stop(ug_motor *m, const uart_gpio_io *io) { return ug_motor_set(m, io, 0); }
