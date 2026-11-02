/**
 * @file busfake.c
 * @brief Inject peripheral faults below the real adapter transfer algorithms.
 */
#include "busfake.h"

#ifdef NDEBUG
#error "Bus fault tests require assertions."
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pinpool.h"
#include "c3i2c.h"

enum
{
    BUSFAKE_DELAY_CLOCK = -1,
    BUSFAKE_NONE        = 0,
    BUSFAKE_I_NACK      = 1,
    BUSFAKE_I_ARBITRATION,
    BUSFAKE_I_TIMEOUT,
    BUSFAKE_I_DEADLINE,
    BUSFAKE_I_CLOCK,
    BUSFAKE_I_YIELD,
    BUSFAKE_I_PAUSE_TIMEOUT,
    BUSFAKE_I_PAUSE_NACK,
    BUSFAKE_I_PAUSE_DEADLINE,
    BUSFAKE_I_COUNT,
    BUSFAKE_I_PAUSE_OK,
    BUSFAKE_I_ONE_BYTE,
    BUSFAKE_I_FULL_FIFO,
    BUSFAKE_I_DELAY,
    BUSFAKE_S_CONFIG_TIMEOUT = 20,
    BUSFAKE_S_UPDATE_TIMEOUT,
    BUSFAKE_S_TRANSFER_TIMEOUT,
    BUSFAKE_S_SECOND_TIMEOUT,
    BUSFAKE_S_CLOCK,
    BUSFAKE_S_YIELD,
    BUSFAKE_S_CHUNKS,
    BUSFAKE_S_LOCK,
    BUSFAKE_S_DELAY
};

typedef struct
{
    c3ihw_command_t operation;
    uint32_t        count;
    bool            b_last;
} busfake_command_t;

static int               g_fault            = BUSFAKE_NONE;
static uint64_t          g_now_us           = 0U;
static unsigned          g_clocks           = 0U;
static unsigned          g_creates          = 0U;
static unsigned          g_resets           = 0U;
static unsigned          g_starts           = 0U;
static unsigned          g_chunks           = 0U;
static unsigned          g_holds            = 0U;
static unsigned          g_restores         = 0U;
static unsigned          g_selects          = 0U;
static unsigned          g_releases         = 0U;
static bool              gb_locked          = false;
static bool              gb_selected        = false;
static bool              gb_holding         = false;
static bool              gb_i_paused        = false;
static bool              gb_i_pause_elapsed = false;
static pinpool_t         g_pins             = {.allowed = UINT32_MAX};
static busfake_command_t g_commands[8]      = {0};
static uint8_t           g_transmitted[32]  = {0};
static uint32_t          g_transmitted_size = 0U;
static uint8_t           g_fifo[64]         = {0};
static size_t            g_fifo_size        = 0U;
static size_t            g_recorded_bytes   = 0U;
static bool              gb_recorded_error  = false;
static bool              gb_delay_failure   = false;

static void verify_i2c (int scenario);
static void verify_spi (int scenario);
static int i2c_error (int scenario);
static int spi_error (int scenario);
static uint32_t requested_read (void);
static void verify_delay_result (int expected, int actual);

/** @brief A fresh process has exactly one permanent mutex for its selected bus.
 */
int busfake_wait_create (void)
{
    ++g_creates;
    return 1;
}

/** @brief Record ownership, or fail the transfer's finite lock acquisition. */
int busfake_wait_lock (int h_mutex, uint32_t timeout_ms)
{
    assert(1 == h_mutex);
    assert(!gb_locked);
    if ((BUSFAKE_S_LOCK == g_fault) && (0U != timeout_ms))
    {
        return C3BWAIT_TIMEOUT;
    }
    gb_locked = true;
    return 0;
}

/** @brief Every adapter exit must release the mutex it acquired. */
int busfake_wait_unlock (int h_mutex)
{
    assert((1 == h_mutex) && gb_locked);
    gb_locked = false;
    return 0;
}

/** @brief Advance one tick or inject a failed native yield. */
int busfake_wait_yield (void)
{
    assert(gb_locked);
    if ((BUSFAKE_I_YIELD == g_fault) || (BUSFAKE_S_YIELD == g_fault))
    {
        return C3BWAIT_ERROR;
    }
    g_now_us += 1000U;
    return 0;
}

