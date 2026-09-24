/*
 * Host unit tests for the BMP388 driver. The full stack is exercised on the
 * host: driver -> Bosch BMP3 API -> I2C HAL host_sim fake, with no hardware.
 *
 * The register file in the fake is preloaded so the vendor init and read paths
 * run to completion. Note: the calibration and raw data bytes are synthetic,
 * so the numeric pressure/temperature are NOT physically meaningful here. These
 * tests prove the wiring, chip-id check, and error handling; accuracy is
 * validated on hardware with tests/hardware.
 */

#include "bmp388.h"
#include "i2c.h"
#include "host_sim.h"

#include <math.h>
#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("  FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            g_failures++;                                                \
        }                                                                \
    } while (0)

/* BMP388 register addresses (from the datasheet / vendor defs). */
#define REG_CHIP_ID  0x00
#define REG_ERR      0x02
#define REG_STATUS   0x03
#define REG_DATA     0x04
#define REG_PWR_CTRL 0x1B
#define REG_OSR      0x1C
#define REG_ODR      0x1D
#define REG_RESERVED 0x1E
#define REG_CONFIG   0x1F
#define REG_CALIB    0x31
#define CHIP_ID_388  0x50
#define CMD_RDY_BIT  0x10

static hal_i2c_config_t i2c0_cfg(void)
{
    hal_i2c_config_t c = { .port = 0, .sda_gpio = 8, .scl_gpio = 9, .clock_hz = 400000 };
    return c;
}

/* Preload a healthy BMP388 register image at the given address. */
static void preload_bmp388(hal_i2c_bus_t *bus, uint8_t addr)
{
    hal_i2c_sim_set_present(bus, addr, true);
    hal_i2c_sim_set_reg(bus, addr, REG_CHIP_ID, CHIP_ID_388);
    hal_i2c_sim_set_reg(bus, addr, REG_STATUS, CMD_RDY_BIT);
    hal_i2c_sim_set_reg(bus, addr, REG_ERR, 0x00);

    /* 21 non-zero calibration bytes so compensation is not degenerate. */
    for (uint8_t i = 0; i < 21; ++i) {
        hal_i2c_sim_set_reg(bus, addr, (uint8_t)(REG_CALIB + i), (uint8_t)(0x20 + i));
    }
    /* 6 raw pressure+temperature data bytes. */
    for (uint8_t i = 0; i < 6; ++i) {
        hal_i2c_sim_set_reg(bus, addr, (uint8_t)(REG_DATA + i), (uint8_t)(0x40 + i));
    }
}

/* Read one register back from the simulated chip. */
static uint8_t peek_reg(hal_i2c_bus_t *bus, uint8_t addr, uint8_t reg)
{
    uint8_t value = 0;
    CHECK(hal_i2c_write_read(bus, addr, &reg, 1, &value, 1, 100) == HAL_OK);
    return value;
}

static void test_init_rejects_bad_arguments(void)
{
    hal_i2c_bus_t *bus = NULL;
    hal_i2c_config_t cfg = i2c0_cfg();
    CHECK(hal_i2c_init(&cfg, &bus) == HAL_OK);

    bmp388_t *dev = NULL;
    bmp388_config_t bad = { .bus = NULL, .addr7 = 0x76, .timeout_ms = 100, .delay_us = NULL };
    CHECK(bmp388_init(NULL, &dev) == HAL_ERR_INVALID_ARG);
    CHECK(bmp388_init(&bad, &dev) == HAL_ERR_INVALID_ARG); /* null bus */

    /* The BMP388 answers only at 0x76 or 0x77. */
    bmp388_config_t bad_addr = { .bus = bus, .addr7 = 0x40, .timeout_ms = 100, .delay_us = NULL };
    CHECK(bmp388_init(&bad_addr, &dev) == HAL_ERR_INVALID_ARG);
    CHECK(dev == NULL);
}

