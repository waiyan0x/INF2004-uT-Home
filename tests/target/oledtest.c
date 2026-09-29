/**
 * @file oledtest.c
 * @brief Test independent displays through real registry and activation code.
 */
#ifdef NDEBUG
#error "OLED tests require assertions."
#endif
#include "oledtest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pinpool.h"
#include "spifake.h"
#include "oledinst.h"
#include "oledview.h"
#include <math.h>
#include "activate.h"
#include "shared.h"
static spifake_t              g_fake         = {0};
static spibus_device_t        g_devices[2]   = {0};
static spifake_line_t         g_data[2]      = {0};
static spifake_line_t         g_reset[2]     = {0};
static oledinst_t             g_instances[2] = {0};
static activate_plan_t        g_plan         = {0};
static const ssd1306_config_t g_config       = {.timeout_ms       = 10U,
                                                .setup_timeout_ms = 100U,
                                                .height           = 64U,
                                                .contrast         = 0x7FU,
                                                .b_charge_pump    = true};
static void verify_view (ssd1306_t * p_display);
static void verify_pins (void);
static void verify_bus (void);
static void verify_display (void);
static void verify_plan (void);
static void bind_plan (void);

/** @brief Keep all physical acceptance pending while proving software paths. */
void oledtest_run (void)
{
    verify_pins();
    verify_bus();
    verify_display();
    verify_plan();
    puts("SPI bounds, pin ownership, independent OLED frames and recovery "
         "passed.");
}

/** @brief Claims/releases are all-or-nothing and reject the wrong owner. */
static void verify_pins (void)
{
    pinpool_t pool   = {.allowed = UINT32_C(0x4FB)};
    const int first  = 1;
    const int second = 2;
    assert(0 == pinpool_claim(&pool, 3U, &first));
    assert(-1 == pinpool_claim(&pool, 10U, &second));
    assert(NULL == pool.p_owners[3]);
    assert(-1 == pinpool_claim(&pool, 4U, &second));
    assert(-1 == pinpool_release(&pool, 3U, &second));
    assert(&first == pool.p_owners[0]);
    assert(0 == pinpool_release(&pool, 3U, &first));
    assert(0 == pinpool_claim(&pool, 10U, &second));
    assert(0 == pinpool_release(&pool, 10U, &second));
    assert(-1 == pinpool_claim(&pool, 0U, &first));
    assert(-1 == pinpool_claim(&pool, UINT32_C(0x80000000), &first));
}

/** @brief Check bounds, copied handles, chip selects and duplex data.
 */
static void verify_bus (void)
{
    spifake_init(&g_fake);
    const spibus_config_t config = {.cs_gpio = 3U, .clock_hz = 1000000U};
    assert(0 == spibus_attach(&g_fake.bus, &g_devices[0], &config));
    assert(SPIBUS_STATE == spibus_attach(&g_fake.bus, &g_devices[1], &config));
    spibus_device_t  copy     = g_devices[0];
    uint8_t          bytes[3] = {1U, 2U, 3U};
    uint8_t          reply[3] = {0};
    spibus_request_t request  = {
         .p_write = bytes, .p_read = reply, .size = 3U, .deadline_ms = 10U};
    assert(SPIBUS_INVALID == spibus_transfer(&copy, &request));
    assert(0 == spibus_transfer(&g_devices[0], &request));
    assert(0 == memcmp(bytes, reply, sizeof(bytes)));
    request.size          = UTHOME_SPI_BYTES + 1U;
    const unsigned before = g_fake.transfers;
    assert(SPIBUS_INVALID == spibus_transfer(&g_devices[0], &request));
    request.size = 0U;
    assert(SPIBUS_INVALID == spibus_transfer(&g_devices[0], &request));
    request.size        = 3U;
    request.deadline_ms = g_fake.now_ms;
    assert(SPIBUS_TIMEOUT == spibus_transfer(&g_devices[0], &request));
    assert(before == g_fake.transfers);
    assert(0 == spibus_claim(&g_devices[0], &copy));
    assert(SPIBUS_STATE == spibus_detach(&g_devices[0]));
    assert(SPIBUS_STATE == spibus_release(&g_devices[0], &config));
    assert(0 == spibus_release(&g_devices[0], &copy));
    g_fake.b_detach_fail = true;
    assert(SPIBUS_IO == spibus_detach(&g_devices[0]));
    assert(spibus_has_devices(&g_fake.bus));
    g_fake.b_detach_fail = false;
    assert(0 == spibus_detach(&g_devices[0]));
    assert(!spibus_has_devices(&g_fake.bus));
    g_fake.b_attach_fail = true;
    assert(SPIBUS_IO == spibus_attach(&g_fake.bus, &g_devices[0], &config));
    assert(!spibus_has_devices(&g_fake.bus));
}

