/**
 * @file uarttest.c
 * @brief Verify bounded radar work through the real registry and queues.
 */
#ifdef NDEBUG
#error "UART tests require assertions."
#endif
#include "uarttest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "uartfake.h"
#include "ldinst.h"
#include "activate.h"
#include "shared.h"
static uartfake_t            g_fake[2]      = {0};
static ldinst_t              g_instances[2] = {0};
static activate_plan_t       g_plan         = {0};
static const uartio_config_t g_uart         = {.baud            = 256000U,
                                               .rx_buffer_bytes = 512U,
                                               .port            = 1U,
                                               .tx_gpio         = 21U,
                                               .rx_gpio         = 20U,
                                               .data_bits       = 8U,
                                               .stop_bits       = 1U};
/* Manufacturer protocol v1.03 example, section 2.3: -782,1713,-16,320. */
static const uint8_t g_frame[30] = {
    0xAAU, 0xFFU, 0x03U, 0x00U, 0x0EU, 0x03U, 0xB1U, 0x86U, 0x10U, 0x00U,
    0x40U, 0x01U, 0U,    0U,    0U,    0U,    0U,    0U,    0U,    0U,
    0U,    0U,    0U,    0U,    0U,    0U,    0U,    0U,    0x55U, 0xCCU};
static void verify_parser (void);
static void verify_transport (void);
static void verify_poll (void);
static void bind_plan (void);
static void verify_plan (void);
static sample_target_report_t consume (void);

/** @brief Host evidence does not qualify native UART or physical radar I/O. */
void uarttest_run (void)
{
    verify_parser();
    verify_transport();
    verify_poll();
    verify_plan();
    puts(
        "UART ownership, bounded polling, radar decoding and recovery passed.");
}

/** @brief Preserve fragments/slots and decode signed magnitude at its limits.
 */
static void verify_parser (void)
{
    ldparse_t              parser = {0};
    sample_target_report_t report = {0};
    assert(!ldparse_feed(NULL, 0U, 0U, &report));
    assert(!ldparse_feed(&parser, 0U, 0U, NULL));
    assert(!ldparse_feed(&parser, 0xAAU, 0U, &report));
    for (size_t index = 0U; index < sizeof(g_frame); ++index)
    {
        assert((29U == index) ==
               ldparse_feed(&parser, g_frame[index], 42U, &report));
    }
    assert((1U == parser.frames) && (1U == parser.discarded_bytes));
    assert((-782 == report.targets[0].x_mm) &&
           (1713 == report.targets[0].y_mm));
    assert((-16 == report.targets[0].speed_cm_s) &&
           (320U == report.targets[0].resolution_mm));
    assert((report.targets[0].b_present) && (!report.targets[1].b_present));
    assert((42U == report.header.timestamp_ms) &&
           (UTHOME_SAMPLE_OK == report.header.status));
    uint8_t frame[30] = {0};
    memcpy(frame, g_frame, sizeof(frame));
    memset(&frame[4], 0, 8U);
    memset(&frame[20], 0xFF, 8U);
    frame[23] = 0x7FU;
    frame[24] = 0U;
    frame[25] = 0x80U;
    for (size_t index = 0U; index < sizeof(frame); ++index)
    {
        assert((29U == index) ==
               ldparse_feed(&parser, frame[index], 43U, &report));
    }
    assert((!report.targets[0].b_present) && (report.targets[2].b_present));
    assert((32767 == report.targets[2].x_mm) &&
           (-32767 == report.targets[2].y_mm));
    assert((0 == report.targets[2].speed_cm_s) &&
           (UINT16_MAX == report.targets[2].resolution_mm));
    frame[29] = 0U;
    for (size_t index = 0U; index < sizeof(frame); ++index)
    {
        assert(!ldparse_feed(&parser, frame[index], 44U, &report));
    }
    assert(1U == parser.framing_errors);
    for (size_t index = 0U; index < sizeof(g_frame); ++index)
    {
        assert((29U == index) ==
               ldparse_feed(&parser, g_frame[index], 45U, &report));
    }
    assert(3U == parser.frames);
    ldparse_reset(&parser);
    assert((0U == parser.used) && (3U == parser.frames));
}

/** @brief Test exclusivity, short TX, capacity limits and retained failed open.
 */