static void test_init_succeeds_on_healthy_device(void)
{
    hal_i2c_bus_t *bus = NULL;
    hal_i2c_config_t cfg = i2c0_cfg();
    CHECK(hal_i2c_init(&cfg, &bus) == HAL_OK);
    preload_bmp388(bus, 0x76);

    bmp388_config_t c = { .bus = bus, .addr7 = 0x76, .timeout_ms = 100, .delay_us = NULL };
    bmp388_t *dev = NULL;
    CHECK(bmp388_init(&c, &dev) == HAL_OK);
    CHECK(dev != NULL);

    if (dev != NULL) {
        CHECK(bmp388_deinit(dev) == HAL_OK);
    }
}

static void test_init_writes_sampling_settings(void)
{
    hal_i2c_bus_t *bus = NULL;
    hal_i2c_config_t cfg = i2c0_cfg();
    CHECK(hal_i2c_init(&cfg, &bus) == HAL_OK);
    preload_bmp388(bus, 0x76);

    bmp388_config_t c = { .bus = bus, .addr7 = 0x76, .timeout_ms = 100, .delay_us = NULL };
    bmp388_t *dev = NULL;
    CHECK(bmp388_init(&c, &dev) == HAL_OK);

    /* Pressure x4 (bits 2:0 = 2), temperature x2 (bits 5:3 = 1). */
    CHECK(peek_reg(bus, 0x76, REG_OSR) == 0x0A);
    /* 25 Hz output data rate. */
    CHECK(peek_reg(bus, 0x76, REG_ODR) == 0x03);
    /* IIR coefficient 3 (bits 3:1 = 2). */
    CHECK(peek_reg(bus, 0x76, REG_CONFIG) == 0x04);
    /* Pressure and temperature enabled, normal mode. */
    CHECK(peek_reg(bus, 0x76, REG_PWR_CTRL) == 0x33);
    /* Nothing spilled into the reserved register between ODR and CONFIG. */
    CHECK(peek_reg(bus, 0x76, REG_RESERVED) == 0x00);

    if (dev != NULL) {
        CHECK(bmp388_deinit(dev) == HAL_OK);
    }
}

static void test_init_rejects_duplicate_sensor(void)
{
    hal_i2c_bus_t *bus = NULL;
    hal_i2c_config_t cfg = i2c0_cfg();
    CHECK(hal_i2c_init(&cfg, &bus) == HAL_OK);
    preload_bmp388(bus, 0x76);

    bmp388_config_t c = { .bus = bus, .addr7 = 0x76, .timeout_ms = 100, .delay_us = NULL };
    bmp388_t *dev = NULL, *again = NULL;
    CHECK(bmp388_init(&c, &dev) == HAL_OK);

    /* A second init would soft-reset the chip under the first handle. */
    CHECK(bmp388_init(&c, &again) == HAL_ERR_INVALID_ARG);
    CHECK(again == NULL);

    /* Once released, the same sensor can be brought up again. */
    if (dev != NULL) {
        CHECK(bmp388_deinit(dev) == HAL_OK);
    }
    CHECK(bmp388_init(&c, &again) == HAL_OK);
    if (again != NULL) {
        CHECK(bmp388_deinit(again) == HAL_OK);
    }
}

static void test_init_rejects_wrong_chip_id(void)
{
    hal_i2c_bus_t *bus = NULL;
    hal_i2c_config_t cfg = i2c0_cfg();
    CHECK(hal_i2c_init(&cfg, &bus) == HAL_OK);
    preload_bmp388(bus, 0x76);
    hal_i2c_sim_set_reg(bus, 0x76, REG_CHIP_ID, 0x99); /* not a BMP388/BMP390 */

    bmp388_config_t c = { .bus = bus, .addr7 = 0x76, .timeout_ms = 100, .delay_us = NULL };
    bmp388_t *dev = NULL;
    CHECK(bmp388_init(&c, &dev) == HAL_ERR_UNSUPPORTED); /* acked, wrong chip */
    CHECK(dev == NULL);
}

