/**
 * @file c3i2c.c
 * @brief Serialize finite FIFO transfers using the pinned SDK register layer.
 */
#include "c3i2c.h"
#include "c3ihw.h"
#include "c3cost.h"
#include "c3pin.h"
#include "i2cown.h"

#include <string.h>
#include "c3bwait.h"
#include "workport.h"

/* Per-session settings; closing resets only this structure. */
typedef struct
{
    i2cbus_config_t config;
    uint32_t        source_hz;
    uint32_t        pins;
} c3i2c_state_t;

static c3i2c_state_t g_state = {0};
/* Permanent: written once by startup creation, never deleted. */
static int      gh_mutex = 0;
static i2cown_t g_owner  = {0};

static int transfer (void * p_context, const i2cbus_request_t * p_request);
static int now_ms (void * p_context, uint64_t * p_now);
static int
delay_us (void * p_context, uint32_t duration_us, uint64_t deadline_ms);
static bool valid_pin (uint8_t pin);
static int create_lock (void * p_context);
static bool lifecycle_owner (void * p_context);
static int lock_bus (void * p_context, uint32_t timeout_ms);
static int unlock_bus (void * p_context);
static int start_bus (void * p_context, const void * p_settings);
static int stop_bus (void * p_context);
static void load_commands (const i2cbus_request_t * p_request, bool b_pause);
static int wait_complete (uint64_t deadline_ms, uint32_t completion);
static int resume_read (const i2cbus_request_t * p_request);
static int exchange (const i2cbus_request_t * p_request);

static const i2cbus_ops_t g_operations = {
    .p_transfer = transfer, .p_now = now_ms, .p_delay = delay_us};
static const i2cown_ops_t g_lifecycle = {.p_create = create_lock,
                                         .p_owner  = lifecycle_owner,
                                         .p_lock   = lock_bus,
                                         .p_unlock = unlock_bus,
                                         .p_start  = start_bus,
                                         .p_stop   = stop_bus};
/* Callbacks stay set even when closed, so a preempted caller never reaches a
 * null pointer; the session checks reject it instead. Claims are per session.
 */
static i2cbus_t g_bus = {.p_operations = &g_operations, .p_context = &g_state};

/** @brief Create the permanent bus mutex during startup, exactly once. */
int c3i2c_init (void)
{
    return i2cown_init(&g_owner, &g_lifecycle, &g_state);
}

/** @brief Validate settings, then start a session on the permanent mutex. */
int c3i2c_open (const i2cbus_config_t * p_config, i2cbus_t ** pp_bus)
{
    if ((NULL == p_config) || (NULL == pp_bus) || (0U != p_config->port) ||
        (!valid_pin(p_config->sda_gpio)) || (!valid_pin(p_config->scl_gpio)) ||
        (p_config->sda_gpio == p_config->scl_gpio) ||
        (p_config->clock_hz < 10000U) || (p_config->clock_hz > 400000U))
    {
        return I2CBUS_INVALID;
    }
    /* A claim recorded while closed belongs to a misbehaving caller; refuse
     * rather than silently discard another device's ownership. */
    if (i2cbus_has_claims(&g_bus))
    {
        return I2CBUS_STATE;
    }
    const int result = i2cown_open(&g_owner, p_config);
    if (0 == result)
    {
        *pp_bus = &g_bus;
    }
    return result;
}

/** @brief Release hardware only after device claims and transfers are gone. */
int c3i2c_close (i2cbus_t * p_bus)
{
    if (&g_bus != p_bus)
    {
        return I2CBUS_STATE;
    }
    return i2cown_close(&g_owner, p_bus);
}

/** @brief Budget mutex contention and the hardware transaction together. */
static int transfer (void * p_context, const i2cbus_request_t * p_request)
{
    if (&g_state != p_context)
    {
        return I2CBUS_STATE;
    }
    c3cost_t cost = {0};
    c3cost_begin(&cost);
    uint64_t now = 0U;
    if (0 != rt_port_now_ms(&now))
    {
        c3cost_end(&cost, 0U, false, true);
        return I2CBUS_STATE;
    }
    if ((now >= p_request->deadline_ms) ||
        ((p_request->deadline_ms - now) > UINT32_C(2147483647)))
    {
        c3cost_end(&cost, 0U, true, true);
        return I2CBUS_TIMEOUT;
    }
    /* Remaining time was checked to fit uint32_t and the kernel TMO range. */
    const int locked =
        i2cown_begin(&g_owner, (uint32_t)(p_request->deadline_ms - now));
    if (0 != locked)
    {
        c3cost_end(&cost, 0U, I2CBUS_TIMEOUT == locked, true);
        return locked;
    }
    c3cost_acquired(&cost);
    const int result   = exchange(p_request);
    const int unlocked = i2cown_end(&g_owner);
    const int final    = (0 != unlocked) ? unlocked : result;
    c3cost_end(&cost,
               (0 == result) ? p_request->write_size + p_request->read_size
                             : 0U,
               I2CBUS_TIMEOUT == final,
               0 != final);
    return final;
}

