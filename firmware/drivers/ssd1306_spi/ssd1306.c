/*
 * SSD1306 driver: framebuffer and command transport over the SPI HAL
 * (firmware/hal/include/spi.h). The D/C line selects command (low) or data
 * (high) for each transfer; the runtime supplies it and the reset line as
 * callbacks bound to GPIO outputs.
 *
 * The framebuffer uses the controller's own layout: one byte per column per
 * 8-row page, bit 0 at the top. Horizontal addressing mode lets render() send
 * the whole buffer in one transfer after setting the column and page window.
 */

#include "ssd1306.h"

#include <stddef.h>
#include <string.h>

#define SSD1306_MAX_INSTANCES 1  /* The prototype has one OLED */
#define SSD1306_MAX_HEIGHT    64u
#define SSD1306_FB_BYTES      (SSD1306_WIDTH_PX * SSD1306_MAX_HEIGHT / 8u)

/* Controller commands used outside the init sequence (datasheet section 9). */
#define CMD_DISPLAY_OFF   0xAE
#define CMD_DISPLAY_ON    0xAF
#define CMD_COLUMN_WINDOW 0x21
#define CMD_PAGE_WINDOW   0x22

/* Fault view layout. */
#define FAULT_RULE_Y      9u
#define FAULT_MESSAGE_Y   12u

struct ssd1306 {
    bool              in_use;
    hal_spi_device_t *spi;
    uint32_t          timeout_ms;
    ssd1306_line_t    dc;
    ssd1306_line_t    reset;
    void            (*delay_us)(uint32_t us);
    uint8_t           width;
    uint8_t           height;
    uint8_t           fb[SSD1306_FB_BYTES];
};

static struct ssd1306 g_instances[SSD1306_MAX_INSTANCES];

/*
 * Printable ASCII 0x20..0x7E, 5 columns per glyph, bit 0 = top row. The
 * classic public-domain 5x7 LCD font.
 */
