/**
 * @file c3pin.c
 * @brief Retain exclusive pin claims and provide bounded output callbacks.
 */
#include "c3pin.h"
#include "pinpool.h"
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/io_mux_reg.h"
static pinpool_t g_pins = {.allowed = UINT32_C(0x4FB)};

/** @brief Admit the exposed, unreserved baseline GPIO set. */
bool c3pin_valid (uint8_t pin)
{
    return (pin < 32U) && (0U != (g_pins.allowed & (UINT32_C(1) << pin)));
}

/** @brief Atomically validate a pin set within serialized lifecycle work. */
int c3pin_reserve (uint32_t mask, const void * p_owner)
{
    return pinpool_claim(&g_pins, mask, p_owner);
}

/** @brief Preserve another module's claims on an invalid release. */
int c3pin_release (uint32_t mask, const void * p_owner)
{
    return pinpool_release(&g_pins, mask, p_owner);
}

/** @brief Set the initial latch before enabling a claimed push-pull output. */
int c3pin_open (c3pin_output_t * p_output, uint8_t pin, bool b_initial)
{
    if ((NULL == p_output) || (p_output->b_open) || (!c3pin_valid(pin)) ||
        (0 != c3pin_reserve(UINT32_C(1) << pin, p_output)))
    {
        return -1;
    }
    gpio_dev_t * p_gpio = GPIO_LL_GET_HW(0);
    gpio_ll_output_disable(p_gpio, pin);
    gpio_ll_set_level(p_gpio, pin, b_initial);
    gpio_ll_func_sel(p_gpio, pin, PIN_FUNC_GPIO);
    gpio_ll_od_disable(p_gpio, pin);
    gpio_ll_pullup_dis(p_gpio, pin);
    gpio_ll_pulldown_dis(p_gpio, pin);
    esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
    gpio_ll_output_enable(p_gpio, pin);
    *p_output = (c3pin_output_t){.pin = pin, .b_open = true};
    return 0;
}

/** @brief Write only through the original retained output context. */
int c3pin_set (void * p_context, bool b_high)
{
    c3pin_output_t * p_output = p_context;
    if ((NULL == p_output) || (!p_output->b_open) ||
        (!c3pin_valid(p_output->pin)) ||
        (p_output != g_pins.p_owners[p_output->pin]))
    {
        return -1;
    }
    gpio_ll_set_level(GPIO_LL_GET_HW(0), p_output->pin, b_high);
    return 0;
}

/** @brief Disable the output before making its pin available again. */
int c3pin_close (c3pin_output_t * p_output)
{
    if ((NULL == p_output) || (!p_output->b_open) ||
        (!c3pin_valid(p_output->pin)) ||
        (p_output != g_pins.p_owners[p_output->pin]))
    {
        return -1;
    }
    gpio_ll_output_disable(GPIO_LL_GET_HW(0), p_output->pin);
    const int result = c3pin_release(UINT32_C(1) << p_output->pin, p_output);
    if (0 == result)
    {
        *p_output = (c3pin_output_t){0};
    }
    return result;
}

/** @brief Claim a digital input with no internal pulls or output drive. */
int c3pin_input_open (c3pin_input_t * p_input, uint8_t pin)
{
    if ((NULL == p_input) || (p_input->b_open) || (!c3pin_valid(pin)) ||
        (0 != c3pin_reserve(UINT32_C(1) << pin, p_input)))
    {
        return -1;
    }
    gpio_dev_t * p_gpio = GPIO_LL_GET_HW(0);
    gpio_ll_output_disable(p_gpio, pin);
    gpio_ll_func_sel(p_gpio, pin, PIN_FUNC_GPIO);
    gpio_ll_pullup_dis(p_gpio, pin);
    gpio_ll_pulldown_dis(p_gpio, pin);
    gpio_ll_input_enable(p_gpio, pin);
    *p_input = (c3pin_input_t){.pin = pin, .b_open = true};
    return 0;
}

/** @brief Return a level only through the context that owns this pin. */
int c3pin_get (void * p_context, bool * pb_high)
{
    c3pin_input_t * p_input = p_context;
    if (NULL != pb_high)
    {
        *pb_high = false;
    }
    if ((NULL == p_input) || (NULL == pb_high) || (!p_input->b_open) ||
        (!c3pin_valid(p_input->pin)) ||
        (p_input != g_pins.p_owners[p_input->pin]))
    {
        return -1;
    }
    *pb_high = (0U != gpio_ll_get_level(GPIO_LL_GET_HW(0), p_input->pin));
    return 0;
}

/** @brief Disable the input before releasing its retained claim. */
int c3pin_input_close (c3pin_input_t * p_input)
{
    if ((NULL == p_input) || (!p_input->b_open) ||
        (!c3pin_valid(p_input->pin)) ||
        (p_input != g_pins.p_owners[p_input->pin]))
    {
        return -1;
    }
    gpio_ll_input_disable(GPIO_LL_GET_HW(0), p_input->pin);
    const int result = c3pin_release(UINT32_C(1) << p_input->pin, p_input);
    if (0 == result)
    {
        *p_input = (c3pin_input_t){0};
    }
    return result;
}

/* End of file. */

