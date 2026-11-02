/**
 * @file c3spi.c
 * @brief Serialize bounded FIFO chunks inside a single chip-select window.
 */
#include "c3spi.h"
#include "c3shw.h"
#include "c3cost.h"
#include <string.h>
#include "c3bwait.h"
#include "c3pin.h"
#include "spiown.h"
#include "workport.h"

/* Per-session settings; closing resets only this structure. */
typedef struct
{
    c3spi_config_t config;
    uint32_t       pins;
    int            source_hz;
} c3spi_state_t;

static c3spi_state_t g_state = {0};
/* Permanent: written once by startup creation, never deleted. */
static int      gh_mutex = 0;
static spiown_t g_owner  = {0};

static int
attach (void * p_context, const spibus_config_t * p_config, bool b_attach);
static int transfer (void *                   p_context,
                     const spibus_config_t *  p_config,
                     const spibus_request_t * p_request);
static int now_ms (void * p_context, uint64_t * p_now);
static int
delay_ms (void * p_context, uint32_t duration_ms, uint64_t deadline_ms);
static int create_lock (void * p_context);
static bool lifecycle_owner (void * p_context);
static int lock_bus (void * p_context, uint32_t timeout_ms);
static int unlock_bus (void * p_context);
static int start_bus (void * p_context, const void * p_settings);
static int stop_bus (void * p_context);
static int exchange (const spibus_config_t *  p_config,
                     const spibus_request_t * p_request);
static int wait_ready (uint64_t deadline_ms, bool b_update);
static int configure (const spibus_config_t * p_config, uint64_t deadline_ms);
static int chunks (const spibus_request_t * p_request);
static const spibus_ops_t g_operations = {.p_attach   = attach,
                                          .p_transfer = transfer,
                                          .p_now      = now_ms,
                                          .p_delay    = delay_ms};
static const spiown_ops_t g_lifecycle  = {.p_create = create_lock,
                                          .p_owner  = lifecycle_owner,
                                          .p_lock   = lock_bus,
                                          .p_unlock = unlock_bus,
                                          .p_start  = start_bus,
                                          .p_stop   = stop_bus};
/* Callbacks stay set even when closed, so a preempted caller never reaches a
 * null pointer; the session checks reject it instead. Devices are per session.
 */
static spibus_t g_bus = {.p_operations = &g_operations, .p_context = &g_state};

/** @brief Create the permanent bus mutex during startup, exactly once. */
int c3spi_init (void)
{
    return spiown_init(&g_owner, &g_lifecycle, &g_state);
}

/** @brief Validate settings, then start a session on the permanent mutex. */
int c3spi_open (const c3spi_config_t * p_config, spibus_t ** pp_bus)
{
    if ((NULL == p_config) || (NULL == pp_bus) || (2U != p_config->host) ||
        (!c3pin_valid(p_config->sclk_gpio)) ||
        (!c3pin_valid(p_config->mosi_gpio)) ||
        (p_config->sclk_gpio == p_config->mosi_gpio) ||
        ((UTHOME_SPI_NO_PIN != p_config->miso_gpio) &&
         ((!c3pin_valid(p_config->miso_gpio)) ||
          (p_config->miso_gpio == p_config->sclk_gpio) ||
          (p_config->miso_gpio == p_config->mosi_gpio))))
    {
        return SPIBUS_INVALID;
    }
    const int result = spiown_open(&g_owner, &g_bus, p_config);
    if (0 == result)
    {
        *pp_bus = &g_bus;
    }
    return result;
}

/** @brief Release hardware only after attached devices and transfers are gone.
 */
int c3spi_close (spibus_t * p_bus)
{
    if (&g_bus != p_bus)
    {
        return SPIBUS_STATE;
    }
    return spiown_close(&g_owner, p_bus);
}

/** @brief Attach/detach CS during serialized lifecycle work, never a transfer.
 */
static int
attach (void * p_context, const spibus_config_t * p_config, bool b_attach)
{
    /* No owner check: worker cleanup detaches while the owner test is false.
     * The task that changes the phase is the caller, so this read is exact.
     */
    if ((&g_state != p_context) || (!spiown_is_open(&g_owner)))
    {
        return SPIBUS_STATE;
    }
    if ((!c3pin_valid(p_config->cs_gpio)) || (p_config->clock_hz < 100000U) ||
        (p_config->clock_hz > 8000000U))
    {
        return SPIBUS_INVALID;
    }
    const uint32_t pin = UINT32_C(1) << p_config->cs_gpio;
    if (b_attach)
    {
        /* The validated 100 kHz..8 MHz request fits the SDK int argument. */
        const int actual = c3shw_clock(g_state.source_hz, p_config->clock_hz);
        if ((actual <= 0) || ((uint32_t)actual > p_config->clock_hz))
        {
            return SPIBUS_UNSUPPORTED;
        }
        if (0 != c3pin_reserve(pin, &g_bus))
        {
            return SPIBUS_STATE;
        }
        c3shw_attach(p_config->cs_gpio);
        return 0;
    }
    /* The common bus validates device identity and live driver claims first. */
    c3shw_detach(p_config->cs_gpio);
    return (0 == c3pin_release(pin, &g_bus)) ? 0 : SPIBUS_STATE;
}