static const uint8_t g_font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* space */ {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* dquote */ {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */ {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */ {0x00,0x05,0x03,0x00,0x00}, /* squote */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */ {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* * */ {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */ {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */ {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */ {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */ {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */ {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */ {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */ {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */ {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x08,0x14,0x22,0x41,0x00}, /* < */ {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x00,0x41,0x22,0x14,0x08}, /* > */ {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */ {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */ {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */ {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */ {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */ {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */ {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */ {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */ {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */ {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */ {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */ {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */ {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */ {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */ {0x00,0x7F,0x41,0x41,0x00}, /* [ */
    {0x02,0x04,0x08,0x10,0x20}, /* backslash */ {0x00,0x41,0x41,0x7F,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04}, /* ^ */ {0x40,0x40,0x40,0x40,0x40}, /* _ */
    {0x00,0x01,0x02,0x04,0x00}, /* backtick */ {0x20,0x54,0x54,0x54,0x78}, /* a */
    {0x7F,0x48,0x44,0x44,0x38}, /* b */ {0x38,0x44,0x44,0x44,0x20}, /* c */
    {0x38,0x44,0x44,0x48,0x7F}, /* d */ {0x38,0x54,0x54,0x54,0x18}, /* e */
    {0x08,0x7E,0x09,0x01,0x02}, /* f */ {0x0C,0x52,0x52,0x52,0x3E}, /* g */
    {0x7F,0x08,0x04,0x04,0x78}, /* h */ {0x00,0x44,0x7D,0x40,0x00}, /* i */
    {0x20,0x40,0x44,0x3D,0x00}, /* j */ {0x7F,0x10,0x28,0x44,0x00}, /* k */
    {0x00,0x41,0x7F,0x40,0x00}, /* l */ {0x7C,0x04,0x18,0x04,0x78}, /* m */
    {0x7C,0x08,0x04,0x04,0x78}, /* n */ {0x38,0x44,0x44,0x44,0x38}, /* o */
    {0x7C,0x14,0x14,0x14,0x08}, /* p */ {0x08,0x14,0x14,0x18,0x7C}, /* q */
    {0x7C,0x08,0x04,0x04,0x08}, /* r */ {0x48,0x54,0x54,0x54,0x20}, /* s */
    {0x04,0x3F,0x44,0x40,0x20}, /* t */ {0x3C,0x40,0x40,0x20,0x7C}, /* u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* v */ {0x3C,0x40,0x30,0x40,0x3C}, /* w */
    {0x44,0x28,0x10,0x28,0x44}, /* x */ {0x0C,0x50,0x50,0x50,0x3C}, /* y */
    {0x44,0x64,0x54,0x4C,0x44}, /* z */ {0x00,0x08,0x36,0x41,0x00}, /* { */
    {0x00,0x00,0x7F,0x00,0x00}, /* | */ {0x00,0x41,0x36,0x08,0x00}, /* } */
    {0x08,0x04,0x08,0x10,0x08}, /* ~ */
};

/* --- transport --- */

static void delay(const struct ssd1306 *dev, uint32_t us)
{
    if (dev->delay_us != NULL) {
        dev->delay_us(us);
    }
}

/* Select command or data on D/C, then send the bytes in one CS window. */
static hal_status_t send(struct ssd1306 *dev, bool data, const uint8_t *bytes, size_t len)
{
    hal_status_t status = dev->dc.set(dev->dc.ctx, data);
    if (status != HAL_OK) {
        return status;
    }
    return hal_spi_write(dev->spi, bytes, len, dev->timeout_ms);
}

static hal_status_t send_command(struct ssd1306 *dev, uint8_t command)
{
    return send(dev, false, &command, 1);
}

/* Reset pulse: high 1 ms, low 10 ms, then released high. */
static hal_status_t pulse_reset(struct ssd1306 *dev)
{
    hal_status_t status = dev->reset.set(dev->reset.ctx, true);
    if (status == HAL_OK) {
        delay(dev, 1000);
        status = dev->reset.set(dev->reset.ctx, false);
    }
    if (status == HAL_OK) {
        delay(dev, 10000);
        status = dev->reset.set(dev->reset.ctx, true);
    }
    return status;
}

/* --- framebuffer --- */

static void put_pixel(struct ssd1306 *dev, unsigned x, unsigned y, bool on)
{
    if (x >= dev->width || y >= dev->height) {
        return;
    }
    uint8_t *byte = &dev->fb[(y / 8u) * dev->width + x];
    uint8_t  mask = (uint8_t)(1u << (y % 8u));
    if (on) {
        *byte |= mask;
    } else {
        *byte &= (uint8_t)~mask;
    }
}

/* Draw one character cell, clearing the background. */
static void draw_glyph(struct ssd1306 *dev, unsigned x, unsigned y, char c)
{
    if (c < 0x20 || c > 0x7E) {
        c = '?';
    }
    const uint8_t *glyph = g_font5x7[c - 0x20];
    for (unsigned col = 0; col < SSD1306_CHAR_WIDTH; ++col) {
        uint8_t bits = (col < 5u) ? glyph[col] : 0x00;
        for (unsigned row = 0; row < SSD1306_LINE_HEIGHT; ++row) {
            put_pixel(dev, x + col, y + row, ((bits >> row) & 1u) != 0);
        }
    }
}

/*
 * Lay out text from (x0, y0). With wrap set, a word that would cross the right
 * edge moves to the next line (a word longer than a line is split), and a
 * space that falls at the edge is dropped rather than starting a line.
 */
static void draw_string(struct ssd1306 *dev, unsigned x0, unsigned y0,
                        const char *text, bool wrap)
{
    unsigned x = x0;
    unsigned y = y0;
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p == '\n') {
            x = x0;
            y += SSD1306_LINE_HEIGHT;
            continue;
        }
        if (wrap) {
            bool fits = x + SSD1306_CHAR_WIDTH <= dev->width;
            if (*p == ' ' && !fits) {
                continue;
            }
            if (*p != ' ' && x != x0 && (p == text || p[-1] == ' ')) {
                size_t word = strcspn(p, " \n");
                fits = x + word * SSD1306_CHAR_WIDTH <= dev->width;
            }
            if (!fits) {
                x = x0;
                y += SSD1306_LINE_HEIGHT;
            }
        }
        draw_glyph(dev, x, y, *p);
        x += SSD1306_CHAR_WIDTH;
    }
}

/* Validate a handle for the operations below. */
static hal_status_t check(const struct ssd1306 *dev)
{
    if (dev == NULL) {
        return HAL_ERR_INVALID_ARG;
    }
    return dev->in_use ? HAL_OK : HAL_ERR_NOT_INIT;
}

/* --- public API --- */

hal_status_t ssd1306_init(const ssd1306_config_t *config, ssd1306_t **out_dev)
{
    if (config == NULL || out_dev == NULL) {
        return HAL_ERR_INVALID_ARG;
    }
    *out_dev = NULL;
    if (config->spi == NULL || config->dc.set == NULL ||
        config->width_px != SSD1306_WIDTH_PX ||
        (config->height_px != 64u && config->height_px != 32u)) {
        return HAL_ERR_INVALID_ARG;
    }

    struct ssd1306 *inst = NULL;
    for (int i = 0; i < SSD1306_MAX_INSTANCES; ++i) {
        if (g_instances[i].in_use) {
            if (g_instances[i].spi == config->spi) {
                return HAL_ERR_INVALID_ARG; /* Display already initialised */
            }
        } else if (inst == NULL) {
            inst = &g_instances[i];
        }
    }
    if (inst == NULL) {
        return HAL_ERR_NO_RESOURCE;
    }

    memset(inst, 0, sizeof(*inst));
    inst->spi = config->spi;
    inst->timeout_ms = config->timeout_ms;
    inst->dc = config->dc;
    inst->reset = config->reset;
    inst->delay_us = config->delay_us;
    inst->width = config->width_px;
    inst->height = config->height_px;

    hal_status_t status = HAL_OK;
    if (inst->reset.set != NULL) {
        status = pulse_reset(inst);
    }

    /* Internal charge pump (3.3 V modules), horizontal addressing, and the
       usual 180-degree orientation so the header pins sit at the top. The
       panel stays off until its memory has been blanked. */
    const bool tall = (inst->height == 64u);
    const uint8_t init_sequence[] = {
        CMD_DISPLAY_OFF,
        0xD5, 0x80,                          /* Clock divide / oscillator */
        0xA8, (uint8_t)(inst->height - 1u),  /* Multiplex ratio */
        0xD3, 0x00,                          /* Display offset */
        0x40,                                /* Start line 0 */
        0x8D, 0x14,                          /* Charge pump on */
        0x20, 0x00,                          /* Horizontal addressing mode */
        0xA1,                                /* Segment remap */
        0xC8,                                /* COM scan direction remapped */
        0xDA, tall ? 0x12 : 0x02,            /* COM pins configuration */
        0x81, tall ? 0xCF : 0x8F,            /* Contrast */
        0xD9, 0xF1,                          /* Pre-charge period */
        0xDB, 0x40,                          /* VCOMH deselect level */
        0xA4,                                /* Display follows RAM */
        0xA6,                                /* Normal, not inverted */
        0x2E,                                /* Scrolling off */
    };
    if (status == HAL_OK) {
        status = send(inst, false, init_sequence, sizeof(init_sequence));
    }

    inst->in_use = true; /* Lets render() run on the new instance */
    if (status == HAL_OK) {
        status = ssd1306_render(inst);
    }
    if (status == HAL_OK) {
        status = send_command(inst, CMD_DISPLAY_ON);
    }
    if (status != HAL_OK) {
        memset(inst, 0, sizeof(*inst));
        return status;
    }

    *out_dev = inst;
    return HAL_OK;
}

hal_status_t ssd1306_clear(ssd1306_t *dev)
{
    hal_status_t status = check(dev);
    if (status == HAL_OK) {
        memset(dev->fb, 0, sizeof(dev->fb));
    }
    return status;
}

hal_status_t ssd1306_set_pixel(ssd1306_t *dev, uint8_t x, uint8_t y, bool on)
{
    hal_status_t status = check(dev);
    if (status == HAL_OK) {
        put_pixel(dev, x, y, on);
    }
    return status;
}

hal_status_t ssd1306_draw_text(ssd1306_t *dev, uint8_t x, uint8_t y, const char *text)
{
    hal_status_t status = check(dev);
    if (status != HAL_OK) {
        return status;
    }
    if (text == NULL) {
        return HAL_ERR_INVALID_ARG;
    }
    draw_string(dev, x, y, text, false);
    return HAL_OK;
}

hal_status_t ssd1306_render(ssd1306_t *dev)
{
    hal_status_t status = check(dev);
    if (status != HAL_OK) {
        return status;
    }
    const uint8_t pages = (uint8_t)(dev->height / 8u);
    const uint8_t window[] = {
        CMD_COLUMN_WINDOW, 0x00, (uint8_t)(dev->width - 1u),
        CMD_PAGE_WINDOW,   0x00, (uint8_t)(pages - 1u),
    };
    status = send(dev, false, window, sizeof(window));
    if (status == HAL_OK) {
        status = send(dev, true, dev->fb, (size_t)dev->width * pages);
    }
    return status;
}

hal_status_t ssd1306_show_fault(ssd1306_t *dev, const char *message)
{
    hal_status_t status = check(dev);
    if (status != HAL_OK) {
        return status;
    }
    memset(dev->fb, 0, sizeof(dev->fb));
    draw_string(dev, 0, 0, "FAULT", false);
    for (unsigned x = 0; x < dev->width; ++x) {
        put_pixel(dev, x, FAULT_RULE_Y, true);
    }
    if (message != NULL) {
        draw_string(dev, 0, FAULT_MESSAGE_Y, message, true);
    }
    return ssd1306_render(dev);
}

hal_status_t ssd1306_set_power(ssd1306_t *dev, bool on)
{
    hal_status_t status = check(dev);
    if (status != HAL_OK) {
        return status;
    }
    return send_command(dev, on ? CMD_DISPLAY_ON : CMD_DISPLAY_OFF);
}

hal_status_t ssd1306_deinit(ssd1306_t *dev)
{
    hal_status_t status = check(dev);
    if (status != HAL_OK) {
        return status;
    }
    status = send_command(dev, CMD_DISPLAY_OFF);
    memset(dev, 0, sizeof(*dev));
    return status;
}
