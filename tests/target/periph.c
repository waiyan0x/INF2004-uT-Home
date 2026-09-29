/**
 * @file periph.c
 * @brief Exercise real BMP3 compensation, bounded transport and plan recovery.
 */
#ifdef NDEBUG
#error "Peripheral tests require assertions."
#endif
#include "periph.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "activate.h"
#include "bmpinst.h"
#include "i2cfake.h"
#include "shared.h"
#include "uthome/sample.h"

static i2cfake_t       g_fake         = {0};
static bmpinst_t       g_instances[2] = {0};
static activate_plan_t g_plan         = {0};

static void verify_bus (void);
static void verify_driver (void);
static void verify_deadlines (void);
static void make_plan (void);
static sample_environment_t consume (void);
static void verify_samples (void);
static void verify_recovery (void);

/** @brief Run isolated bus, driver and real activation-manager tests. */
void periph_run (void)
{
    verify_bus();
    verify_driver();
    verify_deadlines();
    i2cfake_init(&g_fake);
    i2cfake_sensor(&g_fake, 0U, 12500U);
    i2cfake_sensor(&g_fake, 1U, 13750U);
    make_plan();
    const uint32_t writes = g_fake.writes;
    assert(0U == writes);
    assert(0 == activate_apply(&g_plan));
    verify_samples();
    verify_recovery();
    const activate_plan_t empty = {0};
    assert(0 == activate_apply(&empty));
    assert(!i2cbus_has_claims(&g_fake.bus));
    assert(0U == g_fake.registers[0][0x1BU]);
    assert(0U == g_fake.registers[1][0x1BU]);
    puts("I2C bounds, BMP388 compensation, typed faults and plan recovery "
         "passed.");
}

/** @brief Reject malformed transfers before reaching the backend. */
static void verify_bus (void)
{
    i2cfake_init(&g_fake);
    uint8_t          byte    = 0U;
    i2cbus_request_t request = {.address     = 0x76U,
                                .p_write     = &byte,
                                .write_size  = 1U,
                                .deadline_ms = 10U};
    request.write_size       = 31U;
    assert(I2CBUS_INVALID == i2cbus_transfer(&g_fake.bus, &request));
    request.write_size = 1U;
    request.address    = 0U;
    assert(I2CBUS_INVALID == i2cbus_transfer(&g_fake.bus, &request));
    request.address     = 0x76U;
    request.deadline_ms = 0U;
    assert(I2CBUS_TIMEOUT == i2cbus_transfer(&g_fake.bus, &request));
    assert(0U == g_fake.transfers);
    assert(0 == i2cbus_claim(&g_fake.bus, 0x40U));
    assert(I2CBUS_STATE == i2cbus_claim(&g_fake.bus, 0x40U));
    assert(i2cbus_has_claims(&g_fake.bus));
    assert(0 == i2cbus_release(&g_fake.bus, 0x40U));
    assert(!i2cbus_has_claims(&g_fake.bus));
    assert(I2CBUS_STATE == i2cbus_release(&g_fake.bus, 0x40U));
}

