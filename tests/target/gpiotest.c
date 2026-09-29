/**
 * @file gpiotest.c
 * @brief Check owned pins, distance errors and zero-duty cleanup.
 */
#ifdef NDEBUG
#error "GPIO tests require assertions."
#endif
#include "gpiotest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gpiofake.h"
#include "hcinst.h"
#include "pwminst.h"
#include "activate.h"
#include "shared.h"
#include "uthome/sample.h"
static gpiofake_t g_fake[2] = {0};

static hcinst_t g_instances[2] = {0};

static pinpool_t g_pins = {.allowed = UINT32_C(0x4FB)};

static activate_plan_t g_plan = {0};

static const pulseio_config_t g_pulse = {.trigger_gpio = 4U, .input_gpio = 5U};

static const pwmio_config_t g_pwm = {.frequency_hz = 1000U, .gpio = 4U};

static void verify_distance (void);
static void verify_pwm (void);
static void bind_plan (void);
static void verify_plan (void);
static sample_distance_t consume (void);

/** @brief Host tests cover contracts; no peripheral register behavior is faked.
 */
void gpiotest_run (void)
{
    verify_distance();
    verify_pwm();
    verify_plan();
    puts("GPIO ownership, distance status, cadence and PWM cleanup passed.");
}

/** @brief Boundary widths, timing and backend failures cannot leak old data. */
static void verify_distance (void)
{
    gpiofake_init(&g_fake[0], &g_pins);
    hc_sr04_t       sensor = {0};
    hc_sr04_t       other  = {0};
    hc_sr04_value_t value  = {0};
    assert(PULSEIO_INVALID ==
           hc_sr04_open(&sensor, &g_fake[0].pulse, &g_pulse, 30001U));
    assert(0 == hc_sr04_open(&sensor, &g_fake[0].pulse, &g_pulse, 30000U));
    assert(PULSEIO_INVALID ==
           hc_sr04_open(&other, &g_fake[0].pulse, &g_pulse, 30000U));
    assert(0 == hc_sr04_close(&other));
    assert(&sensor == g_fake[0].pulse.p_owner);
    assert(PULSEIO_STATE == pulseio_close(&g_fake[0].pulse, &other));
    assert(0 == hc_sr04_read(&sensor, &value));
    assert((1000U == value.distance_mm) && (5800U == value.echo_us));
    assert(30000U == g_fake[0].last_timeout_us);
    g_fake[0].now_ms = 61U;
    assert(PULSEIO_BUSY == hc_sr04_read(&sensor, &value));
    assert((0U == value.distance_mm) && (1U == g_fake[0].captures));
    g_fake[0].now_ms  = 62U;
    g_fake[0].high_us = 116U;
    assert(0 == hc_sr04_read(&sensor, &value));
    assert(20U == value.distance_mm);
    g_fake[0].now_ms += 62U;
    g_fake[0].high_us = 23200U;
    assert(0 == hc_sr04_read(&sensor, &value));
    assert(4000U == value.distance_mm);
    const uint32_t bad_widths[] = {0U, 100U, 23210U, 30001U};
    for (size_t index = 0U; index < 4U; ++index)
    {
        g_fake[0].now_ms += 62U;
        g_fake[0].high_us = bad_widths[index];
        assert(0 > hc_sr04_read(&sensor, &value));
        assert((0U == value.distance_mm) && (0U == value.echo_us));
    }
    g_fake[0].now_ms += 62U;
    g_fake[0].high_us       = 5800U;
    g_fake[0].measure_error = PULSEIO_TIMEOUT;
    assert(PULSEIO_TIMEOUT == hc_sr04_read(&sensor, &value));
    assert((0U == value.distance_mm) && (0U == value.echo_us));
    uint32_t high_us = 99U;
    assert(PULSEIO_INVALID ==
           pulseio_measure(&g_fake[0].pulse, &sensor, 101U, 30000U, &high_us));
    assert(0U == high_us);
    g_fake[0].now_ms = 0U;
    assert(PULSEIO_STATE == hc_sr04_read(&sensor, &value));
    g_fake[0].close_error = PULSEIO_IO;
    assert(PULSEIO_IO == hc_sr04_close(&sensor));
    assert(&sensor == g_fake[0].pulse.p_owner);
    g_fake[0].close_error = 0;
    assert(0 == hc_sr04_close(&sensor));
    g_fake[0].open_error = PULSEIO_IO;
    assert(PULSEIO_IO ==
           hc_sr04_open(&sensor, &g_fake[0].pulse, &g_pulse, 30000U));
    assert(!g_fake[0].pulse.b_ready);
    assert(0 == hc_sr04_close(&sensor));
    assert(NULL == g_pins.p_owners[4]);
    gpiofake_init(&g_fake[0], &g_pins);
    g_fake[0].elapsed_ms = 30U;
    assert(0 == hc_sr04_open(&sensor, &g_fake[0].pulse, &g_pulse, 30000U));
    assert(0 == hc_sr04_read(&sensor, &value));
    assert(30U == sensor.last_finish_ms);
    g_fake[0].now_ms = 91U;
    assert(PULSEIO_BUSY == hc_sr04_read(&sensor, &value));
    assert(1U == g_fake[0].captures);
    g_fake[0].now_ms = 92U;
    assert(0 == hc_sr04_read(&sensor, &value));
    assert(122U == sensor.last_finish_ms);
    assert(0 == hc_sr04_close(&sensor));
}

