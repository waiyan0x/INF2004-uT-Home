/**
 * @file disctest.c
 * @brief Test the generic observer and production USB/controller integration.
 */
#ifdef NDEBUG
#error "Discovery tests require assertions."
#endif
#include "disctest.h"
#include "cfgdisc.h"
#include "uartdisc.h"
#include "cfgpump.h"
#include "i2cfake.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static i2cfake_t       g_fake        = {0};
static cfgbind_state_t g_binding     = {0};
static cfgbind_io_t    g_io          = {0};
static cfgctrl_t       g_control     = {0};
static cfgdisc_t       g_discovery   = {0};
static cfgpump_t       g_pump        = {0};
static unsigned        g_opens       = 0U;
static unsigned        g_closes      = 0U;
static bool            gb_close_fail = false;
static bool            gb_open_fail  = false;
static uint8_t         g_input[48]   = {0};
static uint8_t         g_output[160] = {0};
static size_t          g_received    = 0U;
static size_t          g_sent        = 0U;
static int open_bus (const i2cbus_config_t * p_config, i2cbus_t ** pp_bus);
static int close_bus (i2cbus_t * p_bus);
static int now_ms (uint64_t * p_now);
static void verify_core (void);
static void verify_budget (void);
static void verify_rejections (cfgwire_request_t * p_request);
static void verify_recovery (cfgwire_request_t * p_request);
static void verify_transport (const cfgwire_request_t * p_request);
static int
receive (void * p_context, uint8_t * p_data, size_t capacity, size_t * p_count);
static int transmit (void *          p_context,
                     const uint8_t * p_data,
                     size_t          size,
                     size_t *        p_count);
static const cfgpump_ops_t   g_transport = {receive, transmit};
static const i2cbus_config_t g_config    = {0U, 0U, 1U, 100000U};
static const disci2c_read_t  g_reads[2]  = {{0x76U, 0U}, {0x77U, 0U}};

/** @brief Use real control/binding code with only hardware callbacks replaced.
 */
void disctest_run (const cfgbind_io_t * p_io, cfgstore_t * p_store)
{
    uartdisc_run();
    verify_core();
    verify_budget();
    i2cfake_init(&g_fake);
    i2cfake_sensor(&g_fake, 0U, 12500U);
    i2cfake_sensor(&g_fake, 1U, 12500U);
    g_fake.registers[1][0] = 0x60U;
    g_io                   = *p_io;
    g_io.p_i2c_open        = open_bus;
    g_io.p_i2c_close       = close_bus;
    assert(0 == cfgbind_init(&g_binding, &g_io));
    uint8_t hash[32] = {1U};
    assert(0 == cfgdisc_init(
                    &g_discovery, &g_control, &g_binding, hash, uartdisc_wait));
    g_discovery.probe.p_now = now_ms;
    assert(0 == cfgctrl_init(
                    &g_control, p_store, cfgdisc_operations(), &g_discovery));
    uint8_t payload[24] = {0};
    cfgbytes_put(payload, 1U);
    cfgbytes_put(&payload[4], 1U);
    cfgbytes_put(&payload[8], 123U);
    cfgbytes_put(&payload[16], 1U);
    cfgwire_request_t request = {.command    = CFGWIRE_DISCOVER,
                                 .request_id = 27U,
                                 .size       = sizeof(payload),
                                 .p_data     = payload};
    verify_transport(&request);
    uartdisc_check(&g_discovery, &request);
    assert((0U == g_control.last_id) && (0U == g_control.active_revision));
    assert((0U == g_control.saved_revision) && (0U == g_control.cached_size));
    verify_rejections(&request);
    verify_recovery(&request);
    puts("Generic I2C discovery, bounded faults, USB parity and retained "
         "cleanup passed.");
}

/** @brief A fake backend retains an opened handle even when open fails. */
static int open_bus (const i2cbus_config_t * p_config, i2cbus_t ** pp_bus)
{
    assert((0U == p_config->port) && (0U == p_config->sda_gpio) &&
           (1U == p_config->scl_gpio) && (100000U == p_config->clock_hz));
    ++g_opens;
    *pp_bus = &g_fake.bus;
    return gb_open_fail ? I2CBUS_IO : 0;
}

/** @brief A failed close never clears a borrowed pointer or address claim. */
static int close_bus (i2cbus_t * p_bus)
{
    assert(&g_fake.bus == p_bus);
    assert(!i2cbus_has_claims(p_bus));
    ++g_closes;
    return gb_close_fail ? I2CBUS_IO : 0;
}

/** @brief Share the register fake's clock, including transfer timeouts. */
static int now_ms (uint64_t * p_now)
{
    *p_now = g_fake.now_ms;
    return 0;
}

/** @brief Distinguish raw values, no response, timeout, I/O and retained state.
 */
