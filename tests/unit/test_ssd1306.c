/*
 * Host unit tests for the SSD1306 OLED driver: driver -> SPI HAL host_sim
 * fake, with no hardware. The SPI fake captures every byte sent; a fake GPIO
 * line records each D/C and reset change together with its position in that
 * byte stream, so the tests can check which bytes went out as commands and
 * which as data.
 *
 * These prove the command sequence, framebuffer layout, font, and error
 * handling. Whether the panel actually lights up is checked on hardware.
 */

#include "ssd1306.h"
#include "spi.h"
#include "host_sim.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("  FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            g_failures++;                                                \
        }                                                                \
    } while (0)

#define WINDOW_BYTES 6u     /* Column + page window sent before each frame */
#define FRAME_64     1024u  /* 128 x 64 / 8 */
#define LINE_LOG_MAX 64

/* --- fake GPIO line --- */

typedef struct {
    hal_spi_device_t *spi;  /* Read its capture length at each change */
    int               count;
    bool              level[LINE_LOG_MAX];
    size_t            at_byte[LINE_LOG_MAX];
    hal_status_t      fail_next;
} fake_line_t;

static hal_status_t fake_line_set(void *ctx, bool high)
{
    fake_line_t *line = (fake_line_t *)ctx;
    if (line->fail_next != HAL_OK) {
        hal_status_t forced = line->fail_next;
        line->fail_next = HAL_OK;
        return forced;
    }
    if (line->count < LINE_LOG_MAX) {
        const uint8_t *cap = NULL;
        size_t len = 0;
        hal_spi_sim_get_capture(line->spi, &cap, &len);
        line->level[line->count] = high;
        line->at_byte[line->count] = len;
        line->count++;
    }
    return HAL_OK;
}

/* D/C level in force when the byte at this index of the capture was sent. */
static bool dc_level_at(const fake_line_t *dc, size_t byte_index)
{
    bool level = false;
    for (int i = 0; i < dc->count; ++i) {
        if (dc->at_byte[i] <= byte_index) {
            level = dc->level[i];
        }
    }
    return level;
}

/* --- test rig: prototype spi2 with the OLED on CS 10 --- */

typedef struct {
    hal_spi_bus_t    *bus;
    hal_spi_device_t *spi;
    fake_line_t       dc;
    fake_line_t       reset;
} rig_t;

static void rig_setup(rig_t *rig)
{
    memset(rig, 0, sizeof(*rig));
    hal_spi_config_t bcfg = {
        .host = 2, .sclk_gpio = 12, .mosi_gpio = 11,
        .miso_gpio = HAL_GPIO_NONE, .clock_hz = 8000000
    };
    CHECK(hal_spi_init(&bcfg, &rig->bus) == HAL_OK);
    hal_spi_device_config_t dcfg = { .cs_gpio = 10, .mode = 0, .cs_active_low = 1, .clock_hz = 0 };
    CHECK(hal_spi_device_add(rig->bus, &dcfg, &rig->spi) == HAL_OK);
    rig->dc.spi = rig->spi;
    rig->reset.spi = rig->spi;
}

static ssd1306_config_t rig_config(rig_t *rig)
{
    ssd1306_config_t c = {
        .spi = rig->spi, .timeout_ms = 100, .width_px = 128, .height_px = 64,
        .dc = { fake_line_set, &rig->dc },
        .reset = { fake_line_set, &rig->reset },
        .delay_us = NULL,
    };
    return c;
}

static const uint8_t *capture(rig_t *rig, size_t *len)
{
    const uint8_t *cap = NULL;
    *len = 0;
    hal_spi_sim_get_capture(rig->spi, &cap, len);
    return cap;
}

/* Render and return the 128x64 frame the driver sent, or NULL. */
static const uint8_t *rendered_frame(rig_t *rig, ssd1306_t *dev)
{
    hal_spi_sim_clear_capture(rig->spi);
    CHECK(ssd1306_render(dev) == HAL_OK);
    size_t len = 0;
    const uint8_t *cap = capture(rig, &len);
    CHECK(len == WINDOW_BYTES + FRAME_64);
    return (len == WINDOW_BYTES + FRAME_64) ? cap + WINDOW_BYTES : NULL;
}

