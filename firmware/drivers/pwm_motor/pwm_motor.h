#ifndef UG_PWM_MOTOR_H
#define UG_PWM_MOTOR_H
#include "uart_gpio_io.h"
/* Unidirectional, active-high external motor driver; never drive a motor
 * directly from GPIO. Runtime owns watchdog/staleness and calls stop. */
typedef struct { unsigned pin; uint32_t hz; bool ready; } ug_motor;
ug_status ug_motor_init(ug_motor *, const uart_gpio_io *, unsigned, uint32_t);
ug_status ug_motor_set(ug_motor *, const uart_gpio_io *, uint16_t duty_permille);
ug_status ug_motor_stop(ug_motor *, const uart_gpio_io *);
#endif