/** @brief Polling advances time so a stalled peripheral reaches its deadline.
 */
void busfake_wait_spin_us (uint32_t duration_us)
{
    assert(gb_locked);
    g_now_us += duration_us;
}

/** @brief Provide the native board's deterministic divisor input. */
int busfake_wait_source_hz (void)
{
    return 40000000;
}

/** @brief Fail once inside polling, after the wrapper and lock checks. */
int busfake_now (uint64_t * p_now)
{
    ++g_clocks;
    assert(g_clocks < 10000U);
    if ((BUSFAKE_DELAY_CLOCK == g_fault) ||
        ((BUSFAKE_I_CLOCK == g_fault) && (4U == g_clocks)) ||
        ((BUSFAKE_S_CLOCK == g_fault) && (3U == g_clocks)))
    {
        return -1;
    }
    *p_now = g_now_us / 1000U;
    return 0;
}

/** @brief All test lifecycle calls come from the one serialized owner. */
bool busfake_owner (void)
{
    return true;
}

/** @brief Admit the actual nonreserved pins used by these fixtures. */
bool busfake_pin_valid (uint8_t pin)
{
    return (pin <= 1U) || ((pin >= 3U) && (pin <= 7U)) || (10U == pin);
}

/** @brief Reuse the real pin ledger rather than weakening resource checks. */
int busfake_pin_reserve (uint32_t mask, const void * p_owner)
{
    return pinpool_claim(&g_pins, mask, p_owner);
}

/** @brief Only the borrowing bus may release a reserved pin. */
int busfake_pin_release (uint32_t mask, const void * p_owner)
{
    return pinpool_release(&g_pins, mask, p_owner);
}

/** @brief Mark instrumentation lifetime without claiming native measurements.
 */
void busfake_cost_begin (c3cost_t * p_state)
{
    p_state->b_valid = true;
}

/** @brief Instrumentation observes acquisition only while the mutex is held. */
void busfake_cost_acquired (c3cost_t * p_state)
{
    assert(gb_locked);
    p_state->b_acquired = true;
}

/** @brief Retain result accounting after the adapter has released ownership. */
void busfake_cost_end (const c3cost_t * p_state,
                       size_t           bytes,
                       bool             b_timeout,
                       bool             b_error)
{
    assert(p_state->b_valid);
    assert(!gb_locked);
    assert((!b_timeout) || b_error);
    g_recorded_bytes  = bytes;
    gb_recorded_error = b_error;
}

/** @brief Verify that bus pins are routed only after serialized reservation. */
void busfake_i_pins_open (const i2cbus_config_t * p_config)
{
    assert(gb_locked);
    assert((0U == p_config->sda_gpio) && (1U == p_config->scl_gpio));
}

/** @brief Closing must still own the mutex and the original settings. */
void busfake_i_stop (const i2cbus_config_t * p_config)
{
    busfake_i_pins_open(p_config);
}

/** @brief Reset aborts held-clock state without erasing diagnostic counters. */
void busfake_i_reset (uint32_t source_hz, uint32_t clock_hz)
{
    assert(gb_locked);
    assert((40000000U == source_hz) && (100000U == clock_hz));
    ++g_resets;
    gb_holding         = false;
    gb_i_paused        = false;
    gb_i_pause_elapsed = false;
}

/** @brief FIFO preparation occurs within the same transfer ownership window. */
void busfake_i_clear_fifo (void)
{
    assert(gb_locked);
}

/** @brief Acknowledge first-phase completion before the held-clock pause. */
void busfake_i_clear_status (void)
{
    assert(gb_locked);
    gb_i_pause_elapsed = gb_i_paused;
}

/** @brief Unused slots start as END, matching the native command bank. */
void busfake_i_clear_commands (void)
{
    assert(gb_locked);
    for (size_t index = 0U; index < 8U; ++index)
    {
        g_commands[index] = (busfake_command_t){.operation = C3IHW_END_COMMAND};
    }
}

