/**
 * @file c3uart.c
 * @brief Use the existing native rings without installing another UART driver.
 */
#include "c3uart.h"
#include "c3cost.h"
#include "c3ukern.h"
#include "workport.h"

typedef struct
{
    bool b_poisoned;
    bool b_attempted;
    bool b_ready;
    bool b_owned;
    bool b_active;
} c3uart_state_t;

static int open_port (void * p_context, const uartio_config_t * p_config);
static int close_port (void * p_context);
static int read_bytes (void *    p_context,
                       uint8_t * p_data,
                       size_t    capacity,
                       size_t *  p_count);
static int write_bytes (void *          p_context,
                        const uint8_t * p_data,
                        size_t          size,
                        size_t *        p_count);
static int flush_bytes (void * p_context);
static int now_ms (void * p_context, uint64_t * p_now);
static int reset_buffers (void);
static int discard_receive (void);
static int lock_port (void * p_context);
static int unlock_port (int result);
static int receive (uint8_t * p_data, size_t capacity, size_t * p_count);
static const uartio_ops_t g_operations = {.p_open  = open_port,
                                          .p_close = close_port,
                                          .p_read  = read_bytes,
                                          .p_write = write_bytes,
                                          .p_flush = flush_bytes,
                                          .p_now   = now_ms};
static c3uart_state_t     g_state      = {0};
static uartio_t g_port = {.p_operations = &g_operations, .p_context = &g_state};

/** @brief Borrow the singleton without touching the device or kernel. */
uartio_t * c3uart_port (void)
{
    return &g_port;
}

/** @brief Provision once before configuration restore or worker activation. */
int c3uart_init (void)
{
    if (!rt_port_is_owner())
    {
        return UARTIO_STATE;
    }
    if (g_state.b_attempted)
    {
        return g_state.b_ready ? 0 : UARTIO_STATE;
    }
    g_state.b_attempted = true;
    if (0 != c3ukern_init())
    {
        return UARTIO_IO;
    }
    /* Native open initializes rings and starts UART. Leave it stopped. */
    if (0 != reset_buffers())
    {
        return UARTIO_IO;
    }
    g_state.b_ready = true;
    return 0;
}

/** @brief Apply cached attributes with START, retaining the native descriptor.
 */
static int open_port (void * p_context, const uartio_config_t * p_config)
{
    if ((&g_state != p_context) || (!rt_port_is_owner()) ||
        (!g_state.b_ready) || (g_state.b_poisoned) || (g_state.b_owned))
    {
        return UARTIO_STATE;
    }
    if (!c3ukern_valid(p_config))
    {
        return UARTIO_INVALID;
    }
    const int locked = c3ukern_lock();
    if (0 != locked)
    {
        return locked;
    }
    g_state.b_owned = true;
    int result      = reset_buffers();
    if (0 == result)
    {
        result = c3ukern_attributes(p_config);
    }
    if (0 == result)
    {
        result = c3ukern_start();
    }
    if (0 != result)
    {
        /* Retain the claim and make one bounded stop attempt after failure. */
        if (0 != c3ukern_stop())
        {
            g_state.b_poisoned = true;
        }
    }
    g_state.b_active = (0 == result);
    return unlock_port(result);
}

/** @brief Release logical ownership only after bounded discard succeeds. */
static int close_port (void * p_context)
{
    if ((&g_state != p_context) ||
        ((!rt_port_is_owner()) && (!rt_port_is_cleanup_owner())) ||
        (!g_state.b_ready) || (g_state.b_poisoned))
    {
        return UARTIO_STATE;
    }
    const int locked = c3ukern_lock();
    if (0 != locked)
    {
        return locked;
    }
    g_state.b_active = false;
    const int result = unlock_port(reset_buffers());
    if (0 == result)
    {
        g_state.b_owned = false;
    }
    return result;
}

/** @brief Reject a whole batch if errors are observed before or after RX. */
static int read_bytes (void *    p_context,
                       uint8_t * p_data,
                       size_t    capacity,
                       size_t *  p_count)
{
    c3cost_t cost = {0};
    c3cost_begin(&cost);
    const int locked = lock_port(p_context);
    if (0 != locked)
    {
        c3cost_end(&cost, 0U, UARTIO_TIMEOUT == locked, locked < 0);
        return locked;
    }
    c3cost_acquired(&cost);
    int result = c3ukern_fault();
    if (0 == result)
    {
        result          = receive(p_data, capacity, p_count);
        const int fault = c3ukern_fault();
        if (0 != fault)
        {
            result = fault;
        }
    }
    const int final = unlock_port(result);
    c3cost_end(&cost,
               (0 == result) ? *p_count : 0U,
               UARTIO_TIMEOUT == final,
               final < 0);
    return final;
}