/** @brief Include lock wait and all chunks in the caller's single deadline. */
static int transfer (void *                   p_context,
                     const spibus_config_t *  p_config,
                     const spibus_request_t * p_request)
{
    if (&g_state != p_context)
    {
        return SPIBUS_STATE;
    }
    c3cost_t cost = {0};
    c3cost_begin(&cost);
    uint64_t now = 0U;
    if (0 != rt_port_now_ms(&now))
    {
        c3cost_end(&cost, 0U, false, true);
        return SPIBUS_IO;
    }
    if ((now >= p_request->deadline_ms) ||
        ((p_request->deadline_ms - now) > UINT32_C(2147483647)))
    {
        c3cost_end(&cost, 0U, true, true);
        return SPIBUS_TIMEOUT;
    }
    /* Remaining time was checked to fit uint32_t and the kernel TMO range. */
    const int locked =
        spiown_begin(&g_owner, (uint32_t)(p_request->deadline_ms - now));
    if (0 != locked)
    {
        /* Closed or retained: no chip select or register is touched. */
        c3cost_end(&cost, 0U, SPIBUS_TIMEOUT == locked, true);
        return locked;
    }
    c3cost_acquired(&cost);
    int result = SPIBUS_UNSUPPORTED;
    /* Session settings are read only after the session was confirmed open. */
    if ((NULL == p_request->p_read) ||
        ((UTHOME_SPI_NO_PIN != g_state.config.miso_gpio) &&
         (p_config->clock_hz <= 1000000U)))
    {
        result = exchange(p_config, p_request);
    }
    const int unlocked = spiown_end(&g_owner);
    const int final    = (0 != unlocked) ? unlocked : result;
    c3cost_end(&cost,
               (0 == result) ? p_request->size : 0U,
               SPIBUS_TIMEOUT == final,
               0 != final);
    return final;
}

/** @brief Use the runtime operating-time clock. */
static int now_ms (void * p_context, uint64_t * p_now)
{
    if ((&g_state != p_context) || (!spiown_is_open(&g_owner)))
    {
        return SPIBUS_STATE;
    }
    return (0 == rt_port_now_ms(p_now)) ? 0 : SPIBUS_IO;
}

/** @brief Create the inherited-priority mutex that every session reuses. */
static int create_lock (void * p_context)
{
    (void)p_context; /* The permanent handle is module state, not session. */
    const int h_mutex = c3bwait_create();
    if (h_mutex <= 0)
    {
        return SPIBUS_IO;
    }
    gh_mutex = h_mutex;
    return 0;
}

/** @brief Restrict session changes to the serialized lifecycle owner. */
static bool lifecycle_owner (void * p_context)
{
    (void)p_context; /* Ownership is a property of the calling task. */
    return rt_port_is_owner();
}

/** @brief Lock within the remaining budget; zero is the kernel's TMO_POL. */
static int lock_bus (void * p_context, uint32_t timeout_ms)
{
    (void)p_context; /* The permanent handle is module state, not session. */
    if (timeout_ms > UINT32_C(2147483647))
    {
        return SPIBUS_INVALID;
    }
    /* The checked value fits the nonnegative kernel TMO range. */
    const int locked = c3bwait_lock(gh_mutex, timeout_ms);
    if (0 == locked)
    {
        return 0;
    }
    return (C3BWAIT_TIMEOUT == locked) ? SPIBUS_TIMEOUT : SPIBUS_IO;
}

/** @brief Release the permanent mutex held by this task. */
static int unlock_bus (void * p_context)
{
    (void)p_context; /* The permanent handle is module state, not session. */
    return (0 == c3bwait_unlock(gh_mutex)) ? 0 : SPIBUS_IO;
}

/** @brief Reserve validated pins before enabling SPI2; never touch flash SPI.
 */
