/**
 * @file c3bind.c
 * @brief Select compiled C3 backends without triggering physical I/O.
 */
#include "c3bind.h"
#include "c3i2c.h"
#include "c3uart.h"
#include "c3pulse.h"
#include "c3spi.h"
#include "c3pin.h"
#include "c3pwm.h"
static c3pin_output_t g_lines[8] = {0};
static int open_spi (uint8_t sclk, uint8_t mosi, spibus_t ** pp_bus);
static int
open_line (uint8_t slot, uint8_t pin, bool b_initial, digiout_t * p_output);
static int close_line (digiout_t * p_output);
static const cfgbind_io_t g_operations = {.p_i2c_open   = c3i2c_open,
                                          .p_i2c_close  = c3i2c_close,
                                          .p_uart       = c3uart_port,
                                          .p_pulse      = c3pulse_port,
                                          .p_pwm        = c3pwm_port,
                                          .p_spi_open   = open_spi,
                                          .p_spi_close  = c3spi_close,
                                          .p_line_open  = open_line,
                                          .p_line_close = close_line};

/** @brief Borrow callbacks; opening hardware remains an activation operation.
 */
const cfgbind_io_t * c3bind_operations (void)
{
    return &g_operations;
}

/** @brief Resolve the admitted write-only SPI2 bus to the existing backend. */
static int open_spi (uint8_t sclk, uint8_t mosi, spibus_t ** pp_bus)
{
    const c3spi_config_t config = {.host      = 2U,
                                   .sclk_gpio = sclk,
                                   .mosi_gpio = mosi,
                                   .miso_gpio = UTHOME_SPI_NO_PIN};
    return c3spi_open(&config, pp_bus);
}

/** @brief Borrow one stable output context for each display control line. */
static int
open_line (uint8_t slot, uint8_t pin, bool b_initial, digiout_t * p_output)
{
    if ((slot >= 8U) || (NULL == p_output) || (NULL != p_output->p_set))
    {
        return -1;
    }
    const int result = c3pin_open(&g_lines[slot], pin, b_initial);
    if (0 == result)
    {
        *p_output =
            (digiout_t){.p_set = c3pin_set, .p_context = &g_lines[slot]};
    }
    return result;
}

/** @brief Preserve the borrowed handle when the underlying release fails. */
static int close_line (digiout_t * p_output)
{
    if ((NULL == p_output) || (c3pin_set != p_output->p_set))
    {
        return -1;
    }
    for (size_t index = 0U; index < 8U; ++index)
    {
        if (&g_lines[index] == p_output->p_context)
        {
            const int result = c3pin_close(&g_lines[index]);
            if (0 == result)
            {
                *p_output = (digiout_t){0};
            }
            return result;
        }
    }
    return -1;
}

/* End of file. */

