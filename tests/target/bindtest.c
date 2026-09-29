/**
 * @file bindtest.c
 * @brief Test admission, shared bus lifetime, actual driver binding and
 * recovery.
 */
#ifdef NDEBUG
#error "Binding tests require assertions."
#endif
#include "bindtest.h"
#include "cfgbind.h"
#include "cfgjson.h"
#include "i2cfake.h"
#include "uartfake.h"
#include "gpiofake.h"
#include "spifake.h"
#include <stdlib.h>
#include "shared.h"
#include "uthome/sample.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static gpiofake_t      g_outputs[4] = {0};
static cfgbind_state_t g_binding    = {0};
static cfgctrl_t       g_control    = {0};
static cfgctrl_t       g_boot       = {0};
static cfgstore_t      g_store      = {0};
static i2cfake_t       g_i2c        = {0};
static uartfake_t      g_uart       = {0};
static gpiofake_t      g_pulses[2]  = {0};
static pinpool_t       g_pins       = {.allowed = UINT32_C(0x4FB)};
static uint8_t         g_flash[2][UTHOME_CFGSTORE_MIN_SLOT] = {{0}};
static unsigned        g_opens                              = 0U;
static unsigned        g_closes                             = 0U;
static unsigned        g_writes                             = 0U;
static int             g_close_error                        = 0;
static int             g_open_error                         = 0;
static int             g_store_error                        = 0;
static spifake_t       g_spi                                = {0};
static spifake_line_t  g_lines[8]                           = {0};
static bool            gb_lines_open[8]                     = {false};
static uint8_t         g_line_pins[8]                       = {0};
static bool            gb_spi_open                          = false;
static bool            gb_line_close_fail                   = false;
static int             g_fail_line                          = -1;
static char            g_display_profile[8193]              = {0};
static int open_spi (uint8_t sclk, uint8_t mosi, spibus_t ** pp_bus);
static int close_spi (spibus_t * p_bus);
static int
open_line (uint8_t slot, uint8_t pin, bool b_initial, digiout_t * p_output);
static int close_line (digiout_t * p_output);
static void verify_display_binding (void);
static void recover_display (void);
static uint32_t g_request = 0U;
static int open_i2c (const i2cbus_config_t * p_config, i2cbus_t ** pp_bus);
static int close_i2c (i2cbus_t * p_bus);
static uartio_t * uart (void);
static pulseio_t * pulse (uint8_t pair);
static pwmio_t * pwm (uint8_t channel);
static void load_profile (const char * p_variable);
static void verify_output_binding (uint64_t * p_clock);
static int read_slot (void *    p_context,
                      unsigned  slot,
                      size_t    offset,
                      uint8_t * p_data,
                      size_t    size);
static int write_slot (void *          p_context,
                       unsigned        slot,
                       size_t          offset,
                       const uint8_t * p_data,
                       size_t          size);
static int erase_slot (void * p_context, unsigned slot);
static int sync_slot (void * p_context);
static void
request (const char * p_text, uint32_t revision, cfgctrl_status_t status);
static void verify_sensor_changes (void);
static void verify_failures (void);
static const cfgbind_io_t   g_io      = {.p_i2c_open   = open_i2c,
                                         .p_i2c_close  = close_i2c,
                                         .p_uart       = uart,
                                         .p_pulse      = pulse,
                                         .p_pwm        = pwm,
                                         .p_spi_open   = open_spi,
                                         .p_spi_close  = close_spi,
                                         .p_line_open  = open_line,
                                         .p_line_close = close_line};
static const cfgstore_ops_t g_storage = {
    read_slot, write_slot, erase_slot, sync_slot};
static const char g_distance[] =
    "{\"schema_version\":\"2.0.0\",\"node_id\":\"distance-only\",\"board\":{"
    "\"pl"
    "atform\":\"esp32-c3\",\"variant\":\"esp32-c3-supermini\",\"capability_ve"
    "rsion\":\"1.0.0\"},\"runtime\":{\"health_report_interval_ms\":5000,\"max"
    "_device_tasks\":4,\"queue_depth\":8,\"configuration_source\":\"usb_ser"
    "ial\",\"rollback_on_error\":true},\"interfaces\":[],\"queues\":[{\"id\":\""
    "distance.front\",\"item_type\":\"distance_sample\",\"depth\":8,\"overflo"
    "w_policy\":\"drop_oldest\"}],\"devices\":[{\"device_id\":\"front_range\","
    "\"driver\":\"hc_sr04\",\"enabled\":true,\"interface\":{\"protocol\":"
    "\"gpio\""
    ",\"trigger_gpio\":4,\"echo_gpio\":5,\"echo_voltage_divider_required\":"
    "true},\"behaviour\":{\"profile\":\"periodic_sensor\",\"operation\":\"meas"
    "ure_distance\",\"interval_ms\":1000,\"timeout_us\":30000,\"destination"
    "\":\"distance.front\"},\"task\":{\"name\":\"hc_sr04_reader\",\"priority\":"
    "1"
    "0,\"stack_size_bytes\":3072},\"descriptor\":{\"id\":\"hc_sr04\",\"version"
    "\":\"1.0.0\"}}]}";
