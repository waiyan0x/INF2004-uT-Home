/**
 * @file ssd1306.h
 * @brief Own one SSD1306 framebuffer while borrowing SPI and control outputs.
 */
#ifndef UTHOME_SSD1306_H
#define UTHOME_SSD1306_H
#include "spibus.h"
#include "digiout.h"
#define UTHOME_SSD1306_BYTES 1024U

typedef struct
{
    uint32_t timeout_ms;
    uint32_t setup_timeout_ms;
    uint8_t  height;
    uint8_t  contrast;
    bool     b_remap;
    bool     b_charge_pump;
} ssd1306_config_t;

typedef struct
{
    spibus_device_t * p_spi;
    digiout_t         data_line;
    digiout_t         reset;
    ssd1306_config_t  config;
    uint8_t           frame[UTHOME_SSD1306_BYTES];
    bool              b_claimed;
    bool              b_ready;
    bool              b_dirty;
} ssd1306_t;

/* Zero each context; one stopped-owner/worker at a time accesses a display.
 * SPI and exclusive D/C/reset outputs remain borrowed until close succeeds.
 * Width is 128, height 32/64; page-major bytes, bit 0 at top of each 8-row
 * page. SPI mode 0, MSB-first, active-low CS. Reset/delay callbacks are
 * mandatory. Open/flush/close each share one deadline. SPI success proves
 * transport completion only: SSD1306 serial mode provides no
 * presence/acknowledgement. */
int ssd1306_open (ssd1306_t *              p_display,
                  spibus_device_t *        p_spi,
                  digiout_t                data_line,
                  digiout_t                reset,
                  const ssd1306_config_t * p_config);
int ssd1306_write (ssd1306_t * p_display, const void * p_frame, size_t size);
int ssd1306_clear (ssd1306_t * p_display);
int ssd1306_pixel (ssd1306_t * p_display,
                   uint8_t     column,
                   uint8_t     row,
                   bool        b_on);
int ssd1306_flush (ssd1306_t * p_display);
int ssd1306_close (ssd1306_t * p_display);
#endif
/* End of file. */