/** @brief Check register settings, identity and independent compensation. */
static void verify_driver (void)
{
    bmp388_t first  = {0};
    bmp388_t second = {0};
    i2cfake_init(&g_fake);
    i2cfake_sensor(&g_fake, 0U, 12500U);
    i2cfake_sensor(&g_fake, 1U, 13750U);
    assert(I2CBUS_INVALID == bmp388_open(&first, &g_fake.bus, 0x75U, 20U));
    assert(0 == bmp388_open(&first, &g_fake.bus, 0x76U, 20U));
    const uint32_t writes = g_fake.writes;
    assert(I2CBUS_STATE == bmp388_open(&second, &g_fake.bus, 0x76U, 20U));
    assert(writes == g_fake.writes);
    assert(0 == bmp388_open(&second, &g_fake.bus, 0x77U, 20U));
    assert(g_fake.delays > 0U);
    assert(0x0AU == g_fake.registers[0][0x1CU]);
    assert(0x03U == g_fake.registers[0][0x1DU]);
    assert(0x00U == g_fake.registers[0][0x1EU]);
    assert(0x04U == g_fake.registers[0][0x1FU]);
    assert(0x33U == g_fake.registers[0][0x1BU]);
    bmp388_value_t value = {0};
    assert(0 == bmp388_read(&first, &value));
    assert((100000.0F == value.pressure_pa) && (8.0F == value.temperature_c));
    assert(0 == bmp388_read(&second, &value));
    assert(110000.0F == value.pressure_pa);
    g_fake.b_present[0] = false;
    assert(I2CBUS_NACK == bmp388_read(&first, &value));
    assert((0.0F == value.pressure_pa) && (0.0F == value.temperature_c));
    assert(I2CBUS_NACK == bmp388_close(&first));
    assert(first.b_claimed);
    g_fake.b_present[0] = true;
    assert(0 == bmp388_close(&first));
    assert(0 == bmp388_close(&second));
    g_fake.registers[0][0] = 0x60U;
    const uint32_t before  = g_fake.writes;
    assert(I2CBUS_UNSUPPORTED == bmp388_open(&first, &g_fake.bus, 0x76U, 20U));
    assert(before == g_fake.writes);
    assert(0 == bmp388_close(&first));
    assert(!i2cbus_has_claims(&g_fake.bus));
    i2cfake_sensor(&g_fake, 0U, 0U);
    assert(0 == bmp388_open(&first, &g_fake.bus, 0x76U, 20U));
    assert(I2CBUS_INVALID == bmp388_read(&first, &value));
    assert((0.0F == value.pressure_pa) && (0.0F == value.temperature_c));
    assert(0 == bmp388_close(&first));
}

/** @brief Ensure elapsed transfers/delays share one budget and can recover. */
static void verify_deadlines (void)
{
    bmp388_t sensor = {0};
    i2cfake_init(&g_fake);
    i2cfake_sensor(&g_fake, 0U, 12500U);
    g_fake.transfer_ms = 3U;
    assert(I2CBUS_TIMEOUT == bmp388_open(&sensor, &g_fake.bus, 0x76U, 10U));
    assert(g_fake.now_ms <= 10U);
    assert(sensor.b_claimed);
    g_fake.transfer_ms = 0U;
    assert(0 == bmp388_close(&sensor));
    assert(0 == bmp388_open(&sensor, &g_fake.bus, 0x76U, 20U));
    const uint64_t started     = g_fake.now_ms;
    g_fake.registers[0][0x03U] = 0x10U;
    bmp388_value_t value       = {0};
    assert(I2CBUS_TIMEOUT == bmp388_read(&sensor, &value));
    assert((g_fake.now_ms - started) <= 20U);
    g_fake.registers[0][0x03U] = 0x70U;
    assert(0 == bmp388_read(&sensor, &value));
    g_fake.fail_at = g_fake.transfers + 1U;
    g_fake.error   = I2CBUS_TIMEOUT;
    assert(I2CBUS_TIMEOUT == bmp388_read(&sensor, &value));
    assert(0 == bmp388_read(&sensor, &value));
    assert(0 == bmp388_close(&sensor));
}

/** @brief Bind two supported instances to one bus and one typed output queue.
 */
static void make_plan (void)
{
    const drvreg_descriptor_t entry    = *bmpinst_descriptor();
    drvreg_registry_t         registry = {0};
    assert(0 == drvreg_init(&registry, &entry, 1U));
    g_plan              = (activate_plan_t){0};
    g_plan.device_count = 2U;
    g_plan.queue_count  = 1U;
    g_plan.queues[0] =
        (msgbus_queue_config_t){.type            = UTHOME_SAMPLE_ENVIRONMENT,
                                .item_size       = sizeof(sample_environment_t),
                                .depth           = 4U,
                                .overflow_policy = UTHOME_MSGBUS_DROP_OLDEST};
    const char * p_names[2] = {"environment-a", "environment-b"};
    for (size_t index = 0U; index < 2U; ++index)
    {
        g_instances[index].p_bus     = &g_fake.bus;
        activate_device_t * p_device = &g_plan.devices[index];
        assert(0 == drvreg_bind(&registry,
                                "bmp388",
                                p_names[index],
                                &g_instances[index],
                                sizeof(g_instances[index]),
                                &p_device->component));
        /* The fixture has only two indices/addresses, all within the fields. */
        const bmpinst_config_t config = {.address = (uint8_t)(0x76U + index),
                                         .source_index  = (uint16_t)index,
                                         .timeout_ms    = 20U,
                                         .h_destination = 0};
        memcpy(p_device->config, &config, sizeof(config));
        p_device->config_size           = sizeof(config);
        p_device->task.priority         = 12U;
        p_device->task.period_ms        = 200U;
        p_device->task.stack_size_bytes = 3072U;
        p_device->worker.p_step         = bmpinst_step;
        p_device->worker.p_cleanup      = bmpinst_cleanup;
        p_device->worker.p_context      = &g_instances[index];
        p_device->heartbeat_ms          = 500U;
        p_device->b_enabled             = true;
    }
}

