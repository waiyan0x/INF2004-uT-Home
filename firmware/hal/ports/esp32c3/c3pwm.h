/**
 * @file c3pwm.h
 * @brief Borrow independently timed C3 PWM channels.
 */
#ifndef UTHOME_C3PWM_H
#define UTHOME_C3PWM_H
#include "pwmio.h"
/* Startup lifecycle owner calls once after runtime initialization, before
 * restore, configuration requests or workers. Success is idempotent; a failed
 * attempt cannot allocate again and requires reboot. Never call first during
 * normal operation. Open fails before successful startup. Close retains all
 * 4 mutexes for reuse. No pin/clock changes occur during startup. */
int c3pwm_startup (void);
/* Channels 0..3 each own a timer. Frequency 100..20000 Hz, 10-bit duty.
 * Factory has no I/O. Open/close require the runtime lifecycle owner with
 * affected users stopped. All LEDC resources are reserved for this adapter. */
pwmio_t * c3pwm_port (uint8_t channel);
#endif
/* End of file. */