/** @brief Record bounded opcodes and final-read NACK without hardware. */
void busfake_i_command (uint32_t        slot,
                        c3ihw_command_t operation,
                        uint32_t        count,
                        bool            b_last_read)
{
    assert(gb_locked);
    assert((slot < 8U) && (count <= 32U));
    g_commands[slot] = (busfake_command_t){operation, count, b_last_read};
}

/** @brief Retain address/payload bytes for independent protocol assertions. */
void busfake_i_write (const uint8_t * p_bytes, uint32_t count)
{
    assert(gb_locked);
    assert(count <= sizeof(g_transmitted));
    memcpy(g_transmitted, p_bytes, count);
    g_transmitted_size = count;
}

/** @brief A held-read continuation must start with READ, never another START.
 */
void busfake_i_start (void)
{
    assert(gb_locked);
    if (gb_i_paused)
    {
        assert(C3IHW_READ_COMMAND == g_commands[0].operation);
    }
    else
    {
        assert(C3IHW_RESTART_COMMAND == g_commands[0].operation);
    }
    ++g_starts;
    gb_i_paused        = 0U == requested_read();
    gb_i_pause_elapsed = false;
}

/** @brief Inject wire errors, stuck completion or faults during held SCL. */
uint32_t busfake_i_status (void)
{
    assert(gb_locked);
    if (BUSFAKE_I_NACK == g_fault)
    {
        return C3IHW_NACK;
    }
    if (BUSFAKE_I_ARBITRATION == g_fault)
    {
        return C3IHW_ARBITRATION;
    }
    if (BUSFAKE_I_TIMEOUT == g_fault)
    {
        return C3IHW_TIMEOUT;
    }
    if ((BUSFAKE_I_DEADLINE == g_fault) || (BUSFAKE_I_YIELD == g_fault))
    {
        return 0U;
    }
    if (gb_i_pause_elapsed && (BUSFAKE_I_PAUSE_TIMEOUT == g_fault))
    {
        return C3IHW_TIMEOUT;
    }
    if (gb_i_pause_elapsed && (BUSFAKE_I_PAUSE_NACK == g_fault))
    {
        return C3IHW_NACK;
    }
    return gb_i_paused ? C3IHW_END : C3IHW_COMPLETE;
}

/** @brief Save recognizable timeout values before the adapter's pause. */
void busfake_i_hold (c3ihw_saved_t * p_saved)
{
    assert(gb_locked);
    ++g_holds;
    gb_holding = true;
    *p_saved   = (c3ihw_saved_t){3, 4U, 5U};
}

/** @brief Restore all three saved timeout fields only after successful read. */
void busfake_i_restore (const c3ihw_saved_t * p_saved)
{
    assert(gb_locked && gb_holding);
    assert((3 == p_saved->timeout) && (4U == p_saved->scl) &&
           (5U == p_saved->main));
    ++g_restores;
    gb_holding = false;
}

/** @brief Make a short FIFO observable without permitting a buffer over-read.
 */
uint32_t busfake_i_read_count (void)
{
    return (BUSFAKE_I_COUNT == g_fault) ? 0U : requested_read();
}

/** @brief Copy recognizable bytes only after the adapter validated the count.
 */
void busfake_i_read (uint8_t * p_bytes, uint32_t count)
{
    assert(gb_locked);
    assert(count == requested_read());
    memset(p_bytes, 0x5A, count);
}

/** @brief Check the SPI fixture's validated bus pins. */
void busfake_s_pins_open (const c3spi_config_t * p_config)
{
    assert(gb_locked);
    assert((4U == p_config->sclk_gpio) && (6U == p_config->mosi_gpio) &&
           (5U == p_config->miso_gpio));
}

/** @brief Hardware shutdown precedes release of the real pin ledger. */
void busfake_s_stop (const c3spi_config_t * p_config)
{
    busfake_s_pins_open(p_config);
    assert(!gb_selected);
}

/** @brief Chip select starts inactive before a device can transfer. */
void busfake_s_attach (uint8_t pin)
{
    assert(7U == pin);
    assert(!gb_selected);
}

/** @brief A detached device must have no active chip-select window. */
void busfake_s_detach (uint8_t pin)
{
    assert(7U == pin);
    assert(!gb_selected);
}