/** @brief Consume one copied environment sample from the active plan. */
static sample_environment_t consume (void)
{
    sample_environment_t sample = {0};
    assert(0 == shared_consume(
                    0, UTHOME_SAMPLE_ENVIRONMENT, &sample, sizeof(sample)));
    return sample;
}

/** @brief Preserve source identity, zero failed values and explicit health. */
static void verify_samples (void)
{
    assert(0 == bmpinst_step(&g_instances[0]));
    assert(0 == bmpinst_step(&g_instances[1]));
    sample_environment_t sample = consume();
    assert((0U == sample.header.source_index) &&
           (0U == sample.header.sequence));
    assert((UTHOME_SAMPLE_OK == sample.header.status) &&
           (100000.0F == sample.pressure_pa));
    sample = consume();
    assert((1U == sample.header.source_index) &&
           (110000.0F == sample.pressure_pa));
    g_fake.now_ms += 25U;
    g_fake.b_present[0] = false;
    assert(0 == bmpinst_step(&g_instances[0]));
    sample = consume();
    assert((UTHOME_SAMPLE_IO == sample.header.status) &&
           (1U == sample.header.sequence) && (0.0F == sample.pressure_pa) &&
           (sample.header.timestamp_ms == g_fake.now_ms));
    g_fake.b_present[0] = true;
    g_fake.fail_at      = g_fake.transfers + 1U;
    g_fake.error        = I2CBUS_TIMEOUT;
    assert(0 == bmpinst_step(&g_instances[0]));
    sample = consume();
    assert((UTHOME_SAMPLE_TIMEOUT == sample.header.status) &&
           (2U == sample.header.sequence));
    assert(0 == bmpinst_step(&g_instances[0]));
    sample = consume();
    assert((UTHOME_SAMPLE_OK == sample.header.status) &&
           (3U == sample.header.sequence));
    health_snapshot_t health = {0};
    assert(0 == shared_snapshot(&health));
    assert(1U == health.event_count[UTHOME_HEALTH_DEVICE_TIMEOUT]);
    assert(1U == health.event_count[UTHOME_HEALTH_DEVICE_IO]);
}

/** @brief Recover failed physical cleanup and reject conflicting addresses. */
static void verify_recovery (void)
{
    bmpinst_config_t config = {0};
    memcpy(&config, g_plan.devices[1].config, sizeof(config));
    config.address = 0x76U;
    memcpy(g_plan.devices[1].config, &config, sizeof(config));
    assert(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    assert((g_instances[0].b_configured) && (g_instances[1].b_configured));
    assert(0x77U == g_instances[1].sensor.address);
    config.address = 0x77U;
    memcpy(g_plan.devices[1].config, &config, sizeof(config));
    g_fake.b_present[0] = false;
    assert(UTHOME_ACTIVATE_RECOVERY == activate_apply(&g_plan));
    activate_status_t status = {0};
    assert(0 == activate_get_status(&status));
    assert((ACTIVATE_NEEDS_RECOVERY == status.state) &&
           (1U == status.retained_instances));
    g_fake.b_present[0] = true;
    assert(0 == activate_recover());
    g_plan.devices[1].b_enabled = false;
    assert(0 == activate_apply(&g_plan));
    assert(!g_instances[1].b_configured);
    g_plan.devices[1].b_enabled = true;
    assert(0 == activate_apply(&g_plan));
}

/* End of file. */