static const char g_environment[] =
    "{\"schema_version\":\"2.0.0\",\"node_id\":\"two-environment-sensors\",\"b"
    "oard\":{\"platform\":\"esp32-c3\",\"variant\":\"esp32-c3-supermini\",\"cap"
    "ability_version\":\"1.0.0\"},\"runtime\":{\"health_report_interval_ms\""
    ":1000,\"max_device_tasks\":4,\"queue_depth\":8,\"configuration_source"
    "\":\"usb_serial\",\"rollback_on_error\":true},\"interfaces\":[{\"id\":\"i2"
    "c0\",\"protocol\":\"i2c\",\"port\":0,\"sda_gpio\":0,\"scl_gpio\":1,"
    "\"clock_h"
    "z\":400000}],\"queues\":[{\"id\":\"zone.a\",\"item_type\":\"environment_sa"
    "mple\",\"depth\":4,\"overflow_policy\":\"drop_oldest\"},{\"id\":\"zone."
    "b\","
    "\"item_type\":\"environment_sample\",\"depth\":4,\"overflow_policy\":\"dr"
    "op_oldest\"}],\"devices\":[{\"device_id\":\"room_1\",\"driver\":"
    "\"bmp388\","
    "\"enabled\":true,\"interface\":{\"ref\":\"i2c0\",\"address\":\"0x76\"},"
    "\"beha"
    "viour\":{\"profile\":\"periodic_sensor\",\"operation\":\"read\",\"interval"
    "_ms\":500,\"timeout_ms\":20,\"destination\":\"zone.a\"},\"task\":{"
    "\"name\":"
    "\"reader_1\",\"priority\":11,\"stack_size_bytes\":3072},\"descriptor\":{"
    "\"id\":\"bmp388\",\"version\":\"1.0.0\"}},{\"device_id\":\"room_2\","
    "\"driver\""
    ":\"bmp388\",\"enabled\":true,\"interface\":{\"ref\":\"i2c0\",\"address\":"
    "\"0x"
    "77\"},\"behaviour\":{\"profile\":\"periodic_sensor\",\"operation\":"
    "\"read\""
    ",\"interval_ms\":5000,\"timeout_ms\":20,\"destination\":\"zone.b\"},\"tas"
    "k\":{\"name\":\"reader_2\",\"priority\":12,\"stack_size_bytes\":3072},\"de"
    "scriptor\":{\"id\":\"bmp388\",\"version\":\"1.0.0\"}}]}";