static ssd1306_t *init_display(rig_t *rig)
{
    ssd1306_config_t c = rig_config(rig);
    ssd1306_t *dev = NULL;
    CHECK(ssd1306_init(&c, &dev) == HAL_OK);
    CHECK(dev != NULL);
    return dev;
}

static void release(ssd1306_t *dev)
{
    if (dev != NULL) {
        CHECK(ssd1306_deinit(dev) == HAL_OK);
    }
}

/* --- tests --- */

static void test_init_rejects_bad_arguments(void)
{
    rig_t rig;
    rig_setup(&rig);
    ssd1306_t *dev = NULL;
    ssd1306_config_t c = rig_config(&rig);

    CHECK(ssd1306_init(NULL, &dev) == HAL_ERR_INVALID_ARG);
    CHECK(ssd1306_init(&c, NULL) == HAL_ERR_INVALID_ARG);

    c.spi = NULL;
    CHECK(ssd1306_init(&c, &dev) == HAL_ERR_INVALID_ARG);
    c = rig_config(&rig);
    c.dc.set = NULL;
    CHECK(ssd1306_init(&c, &dev) == HAL_ERR_INVALID_ARG);
    c = rig_config(&rig);
    c.width_px = 96;
    CHECK(ssd1306_init(&c, &dev) == HAL_ERR_INVALID_ARG);
    c = rig_config(&rig);
    c.height_px = 48;
    CHECK(ssd1306_init(&c, &dev) == HAL_ERR_INVALID_ARG);
    CHECK(dev == NULL);

    /* Nothing reached the bus. */
    size_t len = 0;
    capture(&rig, &len);
    CHECK(len == 0);
}

static void test_init_sends_expected_sequence(void)
{
    rig_t rig;
    rig_setup(&rig);
    ssd1306_t *dev = init_display(&rig);

    const uint8_t init_cmds[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00,
        0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
        0x2E,
    };
    const uint8_t window[] = { 0x21, 0x00, 0x7F, 0x22, 0x00, 0x07 };
    const size_t n_init = sizeof(init_cmds);
    const size_t data_at = n_init + sizeof(window);

    /* Init commands, window, a blank frame, then display on. */
    size_t len = 0;
    const uint8_t *cap = capture(&rig, &len);
    CHECK(len == data_at + FRAME_64 + 1);
    if (len == data_at + FRAME_64 + 1) {
        CHECK(memcmp(cap, init_cmds, n_init) == 0);
        CHECK(memcmp(cap + n_init, window, sizeof(window)) == 0);
        size_t lit = 0;
        for (size_t i = 0; i < FRAME_64; ++i) {
            lit += (cap[data_at + i] != 0);
        }
        CHECK(lit == 0);
        CHECK(cap[len - 1] == 0xAF);

        /* Commands with D/C low, frame bytes with D/C high. */
        CHECK(!dc_level_at(&rig.dc, 0));
        CHECK(!dc_level_at(&rig.dc, data_at - 1));
        CHECK(dc_level_at(&rig.dc, data_at));
        CHECK(dc_level_at(&rig.dc, data_at + FRAME_64 - 1));
        CHECK(!dc_level_at(&rig.dc, len - 1));
    }

    /* Reset pulsed high, low, high before any byte was sent. */
    CHECK(rig.reset.count == 3);
    if (rig.reset.count == 3) {
        CHECK(rig.reset.level[0] && !rig.reset.level[1] && rig.reset.level[2]);
        CHECK(rig.reset.at_byte[2] == 0);
    }

    release(dev);
}

static void test_init_128x32_without_reset(void)
{
    rig_t rig;
    rig_setup(&rig);
    ssd1306_config_t c = rig_config(&rig);
    c.height_px = 32;
    c.reset.set = NULL; /* RES tied high on the module */
    ssd1306_t *dev = NULL;
    CHECK(ssd1306_init(&c, &dev) == HAL_OK);

    size_t len = 0;
    const uint8_t *cap = capture(&rig, &len);
    CHECK(len == 25 + WINDOW_BYTES + 512 + 1);
    if (len == 25 + WINDOW_BYTES + 512 + 1) {
        CHECK(cap[4] == 0x1F);  /* Multiplex ratio: 32 rows */
        CHECK(cap[15] == 0x02); /* COM pins for 32 rows */
        CHECK(cap[17] == 0x8F); /* Contrast for 32 rows */
        CHECK(cap[30] == 0x03); /* Last page of 4 */
    }
    CHECK(rig.reset.count == 0);

    release(dev);
}

