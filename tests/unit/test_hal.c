/*
 * Host unit tests for the I2C and SPI HAL contracts, run against the host_sim
 * fakes. No hardware, no kernel: plain gcc. These are the protocol tests in
 * Member 2's deliverable list.
 */

#include "i2c.h"
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

/* Prototype I2C bus: i2c0, SDA 8, SCL 9, 400 kHz, BMP388 at 0x76. */
static hal_i2c_config_t i2c0_cfg(void)
{
    hal_i2c_config_t c = { .port = 0, .sda_gpio = 8, .scl_gpio = 9, .clock_hz = 400000 };
    return c;
}

/* Prototype SPI bus: spi2, MOSI 11, SCLK 12, 8 MHz, OLED CS 10, write-only. */
static hal_spi_config_t spi2_cfg(void)
{
    hal_spi_config_t c = {
        .host = 2, .sclk_gpio = 12, .mosi_gpio = 11,
        .miso_gpio = HAL_GPIO_NONE, .clock_hz = 8000000
    };
    return c;
}

/* OLED on spi2: CS 10, mode 0, active-low CS, inherits the bus clock. */
static hal_spi_device_config_t oled_cfg(void)
{
    hal_spi_device_config_t c = { .cs_gpio = 10, .mode = 0, .cs_active_low = 1, .clock_hz = 0 };
    return c;
}

static void test_i2c_init_arguments(void)
{
    hal_i2c_bus_t *bus = NULL;
    hal_i2c_config_t cfg = i2c0_cfg();

    CHECK(hal_i2c_init(NULL, &bus) == HAL_ERR_INVALID_ARG);
    CHECK(hal_i2c_init(&cfg, NULL) == HAL_ERR_INVALID_ARG);
    CHECK(hal_i2c_init(&cfg, &bus) == HAL_OK);
    CHECK(bus != NULL);
}

static void test_i2c_init_rejects_bad_config(void)
{
    hal_i2c_bus_t *bus = NULL, *dup = NULL;
    hal_i2c_config_t cfg = i2c0_cfg();

    cfg.clock_hz = 0;
    CHECK(hal_i2c_init(&cfg, &bus) == HAL_ERR_INVALID_ARG);
    CHECK(bus == NULL);

    /* i2c0 can only be brought up once. */
    cfg = i2c0_cfg();
    CHECK(hal_i2c_init(&cfg, &bus) == HAL_OK);
    CHECK(hal_i2c_init(&cfg, &dup) == HAL_ERR_INVALID_ARG);
    CHECK(dup == NULL);

    /* Once released, the port is free again and the old handle is dead. */
    CHECK(hal_i2c_deinit(bus) == HAL_OK);
    CHECK(hal_i2c_deinit(bus) == HAL_ERR_NOT_INIT);
    CHECK(hal_i2c_init(&cfg, &dup) == HAL_OK);
}

static void test_i2c_write_then_read(void)
{
    hal_i2c_bus_t *bus = NULL;
    hal_i2c_config_t cfg = i2c0_cfg();
    CHECK(hal_i2c_init(&cfg, &bus) == HAL_OK);

    const uint8_t addr = 0x76;
    hal_i2c_sim_set_present(bus, addr, true);

    /* Write two bytes starting at register 0x10, then point back and read. */
    const uint8_t wr[] = { 0x10, 0xAA, 0xBB };
    CHECK(hal_i2c_write(bus, addr, wr, sizeof(wr), 100) == HAL_OK);
    const uint8_t idx = 0x10;
    CHECK(hal_i2c_write(bus, addr, &idx, 1, 100) == HAL_OK);
    uint8_t rd[2] = {0};
    CHECK(hal_i2c_read(bus, addr, rd, sizeof(rd), 100) == HAL_OK);
    CHECK(rd[0] == 0xAA && rd[1] == 0xBB);
}

static void test_i2c_register_read(void)
{
    hal_i2c_bus_t *bus = NULL;
    hal_i2c_config_t cfg = i2c0_cfg();
    CHECK(hal_i2c_init(&cfg, &bus) == HAL_OK);

    /* BMP388 present, chip-id register 0x00 preloaded. */
    const uint8_t addr = 0x76;
    hal_i2c_sim_set_present(bus, addr, true);
    hal_i2c_sim_set_reg(bus, addr, 0x00, 0x50);

    /* Driver reads chip id: write register index, repeated start, read one byte. */
    uint8_t reg = 0x00;
    uint8_t id = 0;
    CHECK(hal_i2c_write_read(bus, addr, &reg, 1, &id, 1, 100) == HAL_OK);
    CHECK(id == 0x50);
}