static const char g_radar[] =
    "{\"schema_version\":\"2.0.0\",\"node_id\":\"prototype-node-01\",\"board\":"
    "{\"platform\":\"esp32-c3\",\"variant\":\"esp32-c3-supermini\",\"capabilit"
    "y_version\":\"1.0.0\"},\"runtime\":{\"health_report_interval_ms\":5000,"
    "\"max_device_tasks\":4,\"queue_depth\":8,\"configuration_source\":\"usb"
    "_serial\",\"rollback_on_error\":true},\"interfaces\":[{\"id\":\"uart1\",\""
    "protocol\":\"uart\",\"port\":1,\"rx_gpio\":20,\"tx_gpio\":21,\"baud_"
    "rate\":"
    "256000,\"data_bits\":8,\"parity\":\"none\",\"stop_bits\":1,\"rx_buffer_by"
    "tes\":512}],\"queues\":[{\"id\":\"telemetry.environment\",\"item_type\":\""
    "environment_sample\",\"depth\":8,\"overflow_policy\":\"drop_oldest\"},{"
    "\"id\":\"presence.targets\",\"item_type\":\"target_report\",\"depth\":8,"
    "\"o"
    "verflow_policy\":\"drop_oldest\"},{\"id\":\"distance.front\",\"item_type"
    "\":\"distance_sample\",\"depth\":8,\"overflow_policy\":\"drop_oldest\"},{"
    "\"id\":\"system.health\",\"item_type\":\"health_event\",\"depth\":8,"
    "\"overf"
    "low_policy\":\"drop_oldest\"}],\"devices\":[{\"device_id\":\"presence_se"
    "nsor\",\"driver\":\"hlk_ld2450\",\"enabled\":true,\"interface\":{\"ref\":"
    "\"u"
    "art1\"},\"behaviour\":{\"profile\":\"stream_sensor\",\"operation\":\"read_"
    "frames\",\"timeout_ms\":250,\"frame\":{\"header_hex\":\"AAFF0300\",\"foote"
    "r_hex\":\"55CC\",\"size_bytes\":30,\"report_rate_hz\":10},\"destination\""
    ":\"presence.targets\"},\"task\":{\"name\":\"ld2450_reader\",\"priority\":8"
    ",\"stack_size_bytes\":4096},\"descriptor\":{\"id\":\"hlk_ld2450\",\"versi"
    "on\":\"1.0.0\"}}]}";

/** @brief Exercise the same callbacks intended for the USB control task. */
void bindtest_run (uint64_t * p_clock)
{
    assert(0 == cfgjson_init());
    assert(0 == activate_clear());
    assert(0 == cfgbind_init(&g_binding, &g_io));
    assert(-1 == cfgbind_init(&g_binding, &g_io));
    assert(31U == cfgbind_mask());
    uint8_t frame[376] = {0};
    size_t  size       = 0U;
    assert(0 == cfgbind_capabilities(NULL, &frame[24], 352U, &size));
    const cfgwire_request_t capabilities = {.command    = CFGWIRE_CAPABILITIES,
                                            .request_id = 27U};
    assert(0 == cfgwire_observation(&capabilities, frame, sizeof(frame), size));
    printf("CAPABILITIES:");
    for (size_t index = 0U; index < (24U + size); ++index)
    {
        /* Byte promotion to unsigned matches printf's hexadecimal format. */
        printf("%02X", (unsigned)frame[index]);
    }
    puts("");
    assert((0U == g_opens) && (0U == g_writes));
    memset(g_flash, 0xFF, sizeof(g_flash));
    g_store = (cfgstore_t){.p_operations = &g_storage,
                           .p_context    = g_flash,
                           .slot_bytes   = UTHOME_CFGSTORE_MIN_SLOT};
    assert(0 == cfgctrl_init(
                    &g_control, &g_store, cfgbind_operations(), &g_binding));
    assert(CFGCTRL_EMPTY == cfgctrl_restore(&g_control));
    uartfake_init(&g_uart);
    gpiofake_init(&g_pulses[0], &g_pins);
    gpiofake_init(&g_pulses[1], &g_pins);
    for (size_t index = 0U; index < 4U; ++index)
    {
        gpiofake_init(&g_outputs[index], &g_pins);
    }
    verify_sensor_changes();
    verify_failures();
    verify_display_binding();
    verify_output_binding(p_clock);
    assert(0 == activate_clear());
    assert(NULL == g_binding.p_i2c);
    puts("Production admission/binding, shared resources, driver replacement "
         "and safe-empty recovery passed.");
}

/** @brief Assert admission performs no I/O and retries never reactivate. */
static void
request (const char * p_text, uint32_t revision, cfgctrl_status_t status)
{
    const cfgwire_request_t request  = {.request_id = ++g_request,
                                        .revision   = revision,
                                        .command    = CFGWIRE_APPLY,
                                        .p_data     = (const uint8_t *)p_text,
                                        .size       = strlen(p_text)};
    uint32_t                reply[6] = {0};
    assert(0 == cfgctrl_handle(&g_control, &request, reply));
    assert((uint32_t)status ==
           reply[0]); /* Named enum is a nonnegative wire value. */
    const unsigned opens  = g_opens;
    const unsigned writes = g_writes;
    assert(0 == cfgctrl_handle(&g_control, &request, reply));
    assert((uint32_t)status == reply[0]);
    assert((opens == g_opens) && (writes == g_writes));
}

