/**
 * @file gpiofake.c
 * @brief Keep test failures observable without touching hardware.
 */
#include "gpiofake.h"
static int pulse_open (void * p_context, const pulseio_config_t * p_config);
static int pulse_close (void * p_context);
static int measure (void *     p_context,
                    uint32_t   trigger_us,
                    uint32_t   timeout_us,
                    uint32_t * p_high_us);
static int now_ms (void * p_context, uint64_t * p_now);
static int pwm_open (void * p_context, const pwmio_config_t * p_config);
static int pwm_close (void * p_context);
static int set_duty (void * p_context, uint16_t duty_permille);
static const pulseio_ops_t g_pulse = {.p_open    = pulse_open,
                                      .p_close   = pulse_close,
                                      .p_measure = measure,
                                      .p_now     = now_ms};

static const pwmio_ops_t g_pwm = {
    .p_open = pwm_open, .p_close = pwm_close, .p_set = set_duty};

/** @brief Distinct caller-owned contexts can share the same pin ledger. */
void gpiofake_init (gpiofake_t * p_fake, pinpool_t * p_pins)
{
    if (NULL != p_fake)
    {
        *p_fake = (gpiofake_t){.p_pins = p_pins, .high_us = 5800U};
        p_fake->pulse =
            (pulseio_t){.p_operations = &g_pulse, .p_context = p_fake};
        p_fake->pwm = (pwmio_t){.p_operations = &g_pwm, .p_context = p_fake};
    }
}

/** @brief Claim the whole pin set before simulating a partial setup fault. */
static int pulse_open (void * p_context, const pulseio_config_t * p_config)
{
    gpiofake_t * p_fake = p_context;
    if ((p_config->trigger_gpio >= 32U) || (p_config->input_gpio >= 32U))
    {
        return PULSEIO_INVALID;
    }
    const uint32_t mask = (UINT32_C(1) << p_config->trigger_gpio) |
                          (UINT32_C(1) << p_config->input_gpio);
    if (0 != pinpool_claim(p_fake->p_pins, mask, &p_fake->pulse))
    {
        return PULSEIO_INVALID;
    }
    p_fake->pulse_mask = mask;
    return p_fake->open_error;
}

/** @brief A failed close leaves claims in place for explicit recovery. */
static int pulse_close (void * p_context)
{
    gpiofake_t * p_fake = p_context;
    if (0 != p_fake->close_error)
    {
        return p_fake->close_error;
    }
    if ((0U != p_fake->pulse_mask) &&
        (0 !=
         pinpool_release(p_fake->p_pins, p_fake->pulse_mask, &p_fake->pulse)))
    {
        return PULSEIO_IO;
    }
    p_fake->pulse_mask = 0U;
    return 0;
}

/** @brief Deliberately supply a width on failure to test caller invalidation.
 */
static int measure (void *     p_context,
                    uint32_t   trigger_us,
                    uint32_t   timeout_us,
                    uint32_t * p_high_us)
{
    gpiofake_t * p_fake = p_context;
    if (0U == trigger_us)
    {
        return PULSEIO_INVALID;
    }
    ++p_fake->captures;
    p_fake->now_ms += p_fake->elapsed_ms;
    p_fake->last_timeout_us = timeout_us;
    *p_high_us              = p_fake->high_us;
    return p_fake->measure_error;
}

/** @brief Let tests move the monotonic clock or inject a read failure. */
static int now_ms (void * p_context, uint64_t * p_now)
{
    const gpiofake_t * p_fake = p_context;
    *p_now                    = p_fake->now_ms;
    return p_fake->now_error;
}

/** @brief PWM and pulse capture compete for the same exposed pins. */
static int pwm_open (void * p_context, const pwmio_config_t * p_config)
{
    gpiofake_t * p_fake = p_context;
    if (p_config->gpio >= 32U)
    {
        return PWMIO_INVALID;
    }
    const uint32_t mask = UINT32_C(1) << p_config->gpio;
    if (0 != pinpool_claim(p_fake->p_pins, mask, &p_fake->pwm))
    {
        return PWMIO_INVALID;
    }
    p_fake->pwm_mask      = mask;
    p_fake->duty_permille = 0U;
    return p_fake->open_error;
}

/** @brief Attempt zero output even when subsequent resource cleanup fails. */
static int pwm_close (void * p_context)
{
    gpiofake_t * p_fake   = p_context;
    p_fake->duty_permille = 0U;
    if (0 != p_fake->close_error)
    {
        return p_fake->close_error;
    }
    if ((0U != p_fake->pwm_mask) &&
        (0 != pinpool_release(p_fake->p_pins, p_fake->pwm_mask, &p_fake->pwm)))
    {
        return PWMIO_IO;
    }
    p_fake->pwm_mask = 0U;
    return 0;
}

/** @brief A rejected command does not change the fake's output. */
static int set_duty (void * p_context, uint16_t duty_permille)
{
    gpiofake_t * p_fake = p_context;
    if (0 == p_fake->set_error)
    {
        p_fake->duty_permille = duty_permille;
    }
    return p_fake->set_error;
}

/* End of file. */