static void test_i2c_absent_device_nacks(void)
{
    hal_i2c_bus_t *bus = NULL;
    hal_i2c_config_t cfg = i2c0_cfg();
    CHECK(hal_i2c_init(&cfg, &bus) == HAL_OK);

    uint8_t reg = 0x00, id = 0;
    /* Nothing marked present, so any access is not acknowledged. */
    CHECK(hal_i2c_write_read(bus, 0x76, &reg, 1, &id, 1, 100) == HAL_ERR_NACK);
}

static void test_i2c_disconnect_times_out(void)
{
    hal_i2c_bus_t *bus = NULL;
    hal_i2c_config_t cfg = i2c0_cfg();
    CHECK(hal_i2c_init(&cfg, &bus) == HAL_OK);

    const uint8_t addr = 0x76;
    hal_i2c_sim_set_present(bus, addr, true);

    /* Scenario "fault": disconnect the BMP388, next read must surface a timeout. */
    hal_i2c_sim_fail_next(bus, HAL_ERR_TIMEOUT);
    uint8_t reg = 0x04, out[3] = {0};
    CHECK(hal_i2c_write_read(bus, addr, &reg, 1, out, 3, 100) == HAL_ERR_TIMEOUT);

    /* The fault is one-shot; the bus recovers on the following transaction. */
    CHECK(hal_i2c_write_read(bus, addr, &reg, 1, out, 3, 100) == HAL_OK);
}

static void test_spi_write_is_captured(void)
{
    hal_spi_bus_t *bus = NULL;
    hal_spi_config_t bcfg = spi2_cfg();
    CHECK(hal_spi_init(&bcfg, &bus) == HAL_OK);

    hal_spi_device_config_t dcfg = oled_cfg();
    hal_spi_device_t *oled = NULL;
    CHECK(hal_spi_device_add(bus, &dcfg, &oled) == HAL_OK);
    CHECK(oled != NULL);

    /* An OLED driver would write a command byte then data; here we just check
       the exact bytes reach the bus in order. */
    const uint8_t seq[] = { 0xAE, 0xD5, 0x80, 0xAF };
    CHECK(hal_spi_write(oled, seq, sizeof(seq), 100) == HAL_OK);

    const uint8_t *cap = NULL;
    size_t cap_len = 0;
    hal_spi_sim_get_capture(oled, &cap, &cap_len);
    CHECK(cap_len == sizeof(seq));
    CHECK(memcmp(cap, seq, sizeof(seq)) == 0);

    /* spi2 has no MISO line, so there is nothing to read back. */
    uint8_t rx = 0;
    CHECK(hal_spi_transfer(oled, seq, &rx, 1, 100) == HAL_ERR_UNSUPPORTED);
}

static void test_spi_fault_is_reported(void)
{
    hal_spi_bus_t *bus = NULL;
    hal_spi_config_t bcfg = spi2_cfg();
    CHECK(hal_spi_init(&bcfg, &bus) == HAL_OK);

    hal_spi_device_config_t dcfg = oled_cfg();
    hal_spi_device_t *oled = NULL;
    CHECK(hal_spi_device_add(bus, &dcfg, &oled) == HAL_OK);

    hal_spi_sim_fail_next(oled, HAL_ERR_TIMEOUT);
    uint8_t b = 0xA5;
    CHECK(hal_spi_write(oled, &b, 1, 100) == HAL_ERR_TIMEOUT);
    CHECK(hal_spi_write(oled, &b, 1, 100) == HAL_OK); /* recovers */
}