/** @brief Pin conflicts, command bounds and failed cleanup preserve ownership.
 */
static void verify_pwm (void)
{
    gpiofake_init(&g_fake[0], &g_pins);
    gpiofake_init(&g_fake[1], &g_pins);
    const int owner = 1;
    assert(0 == pulseio_open(&g_fake[0].pulse, &g_pulse, &owner));
    pwminst_t                 instance  = {.p_port = &g_fake[1].pwm};
    const drvreg_descriptor_t driver    = *pwminst_descriptor();
    drvreg_registry_t         registry  = {0};
    component_t               component = {0};
    assert(0 == drvreg_init(&registry, &driver, 1U));
    assert(0 == drvreg_bind(&registry,
                            "pwm_output",
                            "output-a",
                            &instance,
                            sizeof(instance),
                            &component));
    assert(UTHOME_COMPONENT_ERR_OPERATION ==
           component_configure(&component, &g_pwm, sizeof(g_pwm)));
    assert(0 == pwminst_cleanup(&instance));
    assert(&g_fake[0].pulse == g_pins.p_owners[4]);
    assert(0 == pulseio_close(&g_fake[0].pulse, &owner));
    assert(0 == component_configure(&component, &g_pwm, sizeof(g_pwm)));
    assert(0U == g_fake[1].duty_permille);
    pwminst_command_t command = {.duty_permille = 500U};
    assert(0 == component_write(&component, &command, sizeof(command)));
    assert(500U == g_fake[1].duty_permille);
    command.duty_permille = 1000U;
    assert(0 == component_write(&component, &command, sizeof(command)));
    command.duty_permille = 1001U;
    assert(UTHOME_COMPONENT_ERR_OPERATION ==
           component_write(&component, &command, sizeof(command)));
    assert(1000U == g_fake[1].duty_permille);
    command.duty_permille = 0U;
    g_fake[1].set_error   = PWMIO_IO;
    assert(UTHOME_COMPONENT_ERR_OPERATION ==
           component_write(&component, &command, sizeof(command)));
    assert(1000U == g_fake[1].pwm.duty_permille);
    g_fake[1].set_error = 0;
    assert(PWMIO_STATE == pwmio_close(&g_fake[1].pwm, &owner));
    g_fake[1].close_error = PWMIO_IO;
    assert(PWMIO_IO == pwminst_cleanup(&instance));
    assert((0U == g_fake[1].duty_permille) &&
           (&instance == g_fake[1].pwm.p_owner));
    assert(UTHOME_COMPONENT_ERR_OPERATION ==
           component_write(&component, &command, sizeof(command)));
    g_fake[1].close_error = 0;
    assert(0 == pwminst_cleanup(&instance));
    assert(NULL == g_pins.p_owners[4]);
    g_fake[1].open_error = PWMIO_IO;
    assert(UTHOME_COMPONENT_ERR_OPERATION ==
           component_configure(&component, &g_pwm, sizeof(g_pwm)));
    assert(0 == pwminst_cleanup(&instance));
    assert(NULL == g_fake[1].pwm.p_owner);
}