/** @brief Record one CS assertion across any number of FIFO chunks. */
void busfake_s_select (uint8_t pin, bool b_selected)
{
    assert(gb_locked && (7U == pin));
    if (b_selected)
    {
        ++g_selects;
    }
    else
    {
        ++g_releases;
    }
    gb_selected = b_selected;
}

/** @brief Error recovery must deassert CS before resetting hardware. */
void busfake_s_reset (void)
{
    assert(gb_locked && (!gb_selected));
    ++g_resets;
}

/** @brief Quantization is exact for the fixed 1 MHz fixture request. */
int busfake_s_clock (int source_hz, uint32_t clock_hz)
{
    assert((40000000 == source_hz) && (1000000U == clock_hz));
    return 1000000;
}

/** @brief Configuration always occurs outside an asserted CS window. */
int busfake_s_configure (int source_hz, const spibus_config_t * p_config)
{
    assert(gb_locked && (!gb_selected));
    assert(1000000 == busfake_s_clock(source_hz, p_config->clock_hz));
    g_chunks = 0U;
    return 0;
}

/** @brief Stall selected update/transfer phases while the real adapter polls.
 */
bool busfake_s_ready (bool b_update)
{
    assert(gb_locked);
    if (b_update &&
        ((BUSFAKE_S_CONFIG_TIMEOUT == g_fault) || (BUSFAKE_S_YIELD == g_fault)))
    {
        return false;
    }
    if (b_update && (BUSFAKE_S_UPDATE_TIMEOUT == g_fault) && (0U != g_chunks))
    {
        return false;
    }
    if ((!b_update) && (BUSFAKE_S_TRANSFER_TIMEOUT == g_fault))
    {
        return false;
    }
    if ((!b_update) && (BUSFAKE_S_SECOND_TIMEOUT == g_fault) &&
        (2U == g_chunks))
    {
        return false;
    }
    return true;
}

/** @brief Retain each padded chunk while ownership and CS stay asserted. */
void busfake_s_prepare (const uint8_t * p_bytes, size_t size, bool b_read)
{
    assert(gb_locked && gb_selected && b_read);
    assert((size > 0U) && (size <= sizeof(g_fifo)));
    memcpy(g_fifo, p_bytes, sizeof(g_fifo));
    for (size_t index = size; index < sizeof(g_fifo); ++index)
    {
        assert(0U == g_fifo[index]);
    }
    g_fifo_size = size;
    ++g_chunks;
}

/** @brief A data phase may start only inside the current CS window. */
void busfake_s_start (void)
{
    assert(gb_locked && gb_selected);
    ++g_starts;
}

/** @brief Echo the completed chunk, preserving untouched bytes after failure.
 */
void busfake_s_read (uint8_t * p_bytes, size_t size)
{
    assert(gb_locked && gb_selected && (size == g_fifo_size));
    memcpy(p_bytes, g_fifo, size);
}

/** @brief Run one fresh-process fault or protocol-boundary scenario. */
int busfake_verify (int scenario)
{
    if (scenario < BUSFAKE_S_CONFIG_TIMEOUT)
    {
        verify_i2c(scenario);
    }
    else
    {
        verify_spi(scenario);
    }
    assert((!gb_locked) && (!gb_selected) && (!gb_holding));
    assert(1U == g_creates);
    for (size_t index = 0U; index < 32U; ++index)
    {
        assert(NULL == g_pins.p_owners[index]);
    }
    return gb_delay_failure ? 1 : 0;
}

/** @brief Sum bounded READ commands and require a final-byte NACK. */
static uint32_t requested_read (void)
{
    uint32_t count  = 0U;
    bool     b_last = false;
    for (size_t index = 0U; index < 8U; ++index)
    {
        if (C3IHW_READ_COMMAND == g_commands[index].operation)
        {
            assert(!b_last);
            count += g_commands[index].count;
            b_last = g_commands[index].b_last;
            if (b_last)
            {
                assert(1U == g_commands[index].count);
            }
        }
    }
    assert((0U == count) || b_last);
    return count;
}