static void test_spi_full_duplex_reads_miso(void)
{
    hal_spi_bus_t *bus = NULL;
    hal_spi_config_t bcfg = spi2_cfg();
    bcfg.miso_gpio = 7; /* Reading needs a controller-in line */
    CHECK(hal_spi_init(&bcfg, &bus) == HAL_OK);

    hal_spi_device_config_t dcfg = oled_cfg();
    hal_spi_device_t *dev = NULL;
    CHECK(hal_spi_device_add(bus, &dcfg, &dev) == HAL_OK);
    hal_spi_sim_set_miso(dev, 0x5A);

    const uint8_t tx[] = { 0x01, 0x02, 0x03 };
    uint8_t rx[3] = {0};
    CHECK(hal_spi_transfer(dev, tx, rx, sizeof(tx), 100) == HAL_OK);
    CHECK(rx[0] == 0x5A && rx[1] == 0x5A && rx[2] == 0x5A);

    /* Read-only transfer clocks out zeros. */
    hal_spi_sim_clear_capture(dev);
    CHECK(hal_spi_transfer(dev, NULL, rx, 2, 100) == HAL_OK);
    const uint8_t *cap = NULL;
    size_t cap_len = 0;
    hal_spi_sim_get_capture(dev, &cap, &cap_len);
    CHECK(cap_len == 2 && cap[0] == 0x00 && cap[1] == 0x00);
}

static void test_spi_rejects_bad_config(void)
{
    hal_spi_bus_t *bus = NULL, *dup = NULL;
    hal_spi_config_t bcfg = spi2_cfg();

    bcfg.clock_hz = 0;
    CHECK(hal_spi_init(&bcfg, &bus) == HAL_ERR_INVALID_ARG);
    CHECK(bus == NULL);

    /* spi2 can only be brought up once. */
    bcfg = spi2_cfg();
    CHECK(hal_spi_init(&bcfg, &bus) == HAL_OK);
    CHECK(hal_spi_init(&bcfg, &dup) == HAL_ERR_INVALID_ARG);
    CHECK(dup == NULL);

    hal_spi_device_config_t dcfg = oled_cfg();
    hal_spi_device_t *a = NULL, *b = NULL;
    dcfg.mode = 4;
    CHECK(hal_spi_device_add(bus, &dcfg, &a) == HAL_ERR_INVALID_ARG);
    CHECK(a == NULL);

    /* A GPIO can be the chip select of only one device. */
    dcfg.mode = 0;
    CHECK(hal_spi_device_add(bus, &dcfg, &a) == HAL_OK);
    CHECK(hal_spi_device_add(bus, &dcfg, &b) == HAL_ERR_INVALID_ARG);
    CHECK(b == NULL);

    /* Removing the device frees its chip select. */
    CHECK(hal_spi_device_remove(a) == HAL_OK);
    CHECK(hal_spi_device_remove(a) == HAL_ERR_NOT_INIT);
    CHECK(hal_spi_device_add(bus, &dcfg, &b) == HAL_OK);
}

static void test_spi_deinit_removes_devices(void)
{
    hal_spi_bus_t *bus = NULL;
    hal_spi_config_t bcfg = spi2_cfg();
    CHECK(hal_spi_init(&bcfg, &bus) == HAL_OK);

    hal_spi_device_config_t dcfg = oled_cfg();
    hal_spi_device_t *oled = NULL;
    CHECK(hal_spi_device_add(bus, &dcfg, &oled) == HAL_OK);

    CHECK(hal_spi_deinit(bus) == HAL_OK);
    uint8_t b = 0xA5;
    CHECK(hal_spi_write(oled, &b, 1, 100) == HAL_ERR_NOT_INIT);

    /* Bus and chip select are both free again. */
    CHECK(hal_spi_init(&bcfg, &bus) == HAL_OK);
    CHECK(hal_spi_device_add(bus, &dcfg, &oled) == HAL_OK);
}

int main(void)
{
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "i2c_init_arguments",      test_i2c_init_arguments },
        { "i2c_init_rejects_bad_config", test_i2c_init_rejects_bad_config },
        { "i2c_register_read",       test_i2c_register_read },
        { "i2c_write_then_read",     test_i2c_write_then_read },
        { "i2c_absent_device_nacks", test_i2c_absent_device_nacks },
        { "i2c_disconnect_times_out",test_i2c_disconnect_times_out },
        { "spi_write_is_captured",   test_spi_write_is_captured },
        { "spi_fault_is_reported",   test_spi_fault_is_reported },
        { "spi_full_duplex_reads_miso", test_spi_full_duplex_reads_miso },
        { "spi_rejects_bad_config",  test_spi_rejects_bad_config },
        { "spi_deinit_removes_devices", test_spi_deinit_removes_devices },
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