/** @brief Prove reset, framebuffer layout, whole budgets and retained cleanup.
 */
static void verify_display (void)
{
    spifake_init(&g_fake);
    const spibus_config_t bus_config = {.cs_gpio = 3U, .clock_hz = 1000000U};
    assert(0 == spibus_attach(&g_fake.bus, &g_devices[0], &bus_config));
    g_fake.p_data_lines[3]    = &g_data[0];
    const digiout_t data_line = {.p_set = spifake_set, .p_context = &g_data[0]};
    const digiout_t reset   = {.p_set = spifake_set, .p_context = &g_reset[0]};
    ssd1306_t       display = {0};
    assert(0 ==
           ssd1306_open(&display, &g_devices[0], data_line, reset, &g_config));
    assert(12U == g_fake.delayed_ms);
    assert((g_reset[0].b_levels[0]) && (!g_reset[0].b_levels[1]) &&
           (g_reset[0].b_levels[2]));
    assert((4U == g_fake.transfers) && (!display.b_dirty));
    assert(0xAEU == g_fake.records[0].bytes[0]);
    assert((0x21U == g_fake.records[1].bytes[0]) &&
           (!g_fake.records[1].b_data));
    assert((1024U == g_fake.records[2].size) && (g_fake.records[2].b_data));
    assert(0xAFU == g_fake.records[3].bytes[0]);
    verify_view(&display);
    ssd1306_t duplicate = {0};
    assert(
        SPIBUS_STATE ==
        ssd1306_open(&duplicate, &g_devices[0], data_line, reset, &g_config));
    assert(4U == g_fake.transfers);
    assert(0 == ssd1306_pixel(&display, 127U, 63U, true));
    assert(0x80U == display.frame[1023]);
    assert(SPIBUS_INVALID == ssd1306_pixel(&display, 128U, 0U, true));
    g_fake.fail_at = g_fake.transfers + 2U;
    assert(SPIBUS_IO == ssd1306_flush(&display));
    assert((display.b_dirty) && (!g_fake.b_selected));
    const unsigned retry = g_fake.transfers;
    assert(0 == ssd1306_flush(&display));
    assert(0x21U == g_fake.records[retry % UTHOME_SPI_LOG].bytes[0]);
    assert(0x80U == g_fake.records[(retry + 1U) % UTHOME_SPI_LOG].bytes[1023]);
    assert(0 == ssd1306_clear(&display));
    g_fake.transfer_ms = 6U;
    assert(SPIBUS_TIMEOUT == ssd1306_flush(&display));
    assert(display.b_dirty);
    g_fake.transfer_ms = 1U;
    assert(0 == ssd1306_flush(&display));
    g_data[0].b_fail = true;
    assert(SPIBUS_IO == ssd1306_close(&display));
    assert(display.b_claimed);
    assert(SPIBUS_STATE == spibus_detach(&g_devices[0]));
    g_data[0].b_fail = false;
    assert(0 == ssd1306_close(&display));
    ssd1306_config_t short_config = g_config;
    short_config.height           = 32U;
    g_reset[0].b_fail             = true;
    assert(
        SPIBUS_IO ==
        ssd1306_open(&display, &g_devices[0], data_line, reset, &short_config));
    assert((display.b_claimed) && (!display.b_ready));
    g_reset[0].b_fail = false;
    assert(0 == ssd1306_close(&display));
    assert(0 == ssd1306_open(
                    &display, &g_devices[0], data_line, reset, &short_config));
    assert(0 == ssd1306_pixel(&display, 127U, 31U, true));
    assert(0x80U == display.frame[511]);
    assert(0 == ssd1306_close(&display));
    assert(0 == spibus_detach(&g_devices[0]));
}