/** @brief Use the same operating-time origin as sample timestamps. */
static int now_ms (void * p_context, uint64_t * p_now)
{
    if ((&g_state != p_context) || (!i2cown_is_open(&g_owner)))
    {
        return I2CBUS_STATE;
    }
    return (0 == rt_port_now_ms(p_now)) ? 0 : I2CBUS_IO;
}

/** @brief Yield for at least the requested delay without extending its budget.
 */
static int
delay_us (void * p_context, uint32_t duration_us, uint64_t deadline_ms)
{
    uint64_t       now         = 0U;
    const uint64_t duration_ms = ((uint64_t)duration_us + 999U) / 1000U;
    if (0 != now_ms(p_context, &now))
    {
        return I2CBUS_STATE;
    }
    if ((now >= deadline_ms) || (duration_ms >= (deadline_ms - now)))
    {
        return I2CBUS_TIMEOUT;
    }
    const uint64_t target = now + duration_ms;
    while (now < target)
    {
        if ((0 != c3bwait_yield()) || (0 != now_ms(p_context, &now)))
        {
            return I2CBUS_IO;
        }
        if (now >= deadline_ms)
        {
            return I2CBUS_TIMEOUT;
        }
    }
    return 0;
}

/** @brief Exclude USB, flash, strapping pins and the native UART's live pins.
 */
static bool valid_pin (uint8_t pin)
{
    return c3pin_valid(pin);
}

/** @brief Create the inherited-priority mutex that every session reuses. */
static int create_lock (void * p_context)
{
    (void)p_context; /* The permanent handle is module state, not session. */
    const int h_mutex = c3bwait_create();
    if (h_mutex <= 0)
    {
        return I2CBUS_IO;
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
        return I2CBUS_INVALID;
    }
    /* The checked value fits the nonnegative kernel TMO range. */
    const int locked = c3bwait_lock(gh_mutex, timeout_ms);
    if (0 == locked)
    {
        return 0;
    }
    return (C3BWAIT_TIMEOUT == locked) ? I2CBUS_TIMEOUT : I2CBUS_IO;
}

/** @brief Release the permanent mutex held by this task. */
static int unlock_bus (void * p_context)
{
    (void)p_context; /* The permanent handle is module state, not session. */
    return (0 == c3bwait_unlock(gh_mutex)) ? 0 : I2CBUS_IO;
}

/** @brief Reserve validated pins, then configure the controller and signals.
 */
static int start_bus (void * p_context, const void * p_settings)
{
    /* i2cown_open supplies a live, correctly aligned I2C configuration. */
    const i2cbus_config_t * p_config = p_settings;
    (void)p_context; /* Session settings are the module's g_state. */
    const int source_hz = c3bwait_source_hz();
    if (source_hz <= 0)
    {
        return I2CBUS_IO;
    }
    const uint32_t pins = (UINT32_C(1) << p_config->sda_gpio) |
                          (UINT32_C(1) << p_config->scl_gpio);
    if (0 != c3pin_reserve(pins, &g_bus))
    {
        return I2CBUS_STATE;
    }
    /* Positive int source frequency fits the target's uint32_t. */
    g_state = (c3i2c_state_t){
        .config = *p_config, .source_hz = (uint32_t)source_hz, .pins = pins};
    c3ihw_reset(g_state.source_hz, g_state.config.clock_hz);
    c3ihw_pins_open(p_config);
    return 0;
}

/** @brief Stop the controller; keep pin ownership if the ledger refuses it. */
static int stop_bus (void * p_context)
{
    (void)p_context; /* Session settings are the module's g_state. */
    c3ihw_stop(&g_state.config);
    if (0 != c3pin_release(g_state.pins, &g_bus))
    {
        return I2CBUS_STATE;
    }
    g_state = (c3i2c_state_t){0};
    return 0;
}

/** @brief Load at most seven commands and 32 transmit bytes, with final NACK.
 */
static void load_commands (const i2cbus_request_t * p_request, bool b_pause)
{
    uint8_t  bytes[32] = {0};
    size_t   count     = 0U;
    uint32_t slot      = 0U;
    c3ihw_command(slot++, C3IHW_RESTART_COMMAND, 0U, false);
    if (0U != p_request->write_size)
    {
        /* A seven-bit address shifted once fits the transmitted byte. */
        bytes[count++] = (uint8_t)(p_request->address << 1U);
        memcpy(&bytes[count], p_request->p_write, p_request->write_size);
        count += p_request->write_size;
        /* The common HAL limits write_size to 30. */
        c3ihw_command(slot++,
                      C3IHW_WRITE_COMMAND,
                      (uint32_t)(p_request->write_size + 1U),
                      false);
    }
    if (0U != p_request->read_size)
    {
        if (0U != p_request->write_size)
        {
            c3ihw_command(slot++, C3IHW_RESTART_COMMAND, 0U, false);
        }
        bytes[count++] = (uint8_t)((p_request->address << 1U) | 1U);
        c3ihw_command(slot++, C3IHW_WRITE_COMMAND, 1U, false);
        if ((!b_pause) && (p_request->read_size > 1U))
        {
            /* The common HAL limits read_size to 32. */
            c3ihw_command(slot++,
                          C3IHW_READ_COMMAND,
                          (uint32_t)(p_request->read_size - 1U),
                          false);
        }
        if (!b_pause)
        {
            c3ihw_command(slot++, C3IHW_READ_COMMAND, 1U, true);
        }
    }
    c3ihw_command(
        slot, b_pause ? C3IHW_END_COMMAND : C3IHW_STOP_COMMAND, 0U, false);
    /* The two address bytes plus payload fit the hardware's 32-byte FIFO. */
    c3ihw_write(bytes, (uint32_t)count);
}