/** @brief Preserve error domains for the injected I2C failures. */
static int i2c_error (int scenario)
{
    if (BUSFAKE_I_NACK == scenario)
    {
        return I2CBUS_NACK;
    }
    if ((BUSFAKE_I_TIMEOUT == scenario) || (BUSFAKE_I_DEADLINE == scenario) ||
        (BUSFAKE_I_PAUSE_TIMEOUT == scenario) ||
        (BUSFAKE_I_PAUSE_DEADLINE == scenario))
    {
        return I2CBUS_TIMEOUT;
    }
    return (scenario >= BUSFAKE_I_PAUSE_OK) ? 0 : I2CBUS_IO;
}

/** @brief Prove bounded reset/unlock and a successful next I2C transaction. */
static void verify_i2c (int scenario)
{
    assert((scenario >= BUSFAKE_I_NACK) && (scenario <= BUSFAKE_I_DELAY));
    const i2cbus_config_t config = {
        .sda_gpio = 0U, .scl_gpio = 1U, .clock_hz = 100000U};
    i2cbus_t * p_bus       = NULL;
    uint8_t    written[30] = {0x42U};
    uint8_t    received[32];
    memset(received, 0xCC, sizeof(received));
    assert(0 == c3i2c_init());
    assert(0 == c3i2c_init());
    assert(0 == c3i2c_open(&config, &p_bus));
    if (BUSFAKE_I_DELAY == scenario)
    {
        g_fault = BUSFAKE_DELAY_CLOCK;
        verify_delay_result(I2CBUS_IO, i2cbus_delay_us(p_bus, 1000U, 10U));
        g_fault = BUSFAKE_NONE;
        verify_delay_result(I2CBUS_TIMEOUT,
                            i2cbus_delay_us(p_bus, 10000U, 10U));
        verify_delay_result(0, i2cbus_delay_us(p_bus, 0U, 10U));
        assert(0 == c3i2c_close(p_bus));
        const unsigned clocks = g_clocks;
        verify_delay_result(I2CBUS_STATE, i2cbus_delay_us(p_bus, 1000U, 10U));
        assert(clocks == g_clocks);
        return;
    }
    i2cbus_request_t request = {.address     = 0x76U,
                                .p_write     = written,
                                .write_size  = 1U,
                                .p_read      = received,
                                .read_size   = 2U,
                                .deadline_ms = 10U};
    if ((scenario >= BUSFAKE_I_PAUSE_TIMEOUT) &&
        (scenario <= BUSFAKE_I_PAUSE_OK))
    {
        request.read_pause_ms = 2U;
    }
    if (BUSFAKE_I_PAUSE_DEADLINE == scenario)
    {
        request.deadline_ms = 1U;
    }
    if (BUSFAKE_I_ONE_BYTE == scenario)
    {
        request.read_size = 1U;
    }
    if (BUSFAKE_I_FULL_FIFO == scenario)
    {
        request.write_size = sizeof(written);
        request.read_size  = sizeof(received);
    }
    g_fault            = scenario;
    const int expected = i2c_error(scenario);
    assert(expected == i2cbus_transfer(p_bus, &request));
    assert((!gb_locked) && (!gb_holding));
    assert(g_now_us <= ((request.deadline_ms * 1000U) + 1000U));
    assert(0xECU == g_transmitted[0]);
    assert((request.write_size + 2U) == g_transmitted_size);
    assert((0 != expected) == gb_recorded_error);
    if (0 != expected)
    {
        assert(2U == g_resets);
        assert(0U == g_recorded_bytes);
        for (size_t index = 0U; index < sizeof(received); ++index)
        {
            assert(0xCCU == received[index]);
        }
    }
    else
    {
        assert(1U == g_resets);
        assert(request.write_size + request.read_size == g_recorded_bytes);
        if (0U != request.read_pause_ms)
        {
            assert((1U == g_holds) && (1U == g_restores) && (2U == g_starts));
        }
    }
    g_fault               = BUSFAKE_NONE;
    request.read_pause_ms = 0U;
    request.deadline_ms   = (g_now_us / 1000U) + 10U;
    assert(0 == i2cbus_transfer(p_bus, &request));
    for (size_t index = 0U; index < request.read_size; ++index)
    {
        assert(0x5AU == received[index]);
    }
    assert(0 == c3i2c_close(p_bus));
}