/** @brief Reserve the native ring's empty slot and enqueue only free space. */
static int write_bytes (void *          p_context,
                        const uint8_t * p_data,
                        size_t          size,
                        size_t *        p_count)
{
    c3cost_t cost = {0};
    c3cost_begin(&cost);
    const int locked = lock_port(p_context);
    if (0 != locked)
    {
        c3cost_end(&cost, 0U, UARTIO_TIMEOUT == locked, locked < 0);
        return locked;
    }
    c3cost_acquired(&cost);
    size_t space  = 0U;
    int    result = c3ukern_available(true, &space);
    if ((0 == result) && (0U != space))
    {
        const size_t count = (size < space) ? size : space;
        result             = c3ukern_write(p_data, count, p_count);
    }
    const int final = unlock_port(result);
    c3cost_end(&cost,
               (0 == result) ? *p_count : 0U,
               UARTIO_TIMEOUT == final,
               final < 0);
    return final;
}

/** @brief Drain only the entry snapshot, even while more input arrives. */
static int flush_bytes (void * p_context)
{
    c3cost_t cost = {0};
    c3cost_begin(&cost);
    const int locked = lock_port(p_context);
    if (0 != locked)
    {
        c3cost_end(&cost, 0U, UARTIO_TIMEOUT == locked, locked < 0);
        return locked;
    }
    c3cost_acquired(&cost);
    const int result = unlock_port(discard_receive());
    /* Discarded bytes are not successfully delivered payload. */
    c3cost_end(&cost, 0U, UARTIO_TIMEOUT == result, result < 0);
    return result;
}

/** @brief Discard at most the fixed native receive capacity. */
static int discard_receive (void)
{
    size_t pending = 0U;
    int    result  = c3ukern_available(false, &pending);
    while ((0 == result) && (0U != pending))
    {
        uint8_t      bytes[UTHOME_UART_BYTES] = {0};
        const size_t size = (pending < sizeof(bytes)) ? pending : sizeof(bytes);
        size_t       count = 0U;
        result             = receive(bytes, size, &count);
        if ((0 == result) && (0U == count))
        {
            result = UARTIO_IO;
        }
        pending -= count;
    }
    /* Clearing an observed hardware fault is part of explicit discard. */
    const int fault = c3ukern_fault();
    if (UARTIO_IO == fault)
    {
        result = fault;
    }
    return result;
}

/** @brief Kernel operating time matches every other typed sample. */
static int now_ms (void * p_context, uint64_t * p_now)
{
    if ((&g_state != p_context) || (!g_state.b_active) || (!g_state.b_owned) ||
        (g_state.b_poisoned))
    {
        return UARTIO_STATE;
    }
    return (0 == rt_port_now_ms(p_now)) ? 0 : UARTIO_IO;
}

/** @brief Reset hardware, mask interrupts, then discard both software rings. */
static int reset_buffers (void)
{
    /* STOP masks/clears all sources. START resets both hardware FIFOs and
     * enables RX/error only, never TX-empty. STOP again before ring access.
     * An RX ISR in that short interval is included in the later snapshot.
     * A byte already on the wire cannot be recalled. This is abort, not drain.
     */
    if (0 != c3ukern_stop())
    {
        return UARTIO_IO;
    }
    const int started = c3ukern_start();
    const int stopped = c3ukern_stop();
    if ((0 != started) || (0 != stopped))
    {
        return UARTIO_IO;
    }
    /* Native reads toggle the CPU interrupt, but all peripheral interrupt
     * sources stay masked. Only this wrapper may access the native device. */
    const size_t capacity = c3ukern_capacity();
    for (size_t index = 0U; index < capacity; ++index)
    {
        if (!c3ukern_discard_tx())
        {
            return discard_receive();
        }
    }
    return UARTIO_IO;
}

/** @brief Contention returns immediately; no driver call runs recursively. */
static int lock_port (void * p_context)
{
    if ((&g_state != p_context) || (!g_state.b_active) || (!g_state.b_owned) ||
        (g_state.b_poisoned))
    {
        return UARTIO_STATE;
    }
    return c3ukern_lock();
}

/** @brief Preserve an unlock failure as a transport error. */
static int unlock_port (int result)
{
    if (0 != c3ukern_unlock())
    {
        g_state.b_poisoned = true;
        g_state.b_active   = false;
        /* The mutex state is uncertain: stop IRQ-driven communication too.
         * Failure remains terminal; never release the caller's claim. */
        (void)c3ukern_stop();
        return UARTIO_IO;
    }
    return result;
}

/** @brief Exactly one serialized consumer means snapshot bytes cannot vanish.
 */
static int receive (uint8_t * p_data, size_t capacity, size_t * p_count)
{
    size_t pending = 0U;
    int    result  = c3ukern_available(false, &pending);
    if ((0 == result) && (0U != pending))
    {
        const size_t count = (capacity < pending) ? capacity : pending;
        result             = c3ukern_read(p_data, count, p_count);
    }
    return result;
}

/* End of file. */