/** @brief Bind two distinct chip selects through the existing driver registry.
 */
static void bind_plan (void)
{
    spifake_init(&g_fake);
    memset(g_instances, 0, sizeof(g_instances));
    const drvreg_descriptor_t driver   = *oledinst_descriptor();
    drvreg_registry_t         registry = {0};
    assert(0 == drvreg_init(&registry, &driver, 1U));
    g_plan                  = (activate_plan_t){.device_count = 2U};
    const char * p_names[2] = {"display-a", "display-b"};
    for (size_t index = 0U; index < 2U; ++index)
    {
        /* Two fixture indices map to distinct GPIO3/GPIO4. */
        const uint8_t         pin    = (uint8_t)(index + 3U);
        const spibus_config_t config = {.cs_gpio = pin, .clock_hz = 1000000U};
        assert(0 == spibus_attach(&g_fake.bus, &g_devices[index], &config));
        g_fake.p_data_lines[pin] = &g_data[index];
        g_instances[index].p_spi = &g_devices[index];
        g_instances[index].data_line =
            (digiout_t){.p_set = spifake_set, .p_context = &g_data[index]};
        g_instances[index].reset =
            (digiout_t){.p_set = spifake_set, .p_context = &g_reset[index]};
        activate_device_t * p_device = &g_plan.devices[index];
        assert(0 == drvreg_bind(&registry,
                                "ssd1306_spi",
                                p_names[index],
                                &g_instances[index],
                                sizeof(g_instances[index]),
                                &p_device->component));
        memcpy(p_device->config, &g_config, sizeof(g_config));
        p_device->config_size           = sizeof(g_config);
        p_device->task.priority         = 16U;
        p_device->task.period_ms        = 250U;
        p_device->task.stack_size_bytes = 3072U;
        p_device->worker.p_step         = oledinst_step;
        p_device->worker.p_cleanup      = oledinst_cleanup;
        p_device->worker.p_context      = &g_instances[index];
        p_device->heartbeat_ms          = 1000U;
        p_device->b_enabled             = true;
    }
}

/** @brief Display errors retry, and failed teardown retains activation
 * ownership. */
static void verify_plan (void)
{
    bind_plan();
    assert(0U == g_fake.transfers);
    assert(0 == activate_apply(&g_plan));
    uint8_t frame[UTHOME_SSD1306_BYTES] = {0};
    frame[0]                            = 0x55U;
    assert(0 ==
           component_write(&g_plan.devices[0].component, frame, sizeof(frame)));
    frame[0] = 0U;
    assert(0x55U == g_instances[0].display.frame[0]);
    assert(0U == g_instances[1].display.frame[0]);
    g_fake.fail_at = g_fake.transfers + 1U;
    assert(0 == oledinst_step(&g_instances[0]));
    assert(SPIBUS_IO == g_instances[0].last_io_error);
    assert(g_instances[0].display.b_dirty);
    health_snapshot_t health = {0};
    assert(0 == shared_snapshot(&health));
    assert(1U == health.event_count[UTHOME_HEALTH_DEVICE_IO]);
    assert(0 == oledinst_step(&g_instances[0]));
    assert(!g_instances[0].display.b_dirty);
    g_data[0].b_fail = true;
    assert(UTHOME_ACTIVATE_RECOVERY == activate_apply(&g_plan));
    activate_status_t status = {0};
    assert(0 == activate_get_status(&status));
    assert((ACTIVATE_NEEDS_RECOVERY == status.state) &&
           (1U == status.retained_instances));
    g_data[0].b_fail = false;
    assert(0 == activate_recover());
    assert(0 == activate_apply(&g_plan));
    g_plan.devices[1].b_enabled = false;
    assert(0 == activate_apply(&g_plan));
    assert(!g_instances[1].display.b_claimed);
    const activate_plan_t empty = {0};
    assert(0 == activate_apply(&empty));
    for (size_t index = 0U; index < 2U; ++index)
    {
        assert(0 == spibus_detach(&g_devices[index]));
    }
    assert(!spibus_has_devices(&g_fake.bus));
}