static void test_init_failure_releases_instance(void)
{
    rig_t rig;
    rig_setup(&rig);
    ssd1306_config_t c = rig_config(&rig);
    ssd1306_t *dev = NULL;

    hal_spi_sim_fail_next(rig.spi, HAL_ERR_TIMEOUT);
    CHECK(ssd1306_init(&c, &dev) == HAL_ERR_TIMEOUT);
    CHECK(dev == NULL);

    /* The only instance slot is free again. */
    CHECK(ssd1306_init(&c, &dev) == HAL_OK);
    release(dev);
}

static void test_init_rejects_duplicate_display(void)
{
    rig_t rig;
    rig_setup(&rig);
    ssd1306_t *dev = init_display(&rig);

    ssd1306_config_t c = rig_config(&rig);
    ssd1306_t *again = NULL;
    CHECK(ssd1306_init(&c, &again) == HAL_ERR_INVALID_ARG);
    CHECK(again == NULL);

    release(dev);
}

static void test_pixels_map_to_controller_layout(void)
{
    rig_t rig;
    rig_setup(&rig);
    ssd1306_t *dev = init_display(&rig);

    CHECK(ssd1306_set_pixel(dev, 0, 0, true) == HAL_OK);
    CHECK(ssd1306_set_pixel(dev, 127, 63, true) == HAL_OK);
    CHECK(ssd1306_set_pixel(dev, 5, 9, true) == HAL_OK);
    CHECK(ssd1306_set_pixel(dev, 128, 0, true) == HAL_OK); /* Clipped */
    CHECK(ssd1306_set_pixel(dev, 0, 64, true) == HAL_OK);  /* Clipped */

    const uint8_t *frame = rendered_frame(&rig, dev);
    if (frame != NULL) {
        CHECK(frame[0] == 0x01);             /* Page 0, column 0, bit 0 */
        CHECK(frame[7 * 128 + 127] == 0x80); /* Page 7, column 127, bit 7 */
        CHECK(frame[1 * 128 + 5] == 0x02);   /* Page 1, column 5, bit 1 */
        size_t lit = 0;
        for (size_t i = 0; i < FRAME_64; ++i) {
            lit += (frame[i] != 0);
        }
        CHECK(lit == 3);
    }

    CHECK(ssd1306_set_pixel(dev, 5, 9, false) == HAL_OK);
    frame = rendered_frame(&rig, dev);
    if (frame != NULL) {
        CHECK(frame[1 * 128 + 5] == 0x00);
    }

    CHECK(ssd1306_clear(dev) == HAL_OK);
    frame = rendered_frame(&rig, dev);
    if (frame != NULL) {
        CHECK(frame[0] == 0x00 && frame[7 * 128 + 127] == 0x00);
    }

    release(dev);
}

static void test_text_uses_font_and_clips(void)
{
    rig_t rig;
    rig_setup(&rig);
    ssd1306_t *dev = init_display(&rig);

    CHECK(ssd1306_draw_text(dev, 0, 0, "A") == HAL_OK);
    CHECK(ssd1306_draw_text(dev, 0, 8, "F") == HAL_OK);
    CHECK(ssd1306_draw_text(dev, 12, 0, "\x01") == HAL_OK); /* Unprintable */
    CHECK(ssd1306_draw_text(dev, 60, 16, "A\nB") == HAL_OK);
    CHECK(ssd1306_draw_text(dev, 124, 0, "XYZ") == HAL_OK); /* Runs off the edge */
    CHECK(ssd1306_set_pixel(dev, 101, 41, true) == HAL_OK);
    CHECK(ssd1306_draw_text(dev, 100, 40, " ") == HAL_OK);  /* Cell is cleared */
    CHECK(ssd1306_draw_text(dev, 0, 0, NULL) == HAL_ERR_INVALID_ARG);

    const uint8_t *frame = rendered_frame(&rig, dev);
    if (frame != NULL) {
        const uint8_t a[] = { 0x7E, 0x11, 0x11, 0x11, 0x7E, 0x00 };
        const uint8_t f[] = { 0x7F, 0x09, 0x09, 0x09, 0x01, 0x00 };
        const uint8_t q[] = { 0x02, 0x01, 0x51, 0x09, 0x06, 0x00 };
        CHECK(memcmp(&frame[0], a, sizeof(a)) == 0);
        CHECK(memcmp(&frame[128], f, sizeof(f)) == 0);
        CHECK(memcmp(&frame[12], q, sizeof(q)) == 0);

        /* '\n' returns to the starting column one line down. */
        CHECK(frame[2 * 128 + 60] == 0x7E); /* 'A' */
        CHECK(frame[3 * 128 + 60] == 0x7F); /* 'B' */

        /* 'X' is cut at column 127 and nothing wraps onto the next line. */
        CHECK(frame[124] == 0x63 && frame[127] == 0x14);
        CHECK(frame[128] == 0x7F); /* 'F' untouched */

        CHECK(frame[5 * 128 + 101] == 0x00);
    }

    release(dev);
}

