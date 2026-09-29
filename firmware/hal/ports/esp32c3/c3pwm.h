/**
 * @file c3pwm.h
 * @brief Borrow independently timed C3 PWM channels.
 */
#ifndef UTHOME_C3PWM_H
#define UTHOME_C3PWM_H
#include "pwmio.h"
/* Channels 0..3 each own a timer. Frequency 100..20000 Hz, 10-bit duty.
 * Factory has no I/O. Open/close require the runtime lifecycle owner with
 * affected users stopped. All LEDC resources are reserved for this adapter. */
pwmio_t * c3pwm_port (uint8_t channel);
#endif
/* End of file. */