static void verify_core (void)
{
    disci2c_t context = {
        .p_open = open_bus, .p_close = close_bus, .p_now = now_ms};
    disci2c_result_data_t result = {0};
    i2cfake_init(&g_fake);
    i2cfake_sensor(&g_fake, 1U, 12500U);
    assert(0 == disci2c_run(&context, &g_config, g_reads, 2U, &result));
    assert((DISCI2C_COMPLETE == result.status) && (NULL == context.p_bus));
    assert((DISCI2C_NO_RESPONSE == result.records[0].status) &&
           (DISCI2C_RESPONDING == result.records[1].status));
    assert((0x50U == result.records[1].value) && (0U == g_fake.writes));
    g_fake.registers[1][0] = 0xA3U;
    assert(0 == disci2c_run(&context, &g_config, g_reads, 2U, &result));
    assert((DISCI2C_RESPONDING == result.records[1].status) &&
           (0xA3U == result.records[1].value));
    g_fake.transfer_ms = 100U;
    assert(0 == disci2c_run(&context, &g_config, g_reads, 2U, &result));
    assert((DISCI2C_INCOMPLETE == result.status) &&
           (100U == result.elapsed_ms));
    assert((DISCI2C_TIMED_OUT == result.records[0].status) &&
           (DISCI2C_NOT_CHECKED == result.records[1].status));
    assert((0U == result.records[0].value) && (NULL == context.p_bus));
    g_fake.transfer_ms = 0U;
    g_fake.fail_at     = g_fake.transfers + 1U;
    g_fake.error       = I2CBUS_IO;
    assert(0 == disci2c_run(&context, &g_config, g_reads, 2U, &result));
    assert((DISCI2C_INCOMPLETE == result.status) &&
           (DISCI2C_IO_ERROR == result.records[0].status));
    gb_close_fail = true;
    assert(0 == disci2c_run(&context, &g_config, g_reads, 2U, &result));
    assert((DISCI2C_FAULT == result.status) && (NULL != context.p_bus));
    const unsigned opens = g_opens;
    assert(0 == disci2c_run(&context, &g_config, g_reads, 2U, &result));
    assert((DISCI2C_BUSY == result.status) && (opens == g_opens));
    assert(0 != disci2c_release(&context));
    gb_close_fail = false;
    assert(0 == disci2c_release(&context));
    gb_open_fail = true;
    assert(0 == disci2c_run(&context, &g_config, g_reads, 2U, &result));
    assert((DISCI2C_FAULT == result.status) && (NULL == context.p_bus));
    gb_open_fail                      = false;
    const disci2c_read_t duplicate[2] = {{0x76U, 0U}, {0x76U, 1U}};
    const unsigned       before       = g_opens;
    assert(-1 == disci2c_run(&context, &g_config, duplicate, 2U, &result));
    assert(-1 == disci2c_run(&context, &g_config, g_reads, 9U, &result));
    assert(before == g_opens);
}

/** @brief Enforce the whole-request deadline independently of endpoint count.
 */
static void verify_budget (void)
{
    disci2c_t context = {
        .p_open = open_bus, .p_close = close_bus, .p_now = now_ms};
    disci2c_result_data_t result   = {0};
    const disci2c_read_t  reads[8] = {{0x70U, 0U},
                                      {0x71U, 0U},
                                      {0x72U, 0U},
                                      {0x73U, 0U},
                                      {0x74U, 0U},
                                      {0x75U, 0U},
                                      {0x76U, 0U},
                                      {0x77U, 0U}};
    i2cfake_init(&g_fake);
    assert(0 == disci2c_run(&context, &g_config, g_reads, 2U, &result));
    assert((DISCI2C_COMPLETE == result.status) &&
           (DISCI2C_NO_RESPONSE == result.records[0].status) &&
           (DISCI2C_NO_RESPONSE == result.records[1].status));
    g_fake.transfer_ms = 60U;
    assert(0 == disci2c_run(&context, &g_config, reads, 8U, &result));
    assert((DISCI2C_INCOMPLETE == result.status) &&
           (200U == result.elapsed_ms));
    assert((DISCI2C_TIMED_OUT == result.records[3].status) &&
           (DISCI2C_NOT_CHECKED == result.records[4].status));
    assert((NULL == context.p_bus) && (0U == g_fake.writes));
    g_fake.now_ms        = UINT64_MAX - 100U;
    const unsigned opens = g_opens;
    assert(0 == disci2c_run(&context, &g_config, g_reads, 2U, &result));
    assert((DISCI2C_FAULT == result.status) && (opens == g_opens));
}

