/**
 * @file ssd1306.c
 * @brief Adapt Member 2's command transport to retained instances and
 * deadlines.
 */
#include "ssd1306.h"
#include <string.h>
static int line_set (ssd1306_t * p_display,
                     digiout_t   line,
                     bool        b_high,
                     uint64_t    deadline_ms);
static int send (ssd1306_t *     p_display,
                 bool            b_data,
                 const uint8_t * p_bytes,
                 size_t          size,
                 uint64_t        deadline_ms);
static int reset_panel (ssd1306_t * p_display, uint64_t deadline_ms);
static int initialize (ssd1306_t * p_display, uint64_t deadline_ms);
static int render (ssd1306_t * p_display, uint64_t deadline_ms);
static size_t frame_size (const ssd1306_t * p_display);

/** @brief Validate everything and claim the device before pulsing reset. */
int ssd1306_open (ssd1306_t *              p_display,
                  spibus_device_t *        p_spi,
                  digiout_t                data_line,
                  digiout_t                reset,
                  const ssd1306_config_t * p_config)
{
    if ((NULL == p_display) || (NULL == p_spi) || (NULL == p_config) ||
        (NULL == data_line.p_set) || (NULL == data_line.p_context) ||
        (NULL == reset.p_set) || (NULL == reset.p_context) ||
        ((data_line.p_set == reset.p_set) &&
         (data_line.p_context == reset.p_context)) ||
        ((32U != p_config->height) && (64U != p_config->height)) ||
        (p_config->timeout_ms < 5U) || (p_config->timeout_ms > 100U) ||
        (p_config->setup_timeout_ms < 20U) ||
        (p_config->setup_timeout_ms > 100U) || (0U != p_spi->config.mode) ||
        (p_spi->config.clock_hz > 8000000U))
    {
        return SPIBUS_INVALID;
    }
    if (p_display->b_claimed)
    {
        return SPIBUS_STATE;
    }
    const int claimed = spibus_claim(p_spi, p_display);
    if (0 != claimed)
    {
        return claimed;
    }
    *p_display        = (ssd1306_t){.p_spi     = p_spi,
                                    .data_line = data_line,
                                    .reset     = reset,
                                    .config    = *p_config,
                                    .b_claimed = true,
                                    .b_dirty   = true};
    uint64_t deadline = 0U;
    int      result =
        spibus_deadline(p_spi->p_bus, p_config->setup_timeout_ms, &deadline);
    if (0 == result)
    {
        result = reset_panel(p_display, deadline);
    }
    if (0 == result)
    {
        result = initialize(p_display, deadline);
    }
    p_display->b_ready = (0 == result);
    /* Any failed setup retains its claim and context for explicit close. */
    return result;
}

/** @brief Copy a full frame; caller storage is never retained. */
int ssd1306_write (ssd1306_t * p_display, const void * p_frame, size_t size)
{
    if ((NULL == p_display) || (!p_display->b_ready) || (NULL == p_frame) ||
        (frame_size(p_display) != size))
    {
        return SPIBUS_INVALID;
    }
    memmove(p_display->frame, p_frame, size);
    p_display->b_dirty = true;
    return 0;
}

/** @brief Blank local pixels without performing transport work. */
int ssd1306_clear (ssd1306_t * p_display)
{
    if ((NULL == p_display) || (!p_display->b_ready))
    {
        return SPIBUS_STATE;
    }
    memset(p_display->frame, 0, sizeof(p_display->frame));
    p_display->b_dirty = true;
    return 0;
}

/** @brief Validate coordinates before updating a page-major pixel. */
int ssd1306_pixel (ssd1306_t * p_display,
                   uint8_t     column,
                   uint8_t     row,
                   bool        b_on)
{
    if ((NULL == p_display) || (!p_display->b_ready) || (column >= 128U) ||
        (row >= p_display->config.height))
    {
        return SPIBUS_INVALID;
    }
    const size_t index = ((size_t)row / 8U) * 128U + column;
    /* row modulo eight limits the shifted bit to the uint8_t pixel byte. */
    const uint8_t mask = (uint8_t)(1U << (row % 8U));
    if (b_on)
    {
        p_display->frame[index] |= mask;
    }
    else
    {
        p_display->frame[index] &= (uint8_t)(~mask);
    }
    p_display->b_dirty = true;
    return 0;
}

/** @brief Retry the complete window/frame after any partial transport failure.
 */
int ssd1306_flush (ssd1306_t * p_display)
{
    if ((NULL == p_display) || (!p_display->b_ready))
    {
        return SPIBUS_STATE;
    }
    if (!p_display->b_dirty)
    {
        return 0;
    }
    uint64_t deadline = 0U;
    int      result   = spibus_deadline(
        p_display->p_spi->p_bus, p_display->config.timeout_ms, &deadline);
    if (0 == result)
    {
        result = render(p_display, deadline);
    }
    return result;
}

/** @brief Retain ownership until reset is released and display-off completes.
 */
