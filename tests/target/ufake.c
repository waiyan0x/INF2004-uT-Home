/**
 * @file ufake.c
 * @brief Model the owned UART boundary, with no vendor declarations.
 */
#include "ufake.h"
#include <string.h>

static int record (ufake_event_t event);
static ufake_state_t g_state = {
    .b_owner = true, .b_valid = true, .error = UARTIO_IO, .tx_free = 512U};

/** @brief Borrow test controls; each loaded fixture has fresh boot state. */
ufake_state_t * ufake_state (void)
{
    return &g_state;
}

/** @brief Native open starts communication before c3uart stops it. */
int ufake_init (void)
{
    const int result  = record(UFAKE_INIT);
    g_state.b_running = (0 == result);
    return result;
}

/** @brief Let each test control the platform-specific configuration verdict. */
bool ufake_valid (const uartio_config_t * p_config)
{
    return (NULL != p_config) && (g_state.b_valid);
}

/** @brief Model the patched ring, including its reserved empty slot. */
size_t ufake_capacity (void)
{
    return 513U;
}

/** @brief Record successful mutex acquisition separately from failures. */
int ufake_lock (void)
{
    const int result = record(UFAKE_LOCK);
    if (0 == result)
    {
        g_state.b_held = true;
    }
    return result;
}

/** @brief A failed unlock leaves the mutex state uncertain. */
int ufake_unlock (void)
{
    const int result = record(UFAKE_UNLOCK);
    if (0 == result)
    {
        g_state.b_held = false;
    }
    return result;
}

/** @brief Change communication state only when START succeeds. */
int ufake_start (void)
{
    const int result = record(UFAKE_START);
    if (0 == result)
    {
        g_state.b_running = true;
    }
    return result;
}

/** @brief Change communication state only when STOP succeeds. */
int ufake_stop (void)
{
    const int result = record(UFAKE_STOP);
    if (0 == result)
    {
        g_state.b_running = false;
    }
    return result;
}

/** @brief Attribute application is synchronous and failure-injectable. */
int ufake_attributes (const uartio_config_t * p_config)
{
    if (NULL == p_config)
    {
        g_state.b_invalid = true;
        return UARTIO_IO;
    }
    return record(UFAKE_ATTRIBUTES);
}

/** @brief Return independently injected errors before and after a read. */
int ufake_fault (void)
{
    const int result = record(UFAKE_FAULT);
    ++g_state.fault_calls;
    return (0 != result) ? result
                         : ((1U == g_state.fault_calls) ? g_state.fault_before
                                                        : g_state.fault_after);
}

/** @brief Snapshot current RX count or already adjusted TX free space. */
int ufake_available (bool b_transmit, size_t * p_count)
{
    const int result =
        record(b_transmit ? UFAKE_AVAILABLE_TX : UFAKE_AVAILABLE_RX);
    *p_count = b_transmit ? g_state.tx_free : g_state.rx_pending;
    return result;
}

/** @brief Simulate either a finite TX ring or a broken nonempty ring. */
bool ufake_discard_tx (void)
{
    (void)record(UFAKE_DISCARD_TX); /* This native Boolean call has no error. */
    if (g_state.b_stuck)
    {
        return true;
    }
    if (0U == g_state.tx_pending)
    {
        return false;
    }
    --g_state.tx_pending;
    return true;
}

/** @brief Deliver an exact bounded snapshot or leave the count untouched. */
int ufake_read (uint8_t * p_data, size_t size, size_t * p_count)
{
    const int result = record(UFAKE_READ);
    if ((size > UTHOME_UART_BYTES) || (size > g_state.rx_pending))
    {
        g_state.b_invalid = true;
        return UARTIO_IO;
    }
    if (0 == result)
    {
        memset(p_data, 0x5A, size);
        *p_count = size;
        /* size is at most 64; conversion fits the fixture's counters. */
        g_state.read_size += (uint32_t)size;
        if (!g_state.b_refill)
        {
            g_state.rx_pending -= (uint32_t)size;
        }
    }
    return result;
}

/** @brief Preserve partial acceptance when a native write reports failure. */
int ufake_write (const uint8_t * p_data, size_t size, size_t * p_count)
{
    const int result = record(UFAKE_WRITE);
    if ((NULL == p_data) || (size > UTHOME_UART_BYTES) ||
        (size > g_state.tx_free))
    {
        g_state.b_invalid = true;
        return UARTIO_IO;
    }
    *p_count = (0 == result) ? size : size / 2U;
    /* Successful bounds above make the conversion exact. */
    g_state.write_size += (uint32_t)*p_count;
    return result;
}

/** @brief Select the serialized lifecycle owner. */
bool ufake_owner (void)
{
    return g_state.b_owner;
}

/** @brief Select the separate dormant-worker cleanup owner. */
bool ufake_cleanup (void)
{
    return g_state.b_cleanup;
}

/** @brief Provide a stable monotonic value without accessing hardware. */
int ufake_now (uint64_t * p_now)
{
    *p_now = 42U;
    return 0;
}

/** @brief Cost collection is independent of the lifecycle under test. */
void ufake_begin (c3cost_t * p_state)
{
    memset(p_state, 0, sizeof(*p_state));
}

/** @brief Preserve the adapter's acquisition notification. */
void ufake_acquired (c3cost_t * p_state)
{
    p_state->b_acquired = true;
}

/** @brief Timing arithmetic has its own workmeter tests. */
void ufake_end (const c3cost_t * p_state,
                size_t           bytes,
                bool             b_timeout,
                bool             b_error)
{
    (void)p_state; /* No timing backend is installed in this lifecycle test. */
    (void)bytes;   /* Payload accounting is covered by the workmeter suite. */
    (void)b_timeout; /* The returned transport result is asserted by callers. */
    (void)b_error;   /* The returned transport result is asserted by callers. */
}

/** @brief Append a bounded call trace and fail exactly the selected call. */
static int record (ufake_event_t event)
{
    if (g_state.calls >= 2048U)
    {
        g_state.b_invalid = true;
        return UARTIO_IO;
    }
    /* The event enumeration is positive and below 13. */
    g_state.events[g_state.calls] = (uint32_t)event;
    ++g_state.calls;
    return (g_state.fail_call == g_state.calls) ? g_state.error : 0;
}

/* End of file. */

