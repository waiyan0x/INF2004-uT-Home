/**
 * @file gpiofake.h
 * @brief Inject capture and PWM faults with shared pin claims.
 */
#ifndef UTHOME_GPIOFAKE_H
#define UTHOME_GPIOFAKE_H
#include "pulseio.h"
#include "pwmio.h"
#include "pinpool.h"

typedef struct
{
    pulseio_t   pulse;
    pwmio_t     pwm;
    pinpool_t * p_pins;
    uint64_t    now_ms;
    uint32_t    high_us;
    uint32_t    elapsed_ms;
    uint32_t    pulse_mask;
    uint32_t    pwm_mask;
    uint32_t    captures;
    uint32_t    last_timeout_us;
    uint16_t    duty_permille;
    int         open_error;
    int         close_error;
    int         measure_error;
    int         now_error;
    int         set_error;
} gpiofake_t;

/* Initialize only before use or after cleanup. Shared pool must outlive ports.
 */
void gpiofake_init (gpiofake_t * p_fake, pinpool_t * p_pins);
#endif
/* End of file. */