static void test_render_reports_spi_fault_and_keeps_frame(void)
{
    rig_t rig;
    rig_setup(&rig);
    ssd1306_t *dev = init_display(&rig);
    CHECK(ssd1306_draw_text(dev, 0, 0, "A") == HAL_OK);

    /* Scenario "fault": the transfer times out; the caller sees it as-is. */
    hal_spi_sim_fail_next(rig.spi, HAL_ERR_TIMEOUT);
    CHECK(ssd1306_render(dev) == HAL_ERR_TIMEOUT);

    /* The next render sends the same frame. */
    const uint8_t *frame = rendered_frame(&rig, dev);
    if (frame != NULL) {
        CHECK(frame[0] == 0x7E);
    }

    release(dev);
}

static void test_render_reports_dc_fault(void)
{
    rig_t rig;
    rig_setup(&rig);
    ssd1306_t *dev = init_display(&rig);

    hal_spi_sim_clear_capture(rig.spi);
    rig.dc.fail_next = HAL_ERR_BUS;
    CHECK(ssd1306_render(dev) == HAL_ERR_BUS);

    /* Without a valid D/C level nothing is sent. */
    size_t len = 0;
    capture(&rig, &len);
    CHECK(len == 0);

    release(dev);
}

static void test_fault_screen(void)
{
    rig_t rig;
    rig_setup(&rig);
    ssd1306_t *dev = init_display(&rig);
    CHECK(ssd1306_set_pixel(dev, 64, 60, true) == HAL_OK); /* Old content */

    hal_spi_sim_clear_capture(rig.spi);
    CHECK(ssd1306_show_fault(dev, "ABCDEFGHIJKLMNOPQRSTUVWXYZ") == HAL_OK);

    /* show_fault renders by itself: window plus one frame. */
    size_t len = 0;
    const uint8_t *cap = capture(&rig, &len);
    CHECK(len == WINDOW_BYTES + FRAME_64);
    if (len == WINDOW_BYTES + FRAME_64) {
        const uint8_t *frame = cap + WINDOW_BYTES;
        const uint8_t f[] = { 0x7F, 0x09, 0x09, 0x09, 0x01 };
        CHECK(memcmp(&frame[0], f, sizeof(f)) == 0); /* "FAULT" header */

        /* Rule across the full width at row 9 (page 1, bit 1). */
        size_t ruled = 0;
        for (size_t x = 0; x < 128; ++x) {
            ruled += ((frame[128 + x] & 0x02) != 0);
        }
        CHECK(ruled == 128);

        /* Message 'A' from row 12: column 0 has rows 13-18 lit. */
        CHECK(frame[128] == 0xE2);     /* Rows 13-15 plus the rule */
        /* Rows 16-18 of 'A', row 19 clear, then 'V' from row 20 after the
           wrap at 21 characters. */
        CHECK(frame[2 * 128] == 0xF7);
        CHECK(frame[3 * 128] == 0x01); /* Row 24: bottom of 'V' */

        CHECK(frame[7 * 128 + 64] == 0x00); /* Old content cleared */
    }

    CHECK(ssd1306_show_fault(dev, NULL) == HAL_OK);
    release(dev);
}