/** @brief One binary replaces two I2C instances with GPIO and UART profiles. */
static void verify_sensor_changes (void)
{
    const cfgctrl_ops_t * p_operations = cfgbind_operations();
    assert(0 == p_operations->p_validate(&g_binding,
                                         (const uint8_t *)g_environment,
                                         strlen(g_environment)));
    assert(0U == g_opens);
    request(g_environment, 1U, CFGCTRL_SAVED);
    assert((1U == g_opens) && (0U == g_closes));
    shared_capture_t capture = {0};
    assert(0 == shared_capture(&capture));
    assert(capture.samples.entries[0].b_expected);
    assert(capture.samples.entries[1].b_expected);
    assert(!capture.samples.entries[0].b_seen);
    assert(g_binding.slots[0].context.environment.p_bus ==
           g_binding.slots[1].context.environment.p_bus);
    assert(0 == bmpinst_step(&g_binding.slots[0].context.environment));
    sample_environment_t sample = {0};
    assert(0 == shared_consume(
                    0, UTHOME_SAMPLE_ENVIRONMENT, &sample, sizeof(sample)));
    assert((100000.0F == sample.pressure_pa));
    const unsigned writes = g_writes;
    request("{}", 2U, CFGCTRL_REJECTED);
    assert((writes == g_writes) && (1U == g_opens) && (0U == g_closes));
    request(g_distance, 2U, CFGCTRL_SAVED);
    assert((1U == g_closes) && (NULL == g_binding.p_i2c));
    assert(0 == hcinst_step(&g_binding.slots[0].context.distance));
    sample_distance_t distance = {0};
    assert(0 == shared_consume(
                    0, UTHOME_SAMPLE_DISTANCE, &distance, sizeof(distance)));
    assert(1000U == distance.distance_mm);
    request(g_radar, 3U, CFGCTRL_SAVED);
    assert(NULL == g_pulses[0].pulse.p_owner);
    assert(g_uart.b_open);
    assert(0 == shared_capture(&capture));
    assert(0 ==
           strcmp(capture.samples.entries[0].source.name, "presence_sensor"));
    assert(!capture.samples.entries[1].b_expected);
    assert(!capture.samples.entries[0].b_seen);
    assert(256000U == g_uart.config.baud);
    assert(0 == ldinst_step(&g_binding.slots[0].context.radar));
}

/** @brief Check rollback, uncertain storage and non-restarting cleanup. */
static void verify_failures (void)
{
    g_store_error = -1;
    /* Storage is checked before activation; failure cannot change hardware. */
    request(g_distance, 4U, CFGCTRL_STORAGE);
    assert(g_uart.b_open);
    g_store_error = 0;
    g_open_error  = -1;
    request(g_environment, 4U, CFGCTRL_REJECTED);
    assert(g_uart.b_open);
    assert(NULL == g_binding.p_i2c);
    g_open_error           = 0;
    g_pulses[0].open_error = PULSEIO_IO;
    request(g_distance, 4U, CFGCTRL_REJECTED);
    assert(
        g_uart.b_open); /* Previous UART profile was actually reconstructed. */
    assert(0 == strcmp(g_binding.slots[0].component.name, "presence_sensor"));
    shared_capture_t capture = {0};
    assert(0 == shared_capture(&capture));
    assert(0 ==
           strcmp(capture.samples.entries[0].source.name, "presence_sensor"));
    assert(UTHOME_SAMPLE_TARGETS == capture.samples.entries[0].source.type);
    assert(g_binding.monitor.b_ready);
    assert(5000U == g_binding.monitor.interval_ms);
    g_pulses[0].open_error = 0;
    request(g_environment, 4U, CFGCTRL_SAVED);
    g_close_error = -1;
    request(g_distance, 5U, CFGCTRL_RECOVERY);
    activate_status_t state = {0};
    assert(0 == activate_get_status(&state));
    assert(ACTIVATE_NEEDS_RECOVERY == state.state);
    assert(state.b_resources_retained);
    assert(NULL != g_binding.p_i2c);
    const unsigned    opens    = g_opens;
    cfgwire_request_t command  = {.command    = CFGWIRE_RECOVER,
                                  .request_id = ++g_request};
    uint32_t          reply[6] = {0};
    assert(0 == cfgctrl_handle(&g_control, &command, reply));
    assert(CFGCTRL_RECOVERY == reply[0]);
    g_close_error      = 0;
    command.request_id = ++g_request;
    assert(0 == cfgctrl_handle(&g_control, &command, reply));
    assert(CFGCTRL_EMPTY == reply[0]);
    assert(opens ==
           g_opens); /* No old sensor silently restarts during recovery. */
    assert(0 == activate_get_status(&state));
    assert((ACTIVATE_EMPTY == state.state) && (!state.b_resources_retained) &&
           (0U == state.retained_instances));
    request(g_distance, 5U, CFGCTRL_SAVED);
    assert(0 == activate_clear());
    assert(0 ==
           cfgctrl_init(&g_boot, &g_store, cfgbind_operations(), &g_binding));
    assert(CFGCTRL_SAVED == cfgctrl_restore(&g_boot));
    assert(NULL != g_pulses[0].pulse.p_owner);
    assert(0 == activate_clear());
}