static int start_bus (void * p_context, const void * p_settings)
{
    (void)p_context; /* Session settings are the module's g_state. */
    /* c3spi_open passes its validated configuration through spiown. */
    const c3spi_config_t * p_config  = p_settings;
    const int              source_hz = c3bwait_source_hz();
    if (source_hz <= 0)
    {
        return SPIBUS_IO;
    }
    uint32_t pins = (UINT32_C(1) << p_config->sclk_gpio) |
                    (UINT32_C(1) << p_config->mosi_gpio);
    if (UTHOME_SPI_NO_PIN != p_config->miso_gpio)
    {
        pins |= UINT32_C(1) << p_config->miso_gpio;
    }
    if (0 != c3pin_reserve(pins, &g_bus))
    {
        return SPIBUS_STATE;
    }
    g_state = (c3spi_state_t){
        .config = *p_config, .pins = pins, .source_hz = source_hz};
    c3shw_reset();
    c3shw_pins_open(p_config);
    return 0;
}

/** @brief Stop the controller; keep pin ownership if the ledger refuses it. */
static int stop_bus (void * p_context)
{
    (void)p_context; /* Session settings are the module's g_state. */
    c3shw_stop(&g_state.config);
    if (0 != c3pin_release(g_state.pins, &g_bus))
    {
        return SPIBUS_STATE;
    }
    g_state = (c3spi_state_t){0};
    return 0;
}

/** @brief Run one CS window while holding the bus mutex; CS ends inactive. */
static int exchange (const spibus_config_t *  p_config,
                     const spibus_request_t * p_request)
{
    int result = configure(p_config, p_request->deadline_ms);
    if (0 == result)
    {
        c3shw_select(p_config->cs_gpio, true);
        c3bwait_spin_us(1U);
        result = chunks(p_request);
        c3bwait_spin_us(1U);
    }
    c3shw_select(p_config->cs_gpio, false);
    if (0 != result)
    {
        c3shw_reset();
    }
    return result;
}

/** @brief Yield without resetting or extending the operation budget. */
static int
delay_ms (void * p_context, uint32_t duration_ms, uint64_t deadline_ms)
{
    uint64_t now = 0U;
    if ((0 != now_ms(p_context, &now)) || (now >= deadline_ms) ||
        (duration_ms >= (deadline_ms - now)))
    {
        return SPIBUS_TIMEOUT;
    }
    const uint64_t end = now + duration_ms;
    while (now < end)
    {
        if ((0 != c3bwait_yield()) || (0 != now_ms(p_context, &now)))
        {
            return SPIBUS_IO;
        }
        if (now >= deadline_ms)
        {
            return SPIBUS_TIMEOUT;
        }
    }
    return 0;
}

/** @brief Bound both register-update completion and transfer completion. */
static int wait_ready (uint64_t deadline_ms, bool b_update)
{
    unsigned polls = 0U;
    for (;;)
    {
        uint64_t now = 0U;
        if (0 != rt_port_now_ms(&now))
        {
            return SPIBUS_IO;
        }
        if (now >= deadline_ms)
        {
            return SPIBUS_TIMEOUT;
        }
        if (c3shw_ready(b_update))
        {
            return 0;
        }
        c3bwait_spin_us(10U);
        ++polls;
        /* A 64-byte FIFO chunk takes 512 us at 1 MHz. Yielding after only
         * 250 us adds a native tick to every chunk and can exhaust a 20 ms
         * display deadline despite sufficient bus bandwidth. Keep interrupts
         * enabled and allow one millisecond of bounded polling before sleep.
         * The original absolute request deadline still bounds the whole I/O.
         */
        if (100U == polls)
        {
            polls = 0U;
            if (0 != c3bwait_yield())
            {
                return SPIBUS_IO;
            }
        }
    }
}

/** @brief Apply native settings, then bound register-update completion. */
static int configure (const spibus_config_t * p_config, uint64_t deadline_ms)
{
    const int result = c3shw_configure(g_state.source_hz, p_config);
    return (0 == result) ? wait_ready(deadline_ms, true) : result;
}

/** @brief Keep CS asserted across 64-byte chunks without DMA or allocation. */
static int chunks (const spibus_request_t * p_request)
{
    for (size_t offset = 0U; offset < p_request->size; offset += 64U)
    {
        uint8_t bytes[64] = {0};
        size_t  size      = p_request->size - offset;
        if (size > sizeof(bytes))
        {
            size = sizeof(bytes);
        }
        if (NULL != p_request->p_write)
        {
            memcpy(bytes, &p_request->p_write[offset], size);
        }
        c3shw_prepare(bytes, size, NULL != p_request->p_read);
        int result = wait_ready(p_request->deadline_ms, true);
        if (0 == result)
        {
            c3shw_start();
            result = wait_ready(p_request->deadline_ms, false);
        }
        if (0 != result)
        {
            return result;
        }
        if (NULL != p_request->p_read)
        {
            c3shw_read(&p_request->p_read[offset], size);
        }
    }
    return 0;
}

/* End of file. */