static void verify_transport (void)
{
    uartfake_init(&g_fake[0]);
    const int owner     = 1;
    const int other     = 2;
    uint8_t   bytes[65] = {0};
    size_t    count     = 99U;
    assert(UARTIO_INVALID == uartio_read(NULL, &owner, bytes, 1U, &count));
    assert(0U == count);
    assert(0 == uartio_open(&g_fake[0].port, &g_uart, &owner));
    assert(UARTIO_INVALID == uartio_open(&g_fake[0].port, &g_uart, &other));
    assert(UARTIO_STATE == uartio_close(&g_fake[0].port, &other));
    assert(UARTIO_INVALID ==
           uartio_read(&g_fake[0].port, &owner, bytes, 65U, &count));
    assert(UARTIO_AGAIN ==
           uartio_read(&g_fake[0].port, &owner, bytes, 64U, &count));
    g_fake[0].transmitted = 510U;
    assert(0 == uartio_write(&g_fake[0].port, &owner, bytes, 64U, &count));
    assert(2U == count);
    assert(UARTIO_AGAIN ==
           uartio_write(&g_fake[0].port, &owner, bytes, 1U, &count));
    assert(0U == count);
    g_fake[0].close_error = UARTIO_IO;
    assert(UARTIO_IO == uartio_close(&g_fake[0].port, &owner));
    assert(&owner == g_fake[0].port.p_owner);
    g_fake[0].close_error = 0;
    assert(0 == uartio_close(&g_fake[0].port, &owner));
    g_fake[0].open_error = UARTIO_IO;
    assert(UARTIO_IO == uartio_open(&g_fake[0].port, &g_uart, &owner));
    assert((g_fake[0].b_open) && (!g_fake[0].port.b_ready));
    assert(0 == uartio_close(&g_fake[0].port, &owner));
}

/** @brief Noise never postpones timeout, and damaged data cannot be reused. */
static void verify_poll (void)
{
    uartfake_init(&g_fake[0]);
    ld2450_t               sensor = {0};
    ld2450_t               other  = {0};
    sample_target_report_t report = {0};
    assert(UARTIO_INVALID ==
           ld2450_open(&sensor, &g_fake[0].port, &g_uart, 199U));
    assert(0 == ld2450_open(&sensor, &g_fake[0].port, &g_uart, 200U));
    assert(UARTIO_INVALID ==
           ld2450_open(&other, &g_fake[0].port, &g_uart, 200U));
    assert(0 == ld2450_close(&other));
    assert(&sensor == g_fake[0].port.p_owner);
    assert(0 == uartfake_feed(&g_fake[0], g_frame, sizeof(g_frame)));
    g_fake[0].read_limit = 1U;
    for (size_t index = 0U; index < sizeof(g_frame); ++index)
    {
        const int result = ld2450_poll(&sensor, &report);
        assert(((29U == index) ? 0 : UARTIO_AGAIN) == result);
    }
    assert(1U == sensor.parser.frames);
    g_fake[0].read_limit = 64U;
    uint8_t noise[64]    = {0};
    g_fake[0].now_ms     = 199U;
    assert(0 == uartfake_feed(&g_fake[0], noise, sizeof(noise)));
    assert(UARTIO_AGAIN == ld2450_poll(&sensor, &report));
    g_fake[0].now_ms = 200U;
    assert(UARTIO_TIMEOUT == ld2450_poll(&sensor, &report));
    assert(!report.targets[0].b_present);
    assert(1U == sensor.timeouts);
    assert(UARTIO_AGAIN == ld2450_poll(&sensor, &report));
    assert(UARTIO_AGAIN == ld2450_poll(&sensor, &report));
    assert(0 == uartfake_feed(&g_fake[0], g_frame, sizeof(g_frame)));
    g_fake[0].now_ms = 210U;
    assert(0 == ld2450_poll(&sensor, &report));
    assert(210U == report.header.timestamp_ms);
    for (size_t index = 0U; index < 8U; ++index)
    {
        assert(0 == uartfake_feed(&g_fake[0], noise, sizeof(noise)));
    }
    assert(UARTIO_OVERFLOW == uartfake_feed(&g_fake[0], noise, 1U));
    assert(UARTIO_OVERFLOW == ld2450_poll(&sensor, &report));
    assert((1U == sensor.overflows) && (!report.targets[0].b_present));
    g_fake[0].flush_error = UARTIO_IO;
    assert(UARTIO_IO == ld2450_poll(&sensor, &report));
    g_fake[0].flush_error = 0;
    assert(UARTIO_AGAIN == ld2450_poll(&sensor, &report));
    assert(0U == g_fake[0].received);
    assert(0 == uartfake_feed(&g_fake[0], g_frame, sizeof(g_frame)));
    assert(0 == uartfake_feed(&g_fake[0], g_frame, sizeof(g_frame)));
    const uint32_t reads = g_fake[0].reads;
    assert(0 == ld2450_poll(&sensor, &report));
    assert((4U == sensor.parser.frames) && ((reads + 1U) == g_fake[0].reads));
    g_fake[0].close_error = UARTIO_IO;
    assert(UARTIO_IO == ld2450_close(&sensor));
    assert(&sensor == g_fake[0].port.p_owner);
    g_fake[0].close_error = 0;
    assert(0 == ld2450_close(&sensor));
}