static void test_init_reports_absent_device(void)
{
    hal_i2c_bus_t *bus = NULL;
    hal_i2c_config_t cfg = i2c0_cfg();
    CHECK(hal_i2c_init(&cfg, &bus) == HAL_OK);
    /* Nothing preloaded: the address does not acknowledge. */

    bmp388_config_t c = { .bus = bus, .addr7 = 0x76, .timeout_ms = 100, .delay_us = NULL };
    bmp388_t *dev = NULL;
    CHECK(bmp388_init(&c, &dev) == HAL_ERR_NACK);
    CHECK(dev == NULL);
}

static void test_read_returns_finite_sample(void)
{
    hal_i2c_bus_t *bus = NULL;
    hal_i2c_config_t cfg = i2c0_cfg();
    CHECK(hal_i2c_init(&cfg, &bus) == HAL_OK);
    preload_bmp388(bus, 0x76);

    bmp388_config_t c = { .bus = bus, .addr7 = 0x76, .timeout_ms = 100, .delay_us = NULL };
    bmp388_t *dev = NULL;
    CHECK(bmp388_init(&c, &dev) == HAL_OK);

    environment_sample_t sample;
    CHECK(bmp388_read(dev, &sample) == HAL_OK);
    CHECK(sample.status == HAL_OK);
    CHECK(isfinite(sample.pressure_pa));
    CHECK(isfinite(sample.temperature_c));

    if (dev != NULL) {
        CHECK(bmp388_deinit(dev) == HAL_OK);
        CHECK(bmp388_read(dev, &sample) == HAL_ERR_NOT_INIT); /* Released handle */
    }
}

static void test_read_reports_disconnect(void)
{
    hal_i2c_bus_t *bus = NULL;
    hal_i2c_config_t cfg = i2c0_cfg();
    CHECK(hal_i2c_init(&cfg, &bus) == HAL_OK);
    preload_bmp388(bus, 0x76);

    bmp388_config_t c = { .bus = bus, .addr7 = 0x76, .timeout_ms = 100, .delay_us = NULL };
    bmp388_t *dev = NULL;
    CHECK(bmp388_init(&c, &dev) == HAL_OK);

    /* Scenario "fault": the sensor stops responding mid-run. The HAL timeout
       must reach the caller as-is, not as a generic bus error. */
    environment_sample_t sample;
    hal_i2c_sim_fail_next(bus, HAL_ERR_TIMEOUT);
    CHECK(bmp388_read(dev, &sample) == HAL_ERR_TIMEOUT);
    CHECK(sample.status == HAL_ERR_TIMEOUT);

    /* The fault is one-shot, so the next read recovers. */
    CHECK(bmp388_read(dev, &sample) == HAL_OK);
    CHECK(sample.status == HAL_OK);

    /* Unplugged outright: the address stops acknowledging. */
    hal_i2c_sim_set_present(bus, 0x76, false);
    CHECK(bmp388_read(dev, &sample) == HAL_ERR_NACK);
    CHECK(sample.status == HAL_ERR_NACK);

    if (dev != NULL) {
        CHECK(bmp388_deinit(dev) == HAL_OK);
    }
}

int main(void)
{
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "init_rejects_bad_arguments",     test_init_rejects_bad_arguments },
        { "init_succeeds_on_healthy_device",test_init_succeeds_on_healthy_device },
        { "init_writes_sampling_settings",  test_init_writes_sampling_settings },
        { "init_rejects_duplicate_sensor",  test_init_rejects_duplicate_sensor },
        { "init_rejects_wrong_chip_id",     test_init_rejects_wrong_chip_id },
        { "init_reports_absent_device",     test_init_reports_absent_device },
        { "read_returns_finite_sample",     test_read_returns_finite_sample },
        { "read_reports_disconnect",        test_read_reports_disconnect },
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