/** @brief Clock/yield errors are IO; stalled hardware and locks are timeouts.
 */
static int spi_error (int scenario)
{
    if (BUSFAKE_S_CHUNKS == scenario)
    {
        return 0;
    }
    if ((BUSFAKE_S_CLOCK == scenario) || (BUSFAKE_S_YIELD == scenario))
    {
        return SPIBUS_IO;
    }
    return SPIBUS_TIMEOUT;
}

/** @brief Prove CS/reset/unlock, partial RX bounds and a successful retry. */
static void verify_spi (int scenario)
{
    assert((scenario >= BUSFAKE_S_CONFIG_TIMEOUT) &&
           (scenario <= BUSFAKE_S_DELAY));
    const c3spi_config_t config = {
        .host = 2U, .sclk_gpio = 4U, .mosi_gpio = 6U, .miso_gpio = 5U};
    const spibus_config_t device_config = {.cs_gpio = 7U, .clock_hz = 1000000U};
    spibus_t *            p_bus         = NULL;
    spibus_device_t       device        = {0};
    uint8_t               written[130];
    uint8_t               received[130];
    /* The fixture is 130 bytes, so every index fits uint8_t. */
    for (size_t index = 0U; index < sizeof(written); ++index)
    {
        written[index] = (uint8_t)index;
    }
    memset(received, 0xCC, sizeof(received));
    assert(0 == c3spi_init());
    assert(0 == c3spi_init());
    assert(0 == c3spi_open(&config, &p_bus));
    if (BUSFAKE_S_DELAY == scenario)
    {
        g_fault = BUSFAKE_DELAY_CLOCK;
        verify_delay_result(SPIBUS_IO, spibus_delay_ms(p_bus, 1U, 10U));
        g_fault = BUSFAKE_NONE;
        verify_delay_result(SPIBUS_TIMEOUT, spibus_delay_ms(p_bus, 10U, 10U));
        verify_delay_result(0, spibus_delay_ms(p_bus, 0U, 10U));
        assert(0 == c3spi_close(p_bus));
        const unsigned clocks = g_clocks;
        verify_delay_result(SPIBUS_STATE, spibus_delay_ms(p_bus, 1U, 10U));
        assert(clocks == g_clocks);
        return;
    }
    assert(0 == spibus_attach(p_bus, &device, &device_config));
    spibus_request_t request = {.p_write     = written,
                                .p_read      = received,
                                .size        = sizeof(written),
                                .deadline_ms = 10U};
    g_fault                  = scenario;
    const int expected       = spi_error(scenario);
    assert(expected == spibus_transfer(&device, &request));
    assert((!gb_locked) && (!gb_selected));
    assert(g_now_us <= ((request.deadline_ms * 1000U) + 1000U));
    assert((0 != expected) == gb_recorded_error);
    if (0 != expected)
    {
        assert(((BUSFAKE_S_LOCK == scenario) ? 1U : 2U) == g_resets);
        const size_t copied = (BUSFAKE_S_SECOND_TIMEOUT == scenario) ? 64U : 0U;
        assert(0 == memcmp(written, received, copied));
        for (size_t index = copied; index < sizeof(received); ++index)
        {
            assert(0xCCU == received[index]);
        }
    }
    else
    {
        assert((3U == g_chunks) && (1U == g_selects) && (1U == g_releases));
        assert(0 == memcmp(written, received, sizeof(written)));
        assert(sizeof(written) == g_recorded_bytes);
    }
    g_fault             = BUSFAKE_NONE;
    request.deadline_ms = (g_now_us / 1000U) + 10U;
    assert(0 == spibus_transfer(&device, &request));
    assert(0 == memcmp(written, received, sizeof(written)));
    assert(0 == spibus_detach(&device));
    assert(0 == c3spi_close(p_bus));
}

/** @brief Report deliberate regression mismatches without an OS abort dialog.
 */
static void verify_delay_result (int expected, int actual)
{
    if (expected != actual)
    {
        fprintf(stderr,
                "Delay result: expected %d, received %d\n",
                expected,
                actual);
        gb_delay_failure = true;
    }
}

/* End of file. */

