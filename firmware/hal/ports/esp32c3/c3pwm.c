/**
 * @file c3pwm.c
 * @brief Drive PWM through LEDC without a FreeRTOS driver.
 */
#include "c3pwm.h"
#include <tk/tkernel.h>
#include "workport.h"
#include "c3pin.h"
#include "esp_private/esp_clk.h"
#include "esp_private/periph_ctrl.h"
#include "esp_rom_gpio.h"
#include "hal/ledc_ll.h"
#include "soc/gpio_sig_map.h"

typedef struct
{
    c3pin_output_t output;
    ID             h_mutex;
    uint8_t        channel;
    bool           b_clock;
} c3pwm_state_t;

static int open_port (void * p_context, const pwmio_config_t * p_config);
static int close_port (void * p_context);
static int set_duty (void * p_context, uint16_t duty_permille);
static int apply_duty (c3pwm_state_t * p_state, uint16_t duty_permille);
static int stop_output (c3pwm_state_t * p_state);
static c3pwm_state_t g_states[4] = {
    {.channel = 0U}, {.channel = 1U}, {.channel = 2U}, {.channel = 3U}};

static const pwmio_ops_t g_operations = {
    .p_open = open_port, .p_close = close_port, .p_set = set_duty};

static pwmio_t g_ports[4] = {
    {.p_operations = &g_operations, .p_context = &g_states[0]},
    {.p_operations = &g_operations, .p_context = &g_states[1]},
    {.p_operations = &g_operations, .p_context = &g_states[2]},
    {.p_operations = &g_operations, .p_context = &g_states[3]}};

static uint8_t g_users      = 0U;
static bool    gb_attempted = false;
static bool    gb_ready     = false;

/** @brief Create the fixed mutex set once before normal operation begins.
 * A partial failure retains created objects and fails closed until reboot. */
int c3pwm_startup (void)
{
    if (!rt_port_is_owner())
    {
        return PWMIO_STATE;
    }
    if (gb_attempted)
    {
        return gb_ready ? 0 : PWMIO_IO;
    }
    gb_attempted       = true;
    const T_CMTX mutex = {.mtxatr = TA_INHERIT};
    for (size_t index = 0U; index < 4U; ++index)
    {
        const ID h_mutex = tk_cre_mtx(&mutex);
        if (h_mutex <= 0)
        {
            return PWMIO_IO;
        }
        g_states[index].h_mutex = h_mutex;
    }
    gb_ready = true;
    return 0;
}

/** @brief Borrow static storage without enabling a clock or changing a pin. */
pwmio_t * c3pwm_port (uint8_t channel)
{
    return (channel < 4U) ? &g_ports[channel] : NULL;
}

/** @brief Pair each channel with its own timer; setup leaves output low. */
static int open_port (void * p_context, const pwmio_config_t * p_config)
{
    c3pwm_state_t * p_state = p_context;
    if ((!rt_port_is_owner()) || (!gb_ready) || (p_state->b_clock) ||
        (p_state->output.b_open) || (p_config->frequency_hz < 100U) ||
        (p_config->frequency_hz > 20000U))
    {
        return PWMIO_INVALID;
    }
    const uint64_t numerator   = (uint64_t)esp_clk_xtal_freq() * 256U;
    const uint32_t denominator = p_config->frequency_hz * 1024U;
    const uint64_t divider     = (numerator + denominator - 1U) / denominator;
    if ((divider < 256U) || (divider > UINT32_C(0x3FFFF)))
    {
        return PWMIO_INVALID;
    }
    if (0 != c3pin_open(&p_state->output, p_config->gpio, false))
    {
        return PWMIO_INVALID;
    }
    if (0U == g_users)
    {
        PERIPH_RCC_ATOMIC()
        {
            ledc_ll_enable_bus_clock(0, true);
            ledc_ll_reset_register(0);
        }
        ledc_ll_set_slow_clk_sel(&LEDC, LEDC_SLOW_CLK_XTAL);
        ledc_ll_enable_interrupt(&LEDC, UINT32_MAX, false);
    }
    ++g_users;
    p_state->b_clock = true;
    /* Factory channel indices 0..3 are valid timer/channel enumerators. */
    const ledc_timer_t   timer   = (ledc_timer_t)p_state->channel;
    const ledc_channel_t channel = (ledc_channel_t)p_state->channel;
    ledc_ll_timer_pause(&LEDC, LEDC_LOW_SPEED_MODE, timer);
    /* Divider was checked against the 18-bit hardware field above. */
    ledc_ll_set_clock_divider(
        &LEDC, LEDC_LOW_SPEED_MODE, timer, (uint32_t)divider);
    ledc_ll_set_duty_resolution(&LEDC, LEDC_LOW_SPEED_MODE, timer, 10U);
    ledc_ll_ls_timer_update(&LEDC, LEDC_LOW_SPEED_MODE, timer);
    ledc_ll_timer_rst(&LEDC, LEDC_LOW_SPEED_MODE, timer);
    ledc_ll_bind_channel_timer(&LEDC, LEDC_LOW_SPEED_MODE, channel, timer);
    const int result = apply_duty(p_state, 0U);
    ledc_ll_timer_resume(&LEDC, LEDC_LOW_SPEED_MODE, timer);
    ledc_ll_ls_timer_update(&LEDC, LEDC_LOW_SPEED_MODE, timer);
    return result;
}