/** @brief Poll with kernel yields and report distinct deadline/NACK failures.
 */
static int wait_complete (uint64_t deadline_ms, uint32_t completion)
{
    unsigned polls = 0U;
    for (;;)
    {
        uint64_t now = 0U;
        if (0 != rt_port_now_ms(&now))
        {
            return I2CBUS_IO;
        }
        if (now >= deadline_ms)
        {
            return I2CBUS_TIMEOUT;
        }
        uint32_t status = 0U;
        /* Poll raw status because peripheral interrupts remain disabled.
         * The pinned LL get_intr_mask helper reads masked int_status instead.
         */
        status = c3ihw_status();
        if (0U != (status & C3IHW_TIMEOUT))
        {
            return I2CBUS_TIMEOUT;
        }
        if (0U != (status & C3IHW_ARBITRATION))
        {
            return I2CBUS_IO;
        }
        if (0U != (status & C3IHW_NACK))
        {
            return I2CBUS_NACK;
        }
        if (0U != (status & completion))
        {
            return 0;
        }
        c3bwait_spin_us(10U);
        ++polls;
        if (25U == polls)
        {
            polls = 0U;
            if (0 != c3bwait_yield())
            {
                return I2CBUS_IO;
            }
        }
    }
}

/** @brief Continue the held read without a new START or releasing the lock. */
static int resume_read (const i2cbus_request_t * p_request)
{
    c3ihw_clear_status();
    int result = delay_us(
        &g_state, p_request->read_pause_ms * 1000U, p_request->deadline_ms);
    const uint32_t status = c3ihw_status();
    if ((0 == result) && (0U != (status & C3IHW_TIMEOUT)))
    {
        result = I2CBUS_TIMEOUT;
    }
    if ((0 == result) && (0U != (status & (C3IHW_NACK | C3IHW_ARBITRATION))))
    {
        result = I2CBUS_IO;
    }
    if (0 == result)
    {
        uint32_t slot = 0U;
        c3ihw_clear_commands();
        if (p_request->read_size > 1U)
        {
            c3ihw_command(slot++,
                          C3IHW_READ_COMMAND,
                          (uint32_t)(p_request->read_size - 1U),
                          false);
        }
        c3ihw_command(slot++, C3IHW_READ_COMMAND, 1U, true);
        c3ihw_command(slot, C3IHW_STOP_COMMAND, 0U, false);
        c3ihw_start();
        result = wait_complete(p_request->deadline_ms, C3IHW_COMPLETE);
    }
    return result;
}

/** @brief Run and reset one transfer while retaining the bus mutex. */
static int exchange (const i2cbus_request_t * p_request)
{
    uint64_t now = 0U;
    if (0 != rt_port_now_ms(&now))
    {
        return I2CBUS_IO;
    }
    if (now >= p_request->deadline_ms)
    {
        return I2CBUS_TIMEOUT;
    }
    c3ihw_clear_fifo();
    c3ihw_clear_status();
    c3ihw_clear_commands();
    const bool    b_pause = (0U != p_request->read_pause_ms);
    c3ihw_saved_t saved   = {0};
    if (b_pause)
    {
        c3ihw_hold(&saved);
    }
    load_commands(p_request, b_pause);
    c3ihw_start();
    int result = wait_complete(p_request->deadline_ms,
                               b_pause ? C3IHW_END : C3IHW_COMPLETE);
    if ((0 == result) && b_pause)
    {
        result = resume_read(p_request);
    }
    if ((0 == result) && (0U != p_request->read_size))
    {
        const uint32_t count = c3ihw_read_count();
        if (count != p_request->read_size)
        {
            result = I2CBUS_IO;
        }
        else
        {
            c3ihw_read(p_request->p_read, count);
        }
    }
    if (0 != result)
    {
        c3ihw_reset(g_state.source_hz, g_state.config.clock_hz);
    }
    else if (b_pause)
    {
        c3ihw_restore(&saved);
    }
    else
    {
        /* Ordinary successful transfers leave the normal timeout unchanged. */
    }
    return result;
}

/* End of file. */

