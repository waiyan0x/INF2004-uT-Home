/**
 * @file txptest.c
 * @brief Exercise actual owner, rule engine and HALs with explicit bus doubles.
 */
#include "txptest.h"
#include "c3tx.h"
#include "i2cfake.h"
#include "spifake.h"
#include "uartfake.h"
#include <string.h>

static i2cfake_t       g_i2c        = {0};
static spifake_t       g_spi        = {0};
static uartfake_t      g_uart       = {0};
static spifake_line_t  g_line       = {0};
static cfgbind_state_t g_binding    = {0};
static uint8_t         g_input[96]  = {0};
static uint8_t         g_output[96] = {0};
static bool            gb_input     = false;
static bool            gb_output    = false;
static bool            gb_idle      = true;
static bool            gb_empty     = true;
static int i2c_open (const i2cbus_config_t * p_config, i2cbus_t ** pp_bus);
static int i2c_close (i2cbus_t * p_bus);
static int spi_open (uint8_t sclk, uint8_t mosi, spibus_t ** pp_bus);
static int spi_close (spibus_t * p_bus);
static uartio_t * uart_port (void);
static int
line_open (uint8_t slot, uint8_t pin, bool b_initial, digiout_t * p_line);
static int line_close (digiout_t * p_line);
static const cfgbind_io_t g_operations = {.p_i2c_open   = i2c_open,
                                          .p_i2c_close  = i2c_close,
                                          .p_uart       = uart_port,
                                          .p_spi_open   = spi_open,
                                          .p_spi_close  = spi_close,
                                          .p_line_open  = line_open,
                                          .p_line_close = line_close};

/** @brief Borrow fake hardware but keep the actual HAL ownership wrappers. */
const cfgbind_io_t * txptest_bind (void)
{
    return &g_operations;
}

/** @brief Model the independent application's worker lease. */
bool txptest_idle (const cfgbind_state_t * p_state)
{
    return (NULL != p_state) && gb_idle;
}

/** @brief No HTTP upload in this isolated owner fixture. */
bool txptest_mux (const cfgmux_t * p_mux)
{
    (void)p_mux;
    return false;
}

/** @brief Model the native software ring draining, independent of RX. */
int txptest_empty (const void * p_owner, bool * pb_empty)
{
    if ((g_uart.port.p_owner != p_owner) || (NULL == pb_empty))
    {
        return -1;
    }
    *pb_empty = gb_empty;
    return 0;
}

/** @brief Copy exactly one network-owned command. */
int txptest_request (uint8_t * p_data, size_t capacity, size_t * p_size)
{
    *p_size = 0U;
    if (!gb_input)
    {
        return 1;
    }
    if (capacity < sizeof(g_input))
    {
        return -1;
    }
    memcpy(p_data, g_input, sizeof(g_input));
    *p_size  = sizeof(g_input);
    gb_input = false;
    return 0;
}

/** @brief Backpressure cannot overwrite a previously staged result. */
int txptest_result (const uint8_t * p_data, size_t size)
{
    if (gb_output)
    {
        return 1;
    }
    if (size != sizeof(g_output))
    {
        return -1;
    }
    memcpy(g_output, p_data, size);
    gb_output = true;
    return 0;
}

/** @brief Initialize each fixture only once, mirroring boot-lifetime storage.
 */
int txptest_start (void)
{
    i2cfake_init(&g_i2c);
    g_i2c.b_present[1]    = true;
    g_i2c.registers[1][0] = 0x50U;
    g_i2c.registers[1][3] = 0x70U;
    spifake_init(&g_spi);
    uartfake_init(&g_uart);
    g_uart.read_limit = 1U;
    return c3tx_start(&g_binding, NULL, UINT64_C(0x1122334455667788));
}

/** @brief Reject a second unread host request rather than replace it. */
int txptest_send (const uint8_t * p_data, size_t size)
{
    if ((sizeof(g_input) != size) || gb_input)
    {
        return -1;
    }
    memcpy(g_input, p_data, size);
    gb_input = true;
    return 0;
}

/** @brief Advance shared fake time without adding threads or hardware waits. */
void txptest_pump (uint64_t now_ms, bool b_online, bool b_idle)
{
    gb_idle       = b_idle;
    g_i2c.now_ms  = now_ms;
    g_spi.now_ms  = now_ms;
    g_uart.now_ms = now_ms;
    if (!b_online)
    {
        gb_input = false;
    }
    c3tx_step(now_ms, 4U, b_online);
}

/** @brief Copy one result to the test without changing the decision cache. */
int txptest_reply (uint8_t * p_data)
{
    if (!gb_output)
    {
        return 1;
    }
    memcpy(p_data, g_output, sizeof(g_output));
    gb_output = false;
    return 0;
}

/** @brief Feed bytes only after the real UART HAL has been opened. */
int txptest_feed (const uint8_t * p_data, size_t size)
{
    return uartfake_feed(&g_uart, p_data, size);
}

/** @brief Observe raw transport effects rather than interpret physical units.
 */
uint32_t txptest_observed (uint32_t field)
{
    switch (field)
    {
    case 0U:
        return g_i2c.transfers;
    case 1U:
        return g_spi.transfers;
    case 2U:
        return g_uart.reads;
    case 3U:
        return g_spi.records[0].bytes[0];
    case 4U:
        return g_spi.records[0].b_data ? 1U : 0U;
    case 5U:
        return (uint32_t)g_uart.transmitted;
    default:
        return 0U;
    }
}

/** @brief Control software ring completion without changing accepted bytes. */
void txptest_drain (bool b_empty)
{
    gb_empty = b_empty;
}

/** @brief Verify fixed I2C pins at the target-independent boundary. */
static int i2c_open (const i2cbus_config_t * p_config, i2cbus_t ** pp_bus)
{
    if ((0U != p_config->sda_gpio) || (1U != p_config->scl_gpio))
    {
        return -1;
    }
    *pp_bus = &g_i2c.bus;
    return 0;
}

/** @brief A leaked address must fail the physical close. */
static int i2c_close (i2cbus_t * p_bus)
{
    return i2cbus_has_claims(p_bus) ? -1 : 0;
}

/** @brief Verify fixed SPI pins at the target-independent boundary. */
static int spi_open (uint8_t sclk, uint8_t mosi, spibus_t ** pp_bus)
{
    if ((7U != sclk) || (6U != mosi))
    {
        return -1;
    }
    *pp_bus = &g_spi.bus;
    return 0;
}

/** @brief No attached device may disappear on close. */
static int spi_close (spibus_t * p_bus)
{
    return spibus_has_devices(p_bus) ? -1 : 0;
}

/** @brief Factory performs no I/O. */
static uartio_t * uart_port (void)
{
    return &g_uart.port;
}

/** @brief Bind an explicit D/C output through the existing digital interface.
 */
static int
line_open (uint8_t slot, uint8_t pin, bool b_initial, digiout_t * p_line)
{
    if ((7U != slot) || (3U != pin))
    {
        return -1;
    }
    g_line.b_high         = b_initial;
    g_spi.p_data_lines[4] = &g_line;
    *p_line = (digiout_t){.p_set = spifake_set, .p_context = &g_line};
    return 0;
}

/** @brief Release the fixture's borrowed digital line. */
static int line_close (digiout_t * p_line)
{
    *p_line = (digiout_t){0};
    return 0;
}

/* End of file. */