/** @brief Drive low before fallible cleanup and retain claims on failures. */
static int close_port (void * p_context)
{
    c3pwm_state_t * p_state = p_context;
    if ((!rt_port_is_owner()) && (!rt_port_is_cleanup_owner()))
    {
        return PWMIO_STATE;
    }
    if ((p_state->output.b_open) && (0 != stop_output(p_state)))
    {
        return PWMIO_IO;
    }
    /* Mutex lifetime is the boot lifetime, independent of hardware claims. */
    if ((p_state->output.b_open) && (0 != c3pin_close(&p_state->output)))
    {
        return PWMIO_IO;
    }
    if (p_state->b_clock)
    {
        --g_users;
        p_state->b_clock = false;
        if (0U == g_users)
        {
            PERIPH_RCC_ATOMIC()
            {
                ledc_ll_enable_bus_clock(0, false);
            }
        }
    }
    return 0;
}

/** @brief Serialize a bounded update without waiting for a previous writer. */
static int set_duty (void * p_context, uint16_t duty_permille)
{
    c3pwm_state_t * p_state = p_context;
    if ((!p_state->b_clock) || (!p_state->output.b_open))
    {
        return PWMIO_STATE;
    }
    const ER locked = tk_loc_mtx(p_state->h_mutex, TMO_POL);
    if (E_OK != locked)
    {
        return (E_TMOUT == locked) ? PWMIO_BUSY : PWMIO_IO;
    }
    const int result = apply_duty(p_state, duty_permille);
    if (E_OK != tk_unl_mtx(p_state->h_mutex))
    {
        /* Preserve the mutex/claim but force the physical output low. */
        const int stopped = stop_output(p_state);
        return (0 == stopped) ? PWMIO_IO : PWMIO_STATE;
    }
    return result;
}

/** @brief Endpoints are fixed GPIO levels, avoiding a full-period overflow. */
static int apply_duty (c3pwm_state_t * p_state, uint16_t duty_permille)
{
    /* The factory bounds channel to the first four hardware enumerators. */
    const ledc_channel_t channel = (ledc_channel_t)p_state->channel;
    if ((0U == duty_permille) || (1000U == duty_permille))
    {
        /* Valid retained output means set cannot fail in serialized use. */
        const int result =
            c3pin_set(&p_state->output, (1000U == duty_permille));
        if (0 == result)
        {
            esp_rom_gpio_connect_out_signal(
                p_state->output.pin, SIG_GPIO_OUT_IDX, false, false);
        }
        else
        {
            return PWMIO_IO;
        }
        ledc_ll_set_sig_out_en(&LEDC, LEDC_LOW_SPEED_MODE, channel, false);
    }
    else
    {
        const uint32_t ticks =
            (((uint32_t)duty_permille * 1024U) + 500U) / 1000U;
        ledc_ll_set_hpoint(&LEDC, LEDC_LOW_SPEED_MODE, channel, 0U);
        ledc_ll_set_duty_int_part(&LEDC, LEDC_LOW_SPEED_MODE, channel, ticks);
        ledc_ll_set_duty_direction(
            &LEDC, LEDC_LOW_SPEED_MODE, channel, LEDC_DUTY_DIR_INCREASE);
        ledc_ll_set_duty_num(&LEDC, LEDC_LOW_SPEED_MODE, channel, 1U);
        ledc_ll_set_duty_cycle(&LEDC, LEDC_LOW_SPEED_MODE, channel, 1U);
        ledc_ll_set_duty_scale(&LEDC, LEDC_LOW_SPEED_MODE, channel, 0U);
        ledc_ll_set_sig_out_en(&LEDC, LEDC_LOW_SPEED_MODE, channel, true);
        ledc_ll_set_duty_start(&LEDC, LEDC_LOW_SPEED_MODE, channel);
        ledc_ll_ls_channel_update(&LEDC, LEDC_LOW_SPEED_MODE, channel);
        esp_rom_gpio_connect_out_signal(p_state->output.pin,
                                        LEDC_LS_SIG_OUT0_IDX + p_state->channel,
                                        false,
                                        false);
    }
    ledc_ll_ls_channel_update(&LEDC, LEDC_LOW_SPEED_MODE, channel);
    return 0;
}

/** @brief Reconnect a low GPIO latch before stopping the independent timer. */
static int stop_output (c3pwm_state_t * p_state)
{
    if (0 != c3pin_set(&p_state->output, false))
    {
        return PWMIO_IO;
    }
    esp_rom_gpio_connect_out_signal(
        p_state->output.pin, SIG_GPIO_OUT_IDX, false, false);
    if (p_state->b_clock)
    {
        /* Factory bounds both enum conversions to 0..3. */
        const ledc_channel_t channel = (ledc_channel_t)p_state->channel;
        const ledc_timer_t   timer   = (ledc_timer_t)p_state->channel;
        ledc_ll_set_sig_out_en(&LEDC, LEDC_LOW_SPEED_MODE, channel, false);
        ledc_ll_ls_channel_update(&LEDC, LEDC_LOW_SPEED_MODE, channel);
        ledc_ll_timer_pause(&LEDC, LEDC_LOW_SPEED_MODE, timer);
        ledc_ll_ls_timer_update(&LEDC, LEDC_LOW_SPEED_MODE, timer);
    }
    return 0;
}

/* End of file. */

