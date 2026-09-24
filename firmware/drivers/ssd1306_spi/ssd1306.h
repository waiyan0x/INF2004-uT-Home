#ifndef UTHOME_DRIVER_SSD1306_H
#define UTHOME_DRIVER_SSD1306_H

#include <stdbool.h>
#include <stdint.h>

#include "hal_status.h"
#include "spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SSD1306 OLED driver over SPI (4-wire: SCLK, MOSI, CS, plus D/C).
 *
 * A controller-specific pixel and command transport. It keeps a framebuffer,
 * offers drawing primitives (pixel, 5x7 text) and sends the frame on render.
 * What goes on screen, and when, belongs to the display service. The driver
 * creates no tasks, queues, or pins: the runtime supplies these fields from
 * the profile.
 *
 * The D/C and reset pins are ordinary GPIOs, so the driver takes them as
 * callbacks rather than depending on the GPIO contract directly. The runtime
 * binds each callback to a GPIO output.
 */

#define SSD1306_WIDTH_PX    128u
#define SSD1306_CHAR_WIDTH  6u  /* 5x7 glyph plus one column of spacing */
#define SSD1306_LINE_HEIGHT 8u  /* 7 glyph rows plus one row of spacing */

/* One output line driven by the runtime through the GPIO contract. */
typedef struct {
    hal_status_t (*set)(void *ctx, bool high);
    void          *ctx;  /* Passed back to set(), e.g. a GPIO handle */
} ssd1306_line_t;

typedef struct {
    hal_spi_device_t *spi;         /* Device already added to the bus: CS, mode 0 */
    uint32_t          timeout_ms;  /* Per-transfer timeout */
    uint8_t           width_px;    /* Must be 128 */
    uint8_t           height_px;   /* 64 (prototype) or 32 */
    ssd1306_line_t    dc;          /* Data/command select: low = command. Required */
    ssd1306_line_t    reset;       /* Active-low reset. set = NULL if not wired */

    /* Optional microsecond delay, supplied by the runtime/time HAL, used for
     * the reset pulse. NULL selects a no-op, which is correct for host tests
     * but must be wired to a real delay on the esp32s3 port. */
    void (*delay_us)(uint32_t us);
} ssd1306_config_t;

/* Opaque instance handle, backed by static storage in the driver. */
typedef struct ssd1306 ssd1306_t;

/*
 * Pulse reset (if wired), send the init sequence, blank the panel and switch
 * it on. On success returns HAL_OK and a handle in *out_dev; on failure returns
 * an error and sets *out_dev to NULL: HAL_ERR_INVALID_ARG for a missing spi or
 * dc.set, an unsupported size, or an spi device already driven by another
 * instance; otherwise the SPI or D/C status that failed.
 *
 * SPI here is write-only, so the panel cannot acknowledge anything: an
 * unplugged display is not detectable from this driver. Only failures of the
 * SPI transfer or the D/C line are reported.
 */
hal_status_t ssd1306_init(const ssd1306_config_t *config, ssd1306_t **out_dev);

/*
 * Framebuffer operations. These only change memory; nothing reaches the panel
 * until ssd1306_render(). Coordinates outside the panel are clipped.
 */
hal_status_t ssd1306_clear(ssd1306_t *dev);
hal_status_t ssd1306_set_pixel(ssd1306_t *dev, uint8_t x, uint8_t y, bool on);

/*
 * Draw text in the built-in 5x7 font with its top-left corner at (x, y). Each
 * character fills a SSD1306_CHAR_WIDTH x SSD1306_LINE_HEIGHT cell (background
 * cleared). '\n' starts a new line at x. Text past the right edge is clipped,
 * not wrapped; characters outside printable ASCII are drawn as '?'.
 */
hal_status_t ssd1306_draw_text(ssd1306_t *dev, uint8_t x, uint8_t y, const char *text);

/*
 * Send the whole framebuffer to the panel. On failure the framebuffer is kept,
 * so the next render retries the same frame.
 */
hal_status_t ssd1306_render(ssd1306_t *dev);

/*
 * Replace the screen with a fault view: "FAULT", a rule, then message wrapped
 * at word boundaries to the panel width. Renders immediately. message may be
 * NULL.
 */
hal_status_t ssd1306_show_fault(ssd1306_t *dev, const char *message);

/* Switch the panel on or off. The framebuffer and panel memory are kept. */
hal_status_t ssd1306_set_power(ssd1306_t *dev, bool on);

/*
 * Switch the panel off and release the instance. The instance is released
 * even if the display-off command fails; the returned status reports it.
 */
hal_status_t ssd1306_deinit(ssd1306_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* UTHOME_DRIVER_SSD1306_H */