/** @brief Check readable glyphs, quality clearing and bounded numeric cases. */
static void verify_view (ssd1306_t * p_display)
{
    const unsigned    transfers = g_fake.transfers;
    sampcache_entry_t entry     = {.b_expected = true, .b_seen = true};
    memcpy(entry.source.name, "range", 6U);
    entry.source.type                = UTHOME_SAMPLE_DISTANCE;
    entry.source.stale_ms            = 100U;
    entry.header.status              = UTHOME_SAMPLE_OK;
    entry.value.distance.distance_mm = UINT32_MAX;
    assert(0 == oledview_render(p_display, &entry, 0U));
    assert(0x7FU == p_display->frame[128]); /* F in FRESH. */
    assert(0x7FU == p_display->frame[256]); /* D in DIST. */
    assert(0 == oledview_render(p_display, &entry, 100U));
    assert(0x46U == p_display->frame[128]); /* S in STALE. */
    entry.header.status = UTHOME_SAMPLE_TIMEOUT;
    assert(0 == oledview_render(p_display, &entry, 100U));
    assert(0x7FU == p_display->frame[128]); /* F in FAULT. */
    entry.b_seen = false;
    assert(0 == oledview_render(p_display, &entry, 100U));
    for (size_t index = 256U; index < UTHOME_SSD1306_BYTES; ++index)
    {
        assert(0U == p_display->frame[index]);
    }
    entry.b_seen                          = true;
    entry.header.status                   = UTHOME_SAMPLE_OK;
    entry.source.type                     = UTHOME_SAMPLE_ENVIRONMENT;
    entry.value.environment.pressure_pa   = 100000.0F;
    entry.value.environment.temperature_c = -0.5F;
    assert(0 == oledview_render(p_display, &entry, 0U));
    assert(0x08U == p_display->frame[384U + 12U]); /* Minus in T -0.5 C. */
    entry.value.environment.pressure_pa = NAN;
    assert(0 == oledview_render(p_display, &entry, 0U));
    assert(0x3EU == p_display->frame[256]); /* O in OUT OF DISPLAY RANGE. */
    entry.value       = (sampcache_value_t){0};
    entry.source.type = UTHOME_SAMPLE_TARGETS;
    entry.value.targets.targets[2] =
        (sample_target_t){.b_present     = true,
                          .x_mm          = INT16_MIN,
                          .y_mm          = INT16_MAX,
                          .speed_cm_s    = INT16_MIN,
                          .resolution_mm = UINT16_MAX};
    assert(0 == oledview_render(p_display, &entry, 0U));
    assert(0U != p_display->frame[896]); /* Last target's velocity row. */
    entry.source.type              = UTHOME_SAMPLE_HEALTH;
    entry.value                    = (sampcache_value_t){0};
    entry.value.health.occurrences = UINT32_MAX;
    assert(0 == oledview_render(p_display, &entry, 0U));
    assert(0x7FU == p_display->frame[512]); /* L in LAST EVENT REPORT. */
    assert(0 == oledview_render(p_display, NULL, 0U));
    assert(transfers == g_fake.transfers); /* Renderer never performs I/O. */
    assert(0 == ssd1306_clear(p_display));
}

/* End of file. */