/** @brief Enforce the C3's zero-worker shared-bus opening boundary. */
static int open_i2c (const i2cbus_config_t * p_config, i2cbus_t ** pp_bus)
{
    size_t count = 99U;
    assert(0 == rt_port_owned_count(&count));
    assert(0U == count);
    assert((0U == p_config->port) && (0U == p_config->sda_gpio) &&
           (1U == p_config->scl_gpio));
    i2cfake_init(&g_i2c);
    i2cfake_sensor(&g_i2c, 0U, 12500U);
    i2cfake_sensor(&g_i2c, 1U, 13750U);
    *pp_bus = &g_i2c.bus;
    ++g_opens;
    return g_open_error;
}

/** @brief Ensure resources are retained until every worker/address is freed. */
static int close_i2c (i2cbus_t * p_bus)
{
    size_t count = 99U;
    assert(0 == rt_port_owned_count(&count));
    assert((0U == count) && (&g_i2c.bus == p_bus) &&
           (!i2cbus_has_claims(p_bus)));
    if (0 != g_close_error)
    {
        return g_close_error;
    }
    ++g_closes;
    return 0;
}

/** @brief Borrow the fake native UART without doing I/O. */
static uartio_t * uart (void)
{
    return &g_uart.port;
}

/** @brief Borrow one of the same two pulse slots offered by C3. */
static pulseio_t * pulse (uint8_t pair)
{
    return (pair < 2U) ? &g_pulses[pair].pulse : NULL;
}

/** @brief Read an exact bounded storage range. */
static int read_slot (void *    p_context,
                      unsigned  slot,
                      size_t    offset,
                      uint8_t * p_data,
                      size_t    size)
{
    assert((p_context == g_flash) && (slot < 2U) &&
           ((offset + size) <= UTHOME_CFGSTORE_MIN_SLOT));
    memcpy(p_data, &g_flash[slot][offset], size);
    return 0;
}

/** @brief Model NOR bit clearing and count persistence side effects. */
static int write_slot (void *          p_context,
                       unsigned        slot,
                       size_t          offset,
                       const uint8_t * p_data,
                       size_t          size)
{
    assert((p_context == g_flash) && (slot < 2U) &&
           ((offset + size) <= UTHOME_CFGSTORE_MIN_SLOT));
    for (size_t index = 0U; index < size; ++index)
    {
        g_flash[slot][offset + index] &= p_data[index];
    }
    ++g_writes;
    return 0;
}

/** @brief Erase only the requested independent fake slot. */
static int erase_slot (void * p_context, unsigned slot)
{
    assert((p_context == g_flash) && (slot < 2U));
    memset(g_flash[slot], 0xFF, sizeof(g_flash[slot]));
    return 0;
}

/** @brief Inject failure before any profile is allowed to replace hardware. */
static int sync_slot (void * p_context)
{
    assert(p_context == g_flash);
    return g_store_error;
}

/** @brief Use a small generated fixture instead of copying another JSON blob.
 */