/* Show a fault view and return the frame it sent, or NULL. */
static const uint8_t *fault_frame(rig_t *rig, ssd1306_t *dev, const char *message)
{
    hal_spi_sim_clear_capture(rig->spi);
    CHECK(ssd1306_show_fault(dev, message) == HAL_OK);
    size_t len = 0;
    const uint8_t *cap = capture(rig, &len);
    CHECK(len == WINDOW_BYTES + FRAME_64);
    return (len == WINDOW_BYTES + FRAME_64) ? cap + WINDOW_BYTES : NULL;
}

static void test_fault_screen_wraps_at_words(void)
{
    rig_t rig;
    rig_setup(&rig);
    ssd1306_t *dev = init_display(&rig);

    /* "environment_sensor: " fills 20 cells; "I2C" does not fit in the last
       one, so the whole word starts line 2 instead of splitting "I" / "2C". */
    const uint8_t *frame = fault_frame(&rig, dev, "environment_sensor: I2C timeout");
    if (frame != NULL) {
        CHECK(frame[3 * 128 + 2] == 0x07);   /* 'I' stem, rows 24-26, at x=2 */
        CHECK(frame[1 * 128 + 122] == 0x02); /* Line 1 ends before x=120: rule only */
        CHECK(frame[2 * 128 + 122] == 0x00);
    }

    /* A space that lands on the edge is dropped, so line 2 starts at x=0. */
    frame = fault_frame(&rig, dev, "AAAAAAAAAAAAAAAAAAAAA B");
    if (frame != NULL) {
        CHECK(frame[3 * 128 + 0] == 0x07);   /* 'B' left edge, rows 24-26 */
    }

    release(dev);
}

static void test_power_and_release(void)
{
    rig_t rig;
    rig_setup(&rig);
    ssd1306_t *dev = init_display(&rig);

    hal_spi_sim_clear_capture(rig.spi);
    CHECK(ssd1306_set_power(dev, false) == HAL_OK);
    CHECK(ssd1306_set_power(dev, true) == HAL_OK);
    size_t len = 0;
    const uint8_t *cap = capture(&rig, &len);
    CHECK(len == 2);
    if (len == 2) {
        CHECK(cap[0] == 0xAE && cap[1] == 0xAF);
        CHECK(!dc_level_at(&rig.dc, 0) && !dc_level_at(&rig.dc, 1));
    }

    /* Release switches the panel off. */
    hal_spi_sim_clear_capture(rig.spi);
    CHECK(ssd1306_deinit(dev) == HAL_OK);
    cap = capture(&rig, &len);
    CHECK(len == 1 && cap[0] == 0xAE);

    /* The released handle is dead. */
    CHECK(ssd1306_deinit(dev) == HAL_ERR_NOT_INIT);
    CHECK(ssd1306_draw_text(dev, 0, 0, "A") == HAL_ERR_NOT_INIT);
    CHECK(ssd1306_render(dev) == HAL_ERR_NOT_INIT);
    CHECK(ssd1306_set_pixel(NULL, 0, 0, true) == HAL_ERR_INVALID_ARG);
}

int main(void)
{
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "init_rejects_bad_arguments",        test_init_rejects_bad_arguments },
        { "init_sends_expected_sequence",      test_init_sends_expected_sequence },
        { "init_128x32_without_reset",         test_init_128x32_without_reset },
        { "init_failure_releases_instance",    test_init_failure_releases_instance },
        { "init_rejects_duplicate_display",    test_init_rejects_duplicate_display },
        { "pixels_map_to_controller_layout",   test_pixels_map_to_controller_layout },
        { "text_uses_font_and_clips",          test_text_uses_font_and_clips },
        { "render_reports_spi_fault",          test_render_reports_spi_fault_and_keeps_frame },
        { "render_reports_dc_fault",           test_render_reports_dc_fault },
        { "fault_screen",                      test_fault_screen },
        { "fault_screen_wraps_at_words",       test_fault_screen_wraps_at_words },
        { "power_and_release",                 test_power_and_release },
    };

    int n = (int)(sizeof(tests) / sizeof(tests[0]));
    for (int i = 0; i < n; ++i) {
        host_sim_reset();
        int before = g_failures;
        tests[i].fn();
        printf("[%s] %s\n", (g_failures == before) ? "PASS" : "FAIL", tests[i].name);
    }

    printf("\n%d test(s), %d check failure(s)\n", n, g_failures);
    return g_failures == 0 ? 0 : 1;
}