/** @brief Two sensor instances share infrastructure but keep separate state. */
static void bind_plan (void)
{
    const drvreg_descriptor_t driver   = *hcinst_descriptor();
    drvreg_registry_t         registry = {0};
    assert(0 == drvreg_init(&registry, &driver, 1U));
    g_plan = (activate_plan_t){.device_count = 2U, .queue_count = 1U};
    g_plan.queues[0] =
        (msgbus_queue_config_t){.type            = UTHOME_SAMPLE_DISTANCE,
                                .item_size       = sizeof(sample_distance_t),
                                .depth           = 2U,
                                .overflow_policy = UTHOME_MSGBUS_DROP_OLDEST};
    const char * p_names[2] = {"distance-a", "distance-b"};
    for (size_t index = 0U; index < 2U; ++index)
    {
        gpiofake_init(&g_fake[index], &g_pins);
        g_instances[index] = (hcinst_t){.p_port = &g_fake[index].pulse};
        activate_device_t * p_device = &g_plan.devices[index];
        assert(0 == drvreg_bind(&registry,
                                "hc_sr04",
                                p_names[index],
                                &g_instances[index],
                                sizeof(g_instances[index]),
                                &p_device->component));
        /* Indices 0..1 fit the fixed-width fields and choose exposed pins. */
        const hcinst_config_t config = {
            .pulse         = {.trigger_gpio = (uint8_t)(4U + (index * 2U)),
                              .input_gpio   = (uint8_t)(5U + (index * 2U))},
            .source_index  = (uint16_t)index,
            .timeout_us    = 30000U,
            .h_destination = 0};
        assert(sizeof(config) <= sizeof(p_device->config));
        memcpy(p_device->config, &config, sizeof(config));
        p_device->config_size           = sizeof(config);
        p_device->task.priority         = 10U;
        p_device->task.period_ms        = 100U;
        p_device->task.stack_size_bytes = 3072U;
        p_device->worker.p_step         = hcinst_step;
        p_device->worker.p_cleanup      = hcinst_cleanup;
        p_device->worker.p_context      = &g_instances[index];
        p_device->heartbeat_ms          = 500U;
        p_device->b_enabled             = true;
    }
}

/** @brief Publish typed errors, suppress busy work and recover failed teardown.
 */
static void verify_plan (void)
{
    bind_plan();
    assert(NULL == g_fake[0].pulse.p_owner);
    assert(0 == activate_apply(&g_plan));
    for (size_t index = 0U; index < 2U; ++index)
    {
        assert(0 == hcinst_step(&g_instances[index]));
        const sample_distance_t sample = consume();
        assert((index == sample.header.source_index) &&
               (0U == sample.header.sequence));
        assert((UTHOME_SAMPLE_OK == sample.header.status) &&
               (1000U == sample.distance_mm));
    }
    assert(0 == hcinst_step(&g_instances[0]));
    msgbus_stats_t stats = {0};
    assert(0 == shared_queue_stats(0, &stats));
    assert(0U == stats.current_depth);
    g_fake[0].now_ms        = 100U;
    g_fake[0].measure_error = PULSEIO_TIMEOUT;
    assert(0 == hcinst_step(&g_instances[0]));
    sample_distance_t sample = consume();
    assert((1U == sample.header.sequence) &&
           (UTHOME_SAMPLE_TIMEOUT == sample.header.status));
    assert((0U == sample.echo_us) && (0U == sample.distance_mm));
    g_fake[0].now_ms        = 200U;
    g_fake[0].measure_error = 0;
    g_fake[0].high_us       = 100U;
    assert(0 == hcinst_step(&g_instances[0]));
    sample = consume();
    assert(UTHOME_SAMPLE_INVALID == sample.header.status);
    health_snapshot_t health = {0};
    assert(0 == shared_snapshot(&health));
    assert(1U == health.event_count[UTHOME_HEALTH_DEVICE_TIMEOUT]);
    assert(1U == health.event_count[UTHOME_HEALTH_DEVICE_IO]);
    g_fake[0].close_error = PULSEIO_IO;
    assert(UTHOME_ACTIVATE_RECOVERY == activate_apply(&g_plan));
    activate_status_t status = {0};
    assert(0 == activate_get_status(&status));
    assert((ACTIVATE_NEEDS_RECOVERY == status.state) &&
           (1U == status.retained_instances));
    g_fake[0].close_error = 0;
    assert(0 == activate_recover());
    assert(0 == activate_apply(&g_plan));
    const activate_plan_t empty = {0};
    assert(0 == activate_apply(&empty));
    assert((NULL == g_fake[0].pulse.p_owner) &&
           (NULL == g_fake[1].pulse.p_owner));
}

/** @brief Receive only the expected copied distance record. */
static sample_distance_t consume (void)
{
    sample_distance_t sample = {0};
    assert(0 ==
           shared_consume(0, UTHOME_SAMPLE_DISTANCE, &sample, sizeof(sample)));
    return sample;
}

/* End of file. */