int ssd1306_close (ssd1306_t * p_display)
{
    if (NULL == p_display)
    {
        return SPIBUS_INVALID;
    }
    if (!p_display->b_claimed)
    {
        return 0;
    }
    uint64_t deadline = 0U;
    int      result   = spibus_deadline(
        p_display->p_spi->p_bus, p_display->config.timeout_ms, &deadline);
    if (0 == result)
    {
        result = line_set(p_display, p_display->reset, true, deadline);
    }
    const uint8_t command = 0xAEU;
    if (0 == result)
    {
        result = send(p_display, false, &command, 1U, deadline);
    }
    if (0 == result)
    {
        result = spibus_release(p_display->p_spi, p_display);
    }
    if (0 == result)
    {
        *p_display = (ssd1306_t){0};
    }
    return result;
}

/** @brief Check time before and after the required bounded GPIO callback. */
static int line_set (ssd1306_t * p_display,
                     digiout_t   line,
                     bool        b_high,
                     uint64_t    deadline_ms)
{
    uint64_t now = 0U;
    if (0 != spibus_now(p_display->p_spi->p_bus, &now))
    {
        return SPIBUS_IO;
    }
    if (now >= deadline_ms)
    {
        return SPIBUS_TIMEOUT;
    }
    if (0 != line.p_set(line.p_context, b_high))
    {
        return SPIBUS_IO;
    }
    if (0 != spibus_now(p_display->p_spi->p_bus, &now))
    {
        return SPIBUS_IO;
    }
    return (now >= deadline_ms) ? SPIBUS_TIMEOUT : 0;
}

/** @brief Select the exclusive D/C output before one atomic SPI transfer. */
static int send (ssd1306_t *     p_display,
                 bool            b_data,
                 const uint8_t * p_bytes,
                 size_t          size,
                 uint64_t        deadline_ms)
{
    const int result =
        line_set(p_display, p_display->data_line, b_data, deadline_ms);
    if (0 != result)
    {
        return result;
    }
    const spibus_request_t request = {
        .p_write = p_bytes, .size = size, .deadline_ms = deadline_ms};
    return spibus_transfer(p_display->p_spi, &request);
}

/** @brief Perform the retained high/low/high reset with mandatory real waits.
 */
static int reset_panel (ssd1306_t * p_display, uint64_t deadline_ms)
{
    int result = line_set(p_display, p_display->reset, true, deadline_ms);
    if (0 == result)
    {
        result = spibus_delay(p_display->p_spi->p_bus, 1U, deadline_ms);
    }
    if (0 == result)
    {
        result = line_set(p_display, p_display->reset, false, deadline_ms);
    }
    if (0 == result)
    {
        result = spibus_delay(p_display->p_spi->p_bus, 10U, deadline_ms);
    }
    if (0 == result)
    {
        result = line_set(p_display, p_display->reset, true, deadline_ms);
    }
    if (0 == result)
    {
        result = spibus_delay(p_display->p_spi->p_bus, 1U, deadline_ms);
    }
    return result;
}

/** @brief Keep the panel off until its initial blank frame has been sent. */
static int initialize (ssd1306_t * p_display, uint64_t deadline_ms)
{
    const bool b_tall = (64U == p_display->config.height);
    /* Validated height is 32 or 64, so height-1 fits the multiplex byte. */
    const uint8_t commands[] = {0xAEU,
                                0xD5U,
                                0x80U,
                                0xA8U,
                                (uint8_t)(p_display->config.height - 1U),
                                0xD3U,
                                0x00U,
                                0x40U,
                                0x8DU,
                                p_display->config.b_charge_pump ? 0x14U : 0x10U,
                                0x20U,
                                0x00U,
                                p_display->config.b_remap ? 0xA1U : 0xA0U,
                                p_display->config.b_remap ? 0xC8U : 0xC0U,
                                0xDAU,
                                b_tall ? 0x12U : 0x02U,
                                0x81U,
                                p_display->config.contrast,
                                0xD9U,
                                p_display->config.b_charge_pump ? 0xF1U : 0x22U,
                                0xDBU,
                                0x20U,
                                0xA4U,
                                0xA6U,
                                0x2EU};
    int           result =
        send(p_display, false, commands, sizeof(commands), deadline_ms);
    if (0 == result)
    {
        result = render(p_display, deadline_ms);
    }
    const uint8_t power_on = 0xAFU;
    if (0 == result)
    {
        result = send(p_display, false, &power_on, 1U, deadline_ms);
    }
    return result;
}

/** @brief Restore the address window on every refresh, including retries.
 */
static int render (ssd1306_t * p_display, uint64_t deadline_ms)
{
    /* Validated height gives four or eight pages. */
    const uint8_t window[] = {0x21U,
                              0U,
                              127U,
                              0x22U,
                              0U,
                              (uint8_t)((p_display->config.height / 8U) - 1U)};
    int result = send(p_display, false, window, sizeof(window), deadline_ms);
    if (0 == result)
    {
        result = send(p_display,
                      true,
                      p_display->frame,
                      frame_size(p_display),
                      deadline_ms);
    }
    if (0 == result)
    {
        p_display->b_dirty = false;
    }
    return result;
}

/** @brief Return the active portion of the fixed maximum framebuffer. */
static size_t frame_size (const ssd1306_t * p_display)
{
    return ((size_t)p_display->config.height / 8U) * 128U;
}

/* End of file. */