/** @brief Busy/stale/invalid requests must perform zero hardware calls. */
static void verify_rejections (cfgwire_request_t * p_request)
{
    uint8_t        output[112] = {0};
    size_t         size        = 0U;
    const unsigned opens       = g_opens;
    p_request->revision        = 1U;
    assert(0 == cfgdisc_observe(
                    &g_discovery, p_request, output, sizeof(output), &size));
    assert(DISCI2C_STALE == cfgbytes_get(&output[4]));
    p_request->revision = 0U;
    const char disabled[] =
        "{\"devices\":[{\"enabled\":false}],\"interfaces\":[]}";
    memcpy(g_control.active, disabled, sizeof(disabled) - 1U);
    g_control.active_size = sizeof(disabled) - 1U;
    assert(0 == cfgdisc_observe(
                    &g_discovery, p_request, output, sizeof(output), &size));
    assert(DISCI2C_BUSY == cfgbytes_get(&output[4]));
    g_control.active_size       = 0U;
    uint8_t         invalid[24] = {0};
    const uint8_t * p_original  = p_request->p_data;
    p_request->p_data           = invalid;
    assert(0 == cfgdisc_observe(
                    &g_discovery, p_request, output, sizeof(output), &size));
    assert(DISCI2C_INVALID == cfgbytes_get(&output[4]));
    p_request->p_data = p_original;
    assert(opens == g_opens);
}

/** @brief The ordinary recovery command retries cleanup before clearing state.
 */
static void verify_recovery (cfgwire_request_t * p_request)
{
    uint8_t output[112] = {0};
    size_t  size        = 0U;
    gb_close_fail       = true;
    assert(0 == cfgdisc_observe(
                    &g_discovery, p_request, output, sizeof(output), &size));
    assert((DISCI2C_FAULT == cfgbytes_get(&output[4])) &&
           (g_control.b_recovery));
    const uint8_t     candidate[] = "{}";
    cfgwire_request_t request     = {.command    = CFGWIRE_APPLY,
                                     .request_id = 1U,
                                     .revision   = 1U,
                                     .size       = 2U,
                                     .p_data     = candidate};
    uint32_t          fields[6]   = {0};
    assert(0 == cfgctrl_handle(&g_control, &request, fields));
    assert(CFGCTRL_RECOVERY == fields[0]);
    request = (cfgwire_request_t){.command = CFGWIRE_RECOVER, .request_id = 2U};
    assert(0 == cfgctrl_handle(&g_control, &request, fields));
    assert((CFGCTRL_RECOVERY == fields[0]) &&
           (NULL != g_discovery.probe.p_bus));
    gb_close_fail      = false;
    request.request_id = 3U;
    assert(0 == cfgctrl_handle(&g_control, &request, fields));
    assert((CFGCTRL_EMPTY == fields[0]) && (!g_control.b_recovery));
    assert(NULL == g_discovery.probe.p_bus);
}

/** @brief Fragment both directions and expose the actual C frame to Python. */
static void verify_transport (const cfgwire_request_t * p_request)
{
    assert(0 == cfgpump_init(&g_pump, &g_control, &g_transport, &g_pump));
    assert(0 == cfgpump_discovery(&g_pump, cfgdisc_observe, &g_discovery));
    memcpy(g_input, "UTH1", 4U);
    g_input[4] = 1U;
    g_input[5] = CFGWIRE_DISCOVER;
    cfgbytes_put(&g_input[8], p_request->request_id);
    cfgbytes_put(&g_input[16], 24U);
    memcpy(&g_input[24], p_request->p_data, 24U);
    uint32_t crc = cfgbytes_crc(UINT32_MAX, g_input, 20U);
    crc          = cfgbytes_crc(crc, &g_input[24], 24U);
    cfgbytes_put(&g_input[20], ~crc);
    for (uint64_t time_ms = 0U; time_ms < 60U; ++time_ms)
    {
        assert(0 == cfgpump_step(&g_pump, time_ms));
    }
    assert(136U == g_sent);
    assert(DISCI2C_COMPLETE == cfgbytes_get(&g_output[28]));
    printf("DISCOVERY:");
    for (size_t index = 0U; index < g_sent; ++index)
    {
        /* Byte promotion matches the unsigned printf conversion. */
        printf("%02X", (unsigned)g_output[index]);
    }
    puts("");
}

/** @brief Read three bytes at most so framing must retain partial requests. */
static int
receive (void * p_context, uint8_t * p_data, size_t capacity, size_t * p_count)
{
    assert(p_context == &g_pump);
    assert(capacity >= 3U);
    const size_t remaining = sizeof(g_input) - g_received;
    *p_count               = (remaining < 3U) ? remaining : 3U;
    memcpy(p_data, &g_input[g_received], *p_count);
    g_received += *p_count;
    return 0;
}

/** @brief Preserve short writes and reject reentry while handling a reply. */
static int transmit (void *          p_context,
                     const uint8_t * p_data,
                     size_t          size,
                     size_t *        p_count)
{
    assert(p_context == &g_pump);
    assert(-2 == cfgpump_step(&g_pump, 0U));
    *p_count = (size < 7U) ? size : 7U;
    assert((g_sent + *p_count) <= sizeof(g_output));
    memcpy(&g_output[g_sent], p_data, *p_count);
    g_sent += *p_count;
    return 0;
}

/* End of file. */