/** @brief Two portable UART contexts prove the driver has no singleton state.
 */
static void bind_plan (void)
{
    const drvreg_descriptor_t driver   = *ldinst_descriptor();
    drvreg_registry_t         registry = {0};
    assert(0 == drvreg_init(&registry, &driver, 1U));
    g_plan = (activate_plan_t){.device_count = 2U, .queue_count = 1U};
    g_plan.queues[0] =
        (msgbus_queue_config_t){.type      = UTHOME_SAMPLE_TARGETS,
                                .item_size = sizeof(sample_target_report_t),
                                .depth     = 2U,
                                .overflow_policy = UTHOME_MSGBUS_DROP_OLDEST};
    const char * p_names[2] = {"radar-a", "radar-b"};
    for (size_t index = 0U; index < 2U; ++index)
    {
        uartfake_init(&g_fake[index]);
        g_instances[index] = (ldinst_t){.p_port = &g_fake[index].port};
        activate_device_t * p_device = &g_plan.devices[index];
        assert(0 == drvreg_bind(&registry,
                                "hlk_ld2450",
                                p_names[index],
                                &g_instances[index],
                                sizeof(g_instances[index]),
                                &p_device->component));
        /* The two fixture indices fit the sample source field. */
        const ldinst_config_t config = {.uart          = g_uart,
                                        .timeout_ms    = 200U,
                                        .source_index  = (uint16_t)index,
                                        .h_destination = 0};
        assert(sizeof(config) <= sizeof(p_device->config));
        memcpy(p_device->config, &config, sizeof(config));
        p_device->config_size           = sizeof(config);
        p_device->task.priority         = 12U;
        p_device->task.period_ms        = 10U;
        p_device->task.stack_size_bytes = 3072U;
        p_device->worker.p_step         = ldinst_step;
        p_device->worker.p_cleanup      = ldinst_cleanup;
        p_device->worker.p_context      = &g_instances[index];
        p_device->heartbeat_ms          = 500U;
        p_device->b_enabled             = true;
    }
}

/** @brief Typed publication, faults and failed teardown use existing runtime.
 */
static void verify_plan (void)
{
    bind_plan();
    assert(NULL == g_fake[0].port.p_owner);
    assert(0 == activate_apply(&g_plan));
    assert(0 == ldinst_step(&g_instances[0]));
    msgbus_stats_t stats = {0};
    assert(0 == shared_queue_stats(0, &stats));
    assert(0U == stats.current_depth);
    for (size_t index = 0U; index < 2U; ++index)
    {
        assert(0 == uartfake_feed(&g_fake[index], g_frame, sizeof(g_frame)));
        assert(0 == ldinst_step(&g_instances[index]));
        const sample_target_report_t sample = consume();
        assert((index == sample.header.source_index) &&
               (0U == sample.header.sequence));
        assert(UTHOME_SAMPLE_OK == sample.header.status);
    }
    g_fake[0].now_ms = 200U;
    assert(0 == ldinst_step(&g_instances[0]));
    sample_target_report_t sample = consume();
    assert((1U == sample.header.sequence) &&
           (UTHOME_SAMPLE_TIMEOUT == sample.header.status));
    assert(!sample.targets[0].b_present);
    assert(0 == ldinst_step(&g_instances[0]));
    g_fake[0].b_overflow = true;
    assert(0 == ldinst_step(&g_instances[0]));
    sample = consume();
    assert((UTHOME_SAMPLE_IO == sample.header.status) &&
           (!sample.targets[0].b_present));
    health_snapshot_t health = {0};
    assert(0 == shared_snapshot(&health));
    assert(1U == health.event_count[UTHOME_HEALTH_DEVICE_TIMEOUT]);
    assert(1U == health.event_count[UTHOME_HEALTH_UART_RX_OVERFLOW]);
    g_fake[0].close_error = UARTIO_IO;
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
    assert((NULL == g_fake[0].port.p_owner) &&
           (NULL == g_fake[1].port.p_owner));
}

/** @brief Receive a copied record with a checked type and capacity. */
static sample_target_report_t consume (void)
{
    sample_target_report_t sample = {0};
    assert(0 ==
           shared_consume(0, UTHOME_SAMPLE_TARGETS, &sample, sizeof(sample)));
    return sample;
}

/* End of file. */