static void verify_display_binding (void)
{
    load_profile("UTHOME_DISPLAY_PROFILE");
    request(g_display_profile, 6U, CFGCTRL_SAVED);
    assert((gb_spi_open) && (gb_lines_open[0]) && (gb_lines_open[1]));
    assert(0 == hcinst_step(&g_binding.slots[0].context.distance));
    const rt_port_worker_t * p_worker = &g_binding.plan.devices[1].worker;
    assert(0 == p_worker->p_step(p_worker->p_context));
    cfgbind_display_t * p_display = &g_binding.slots[1].context.display;
    assert((1U == p_display->sources) &&
           (!p_display->instance.display.b_dirty));
    assert(0U != p_display->instance.display.frame[256]);
    msgbus_stats_t stats = {0};
    assert(0 == shared_queue_stats(0, &stats));
    assert((1U == stats.current_depth) && (0U == stats.consumed_count));
    g_spi.fail_at = g_spi.transfers + 1U;
    assert(0 == p_worker->p_step(p_worker->p_context));
    assert(p_display->instance.display.b_dirty);
    assert(SPIBUS_IO == p_display->instance.last_io_error);
    assert(0 == p_worker->p_step(p_worker->p_context));
    assert(!p_display->instance.display.b_dirty);
    /* Failed release keeps the live handle for explicit recovery. */
    gb_line_close_fail = true;
    request(g_distance, 7U, CFGCTRL_RECOVERY);
    assert((gb_spi_open) && (gb_lines_open[1]));
    gb_line_close_fail = false;
    recover_display();
    /* Failure after SPI and D/C acquisition must release both before reject. */
    g_fail_line = 1;
    request(g_display_profile, 7U, CFGCTRL_REJECTED);
    assert((!gb_spi_open) && (!gb_lines_open[0]) && (!gb_lines_open[1]));
    g_fail_line = -1;
    request(g_display_profile, 7U, CFGCTRL_SAVED);
    g_spi.b_detach_fail = true;
    request(g_distance, 8U, CFGCTRL_RECOVERY);
    assert((gb_spi_open) && (spibus_has_devices(&g_spi.bus)));
    assert((!gb_lines_open[0]) && (!gb_lines_open[1]));
    g_spi.b_detach_fail = false;
    recover_display();
    request(g_display_profile, 8U, CFGCTRL_SAVED);
    request(g_distance, 9U, CFGCTRL_SAVED);
    assert((!gb_spi_open) && (NULL == g_binding.p_spi));
    assert(0 == activate_clear());
}

/** @brief Verify safe-empty recovery, with no display or sensor restarted. */
static void recover_display (void)
{
    const cfgwire_request_t request  = {.command    = CFGWIRE_RECOVER,
                                        .request_id = ++g_request};
    uint32_t                reply[6] = {0};
    assert(0 == cfgctrl_handle(&g_control, &request, reply));
    assert(CFGCTRL_EMPTY == reply[0]);
    assert((!gb_spi_open) && (NULL == g_binding.p_spi));
    assert((!gb_lines_open[0]) && (!gb_lines_open[1]));
}

/** @brief Borrow the existing SPI fake, checking whole-plan bus lifetime. */
static int open_spi (uint8_t sclk, uint8_t mosi, spibus_t ** pp_bus)
{
    assert((7U == sclk) && (6U == mosi) && (!gb_spi_open));
    size_t owned = 99U;
    assert((0 == rt_port_owned_count(&owned)) && (0U == owned));
    spifake_init(&g_spi);
    g_spi.p_data_lines[0] = &g_lines[0];
    *pp_bus               = &g_spi.bus;
    gb_spi_open           = true;
    return 0;
}

/** @brief Refuse bus release until attached display devices are detached. */
static int close_spi (spibus_t * p_bus)
{
    assert((&g_spi.bus == p_bus) && (gb_spi_open));
    assert(!spibus_has_devices(p_bus));
    gb_spi_open = false;
    return 0;
}

/** @brief Model independent output ownership and partial acquisition failure.
 */
static int
open_line (uint8_t slot, uint8_t pin, bool b_initial, digiout_t * p_output)
{
    assert((slot < 8U) && (!gb_lines_open[slot]));
    if (g_fail_line == slot)
    {
        return -1;
    }
    assert(0 == pinpool_claim(&g_pins, UINT32_C(1) << pin, &g_lines[slot]));
    g_lines[slot]       = (spifake_line_t){.b_high = b_initial};
    g_line_pins[slot]   = pin;
    gb_lines_open[slot] = true;
    *p_output = (digiout_t){.p_set = spifake_set, .p_context = &g_lines[slot]};
    return 0;
}

/** @brief Keep the output handle intact on failure so cleanup can retry. */
static int close_line (digiout_t * p_output)
{
    if (gb_line_close_fail)
    {
        return -1;
    }
    for (size_t index = 0U; index < 8U; ++index)
    {
        if (&g_lines[index] == p_output->p_context)
        {
            assert(gb_lines_open[index]);
            assert(0 == pinpool_release(&g_pins,
                                        UINT32_C(1) << g_line_pins[index],
                                        &g_lines[index]));
            gb_lines_open[index] = false;
            *p_output            = (digiout_t){0};
            return 0;
        }
    }
    return -1;
}

/** @brief Reuse a bounded external JSON fixture without embedding duplicates.
 */
static void load_profile (const char * p_variable)
{
    const char * p_path = getenv(p_variable);
    assert(NULL != p_path);
    FILE * p_file = fopen(p_path, "rb");
    assert(NULL != p_file);
    memset(g_display_profile, 0, sizeof(g_display_profile));
    const size_t size =
        fread(g_display_profile, 1U, sizeof(g_display_profile), p_file);
    assert((size < sizeof(g_display_profile)) && (0 == ferror(p_file)));
    assert(0 == fclose(p_file));
}

/** @brief Borrow an independent PWM port without opening hardware. */
static pwmio_t * pwm (uint8_t channel)
{
    return (channel < 4U) ? &g_outputs[channel].pwm : NULL;
}

/** @brief Exercise real routing, cache sharing, stale shutdown and recovery. */
static void verify_output_binding (uint64_t * p_clock)
{
    load_profile("UTHOME_RULE_PROFILE");
    request(g_display_profile, 10U, CFGCTRL_SAVED);
    const rt_port_worker_t * p_worker = &g_binding.plan.devices[1].worker;
    assert(0U == g_outputs[0].duty_permille);
    g_pulses[0].high_us = 1000U;
    g_pulses[0].now_ms  = *p_clock;
    assert(0 == hcinst_step(&g_binding.slots[0].context.distance));
    assert(0 == p_worker->p_step(p_worker->p_context));
    assert(500U == g_outputs[0].duty_permille);
    const rt_port_worker_t * p_display = &g_binding.plan.devices[2].worker;
    assert(0 == p_display->p_step(p_display->p_context));
    msgbus_stats_t stats = {0};
    assert(0 == shared_queue_stats(0, &stats));
    assert((1U == stats.current_depth) && (0U == stats.consumed_count));
    *p_clock += 500U;
    assert(0 == p_worker->p_step(p_worker->p_context));
    assert(0U == g_outputs[0].duty_permille);
    g_pulses[0].now_ms = *p_clock;
    assert(0 == hcinst_step(&g_binding.slots[0].context.distance));
    assert(0 == p_worker->p_step(p_worker->p_context));
    assert(0U == g_outputs[0].duty_permille);
    *p_clock += 200U;
    g_pulses[0].now_ms = *p_clock;
    assert(0 == hcinst_step(&g_binding.slots[0].context.distance));
    assert(0 == p_worker->p_step(p_worker->p_context));
    assert(500U == g_outputs[0].duty_permille);
    g_outputs[0].set_error = PWMIO_IO;
    *p_clock += 500U;
    assert(0 > p_worker->p_step(p_worker->p_context));
    assert(NULL != g_outputs[0].pwm.p_owner);
    g_outputs[0].set_error = 0;
    assert(0 == p_worker->p_step(p_worker->p_context));
    assert(0U == g_outputs[0].duty_permille);
    g_outputs[0].close_error = PWMIO_IO;
    request(g_distance, 11U, CFGCTRL_RECOVERY);
    assert((0U == g_outputs[0].duty_permille) &&
           (NULL != g_outputs[0].pwm.p_owner));
    g_outputs[0].close_error = 0;
    recover_display();
    assert(NULL == g_outputs[0].pwm.p_owner);
    request(g_distance, 11U, CFGCTRL_SAVED);
    g_outputs[0].open_error = PWMIO_IO;
    request(g_display_profile, 12U, CFGCTRL_REJECTED);
    assert((NULL == g_outputs[0].pwm.p_owner) &&
           (NULL != g_pulses[0].pulse.p_owner));
    assert(0 == strcmp(g_binding.slots[0].component.name, "front_range"));
    g_outputs[0].open_error = 0;
    load_profile("UTHOME_ALT_RULE_PROFILE");
    request(g_display_profile, 12U, CFGCTRL_SAVED);
    assert(0U == g_outputs[0].duty_permille);
    g_i2c.now_ms = *p_clock;
    assert(0 == bmpinst_step(&g_binding.slots[0].context.environment));
    assert(0 == p_worker->p_step(p_worker->p_context));
    assert(200U == g_outputs[0].duty_permille);
    assert(0 == activate_clear());
    assert((0U == g_outputs[0].duty_permille) &&
           (NULL == g_outputs[0].pwm.p_owner));
}

/* End of file. */

