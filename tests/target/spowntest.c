/**
 * @file spowntest.c
 * @brief Count every lock creation while SPI sessions open, fail and close.
 */
#ifdef NDEBUG
#error "Ownership tests require assertions."
#endif
#include "spowntest.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "spiown.h"

#define SPOWNTEST_CYCLES 50U

/* Records each backend call; the interface has no way to delete the lock. */
typedef struct
{
    unsigned     creates;
    unsigned     locks;
    unsigned     unlocks;
    unsigned     starts;
    unsigned     stops;
    uint32_t     last_timeout_ms;
    const void * p_last_settings;
    int          create_result;
    int          lock_result;
    int          unlock_result;
    int          start_result;
    int          stop_result;
    bool         b_owner;
    bool         b_held;
} spowntest_fake_t;

static spowntest_fake_t g_fake     = {0};
static spibus_t         g_bus      = {0};
static const int        g_settings = 2;
static const int        g_device   = 0;

static int create_lock (void * p_context);
static bool owner (void * p_context);
static int lock_bus (void * p_context, uint32_t timeout_ms);
static int unlock_bus (void * p_context);
static int start_bus (void * p_context, const void * p_settings);
static int stop_bus (void * p_context);
static void reset_fake (void);
static void verify_uninitialized (void);
static void verify_initialization (void);
static void verify_failed_initialization (void);
static void verify_repeated_sessions (void);
static void verify_owner_and_devices (void);
static void verify_cleanup_detach (void);
static void verify_in_flight_transfer (void);
static void verify_bounded_lock (void);
static void verify_retained_cleanup (void);
static void verify_failed_start (void);

static const spiown_ops_t g_operations = {.p_create = create_lock,
                                          .p_owner  = owner,
                                          .p_lock   = lock_bus,
                                          .p_unlock = unlock_bus,
                                          .p_start  = start_bus,
                                          .p_stop   = stop_bus};

/** @brief Run each scenario against fresh permanent storage. */
void spowntest_run (void)
{
    verify_uninitialized();
    verify_initialization();
    verify_failed_initialization();
    verify_repeated_sessions();
    verify_owner_and_devices();
    verify_cleanup_detach();
    verify_in_flight_transfer();
    verify_bounded_lock();
    verify_retained_cleanup();
    verify_failed_start();
    puts("SPI permanent-lock ownership checks passed.");
}

/** @brief Count the single permitted creation call. */
static int create_lock (void * p_context)
{
    assert(&g_fake == p_context);
    ++g_fake.creates;
    return g_fake.create_result;
}

/** @brief Report the configured lifecycle-owner state. */
static bool owner (void * p_context)
{
    assert(&g_fake == p_context);
    return g_fake.b_owner;
}

/** @brief Model one nonrecursive mutex; a held lock times out. */
static int lock_bus (void * p_context, uint32_t timeout_ms)
{
    assert(&g_fake == p_context);
    ++g_fake.locks;
    g_fake.last_timeout_ms = timeout_ms;
    if (0 != g_fake.lock_result)
    {
        return g_fake.lock_result;
    }
    if (g_fake.b_held)
    {
        return SPIBUS_TIMEOUT;
    }
    g_fake.b_held = true;
    return 0;
}

/** @brief Reject unlocking a lock that is not held. */
static int unlock_bus (void * p_context)
{
    assert(&g_fake == p_context);
    ++g_fake.unlocks;
    if (!g_fake.b_held)
    {
        return SPIBUS_IO;
    }
    g_fake.b_held = false;
    return g_fake.unlock_result;
}

/** @brief Start a session only while the lock is held. */
static int start_bus (void * p_context, const void * p_settings)
{
    assert(&g_fake == p_context);
    assert(g_fake.b_held);
    ++g_fake.starts;
    g_fake.p_last_settings = p_settings;
    return g_fake.start_result;
}

/** @brief Stop a session only while the lock is held. */
static int stop_bus (void * p_context)
{
    assert(&g_fake == p_context);
    assert(g_fake.b_held);
    ++g_fake.stops;
    return g_fake.stop_result;
}

/** @brief Restore default successful callbacks and clear all counters. */
static void reset_fake (void)
{
    g_fake = (spowntest_fake_t){.b_owner = true};
    g_bus  = (spibus_t){0};
}

/** @brief Reject every session operation before startup initialization. */
static void verify_uninitialized (void)
{
    reset_fake();
    spiown_t owner_state = {0};
    assert(SPIBUS_STATE == spiown_open(&owner_state, &g_bus, &g_settings));
    assert(SPIBUS_STATE == spiown_close(&owner_state, &g_bus));
    assert(SPIBUS_STATE == spiown_begin(&owner_state, 5U));
    assert(SPIBUS_STATE == spiown_end(&owner_state));
    assert(!spiown_is_open(&owner_state));
    assert(SPIBUS_INVALID == spiown_init(NULL, &g_operations, &g_fake));
    const spiown_ops_t incomplete = {.p_create = create_lock};
    assert(SPIBUS_INVALID == spiown_init(&owner_state, &incomplete, &g_fake));
    assert(SPIBUS_INVALID == spiown_init(&owner_state, &g_operations, NULL));
    assert(SPIOWN_UNINIT == owner_state.status);
    assert((0U == g_fake.creates) && (0U == g_fake.locks));
}

/** @brief Create once; repeated initialization performs no creation call. */
static void verify_initialization (void)
{
    reset_fake();
    spiown_t owner_state = {0};
    assert(0 == spiown_init(&owner_state, &g_operations, &g_fake));
    for (unsigned index = 0U; index < 5U; ++index)
    {
        assert(0 == spiown_init(&owner_state, &g_operations, &g_fake));
    }
    assert((1U == g_fake.creates) && (SPIOWN_READY == owner_state.status));
    assert((SPIOWN_CLOSED == owner_state.phase) && (0U == g_fake.locks));
}

/** @brief Keep a failed creation final: no retry, no session, no transfer. */
static void verify_failed_initialization (void)
{
    reset_fake();
    g_fake.create_result = SPIBUS_IO;
    spiown_t owner_state = {0};
    assert(SPIBUS_IO == spiown_init(&owner_state, &g_operations, &g_fake));
    g_fake.create_result = 0;
    assert(SPIBUS_STATE == spiown_init(&owner_state, &g_operations, &g_fake));
    assert(SPIBUS_STATE == spiown_open(&owner_state, &g_bus, &g_settings));
    assert(SPIBUS_STATE == spiown_begin(&owner_state, 5U));
    assert(SPIBUS_STATE == spiown_close(&owner_state, &g_bus));
    assert((1U == g_fake.creates) && (SPIOWN_FAILED == owner_state.status));
    assert((0U == g_fake.locks) && (0U == g_fake.starts));
}

/** @brief Reuse the one lock across many sessions and transfers. */
static void verify_repeated_sessions (void)
{
    reset_fake();
    spiown_t owner_state = {0};
    assert(0 == spiown_init(&owner_state, &g_operations, &g_fake));
    assert(SPIBUS_INVALID == spiown_open(&owner_state, &g_bus, NULL));
    for (unsigned cycle = 0U; cycle < SPOWNTEST_CYCLES; ++cycle)
    {
        assert(0 == spiown_open(&owner_state, &g_bus, &g_settings));
        assert(&g_settings == g_fake.p_last_settings);
        assert(spiown_is_open(&owner_state));
        assert(0 == spiown_begin(&owner_state, 5U));
        assert(0 == spiown_end(&owner_state));
        assert(0 == spiown_close(&owner_state, &g_bus));
        assert(!spiown_is_open(&owner_state));
    }
    assert(1U == g_fake.creates);
    assert((SPOWNTEST_CYCLES == g_fake.starts) &&
           (SPOWNTEST_CYCLES == g_fake.stops));
    assert((3U * SPOWNTEST_CYCLES) == g_fake.locks);
    assert((g_fake.locks == g_fake.unlocks) && (!g_fake.b_held));
}

/** @brief Restrict lifecycle changes to the owner and device-free buses. */
static void verify_owner_and_devices (void)
{
    reset_fake();
    spiown_t owner_state = {0};
    assert(0 == spiown_init(&owner_state, &g_operations, &g_fake));
    g_fake.b_owner = false;
    assert(SPIBUS_STATE == spiown_open(&owner_state, &g_bus, &g_settings));
    g_fake.b_owner = true;
    /* A device recorded while closed is stray; open must not discard it. */
    g_bus.p_devices[1] = &g_device;
    assert(SPIBUS_STATE == spiown_open(&owner_state, &g_bus, &g_settings));
    assert((0U == g_fake.starts) && (0U == g_fake.locks));
    g_bus.p_devices[1] = NULL;
    assert(0 == spiown_open(&owner_state, &g_bus, &g_settings));
    assert(SPIBUS_STATE == spiown_open(&owner_state, &g_bus, &g_settings));
    g_fake.b_owner = false;
    assert(SPIBUS_STATE == spiown_close(&owner_state, &g_bus));
    g_fake.b_owner = true;
    /* Record the device as spibus_attach does for the configured OLED. */
    g_bus.p_devices[0] = &g_device;
    assert(SPIBUS_STATE == spiown_close(&owner_state, &g_bus));
    assert((0U == g_fake.stops) && (spiown_is_open(&owner_state)));
    g_bus.p_devices[0] = NULL;
    assert(SPIBUS_INVALID == spiown_close(&owner_state, NULL));
    assert(0 == spiown_close(&owner_state, &g_bus));
    assert(SPIBUS_STATE == spiown_close(&owner_state, &g_bus));
    assert((1U == g_fake.starts) && (1U == g_fake.stops));
    assert((1U == g_fake.creates) && (!g_fake.b_held));
}

/** @brief Keep detach possible during worker cleanup, where owner is false. */
static void verify_cleanup_detach (void)
{
    reset_fake();
    spiown_t owner_state = {0};
    assert(0 == spiown_init(&owner_state, &g_operations, &g_fake));
    assert(0 == spiown_open(&owner_state, &g_bus, &g_settings));
    /* The adapter gates attach/detach on this hint, not on ownership. */
    g_fake.b_owner = false;
    assert(spiown_is_open(&owner_state));
    assert(SPIBUS_STATE == spiown_close(&owner_state, &g_bus));
    g_fake.b_owner = true;
    assert(0 == spiown_close(&owner_state, &g_bus));
    g_fake.b_owner = false;
    assert(!spiown_is_open(&owner_state));
    assert((1U == g_fake.stops) && (1U == g_fake.creates));
}

/** @brief Refuse to close, or to reopen, while another task holds the lock. */
static void verify_in_flight_transfer (void)
{
    reset_fake();
    spiown_t owner_state = {0};
    assert(0 == spiown_init(&owner_state, &g_operations, &g_fake));
    assert(0 == spiown_open(&owner_state, &g_bus, &g_settings));
    assert(0 == spiown_begin(&owner_state, 5U));
    assert(SPIBUS_STATE == spiown_close(&owner_state, &g_bus));
    assert((0U == g_fake.last_timeout_ms) && (0U == g_fake.stops));
    assert(SPIOWN_OPEN == owner_state.phase);
    assert(0 == spiown_end(&owner_state));
    assert(0 == spiown_close(&owner_state, &g_bus));
    /* A stale transfer attempt may hold the lock briefly while closed. */
    g_fake.b_held = true;
    assert(SPIBUS_STATE == spiown_open(&owner_state, &g_bus, &g_settings));
    assert((0U == g_fake.last_timeout_ms) && (1U == g_fake.starts));
    g_fake.b_held = false;
    assert(0 == spiown_open(&owner_state, &g_bus, &g_settings));
    assert(0 == spiown_close(&owner_state, &g_bus));
    assert(1U == g_fake.creates);
}

/** @brief Pass the remaining budget through; failures leave nothing held. */
static void verify_bounded_lock (void)
{
    reset_fake();
    spiown_t owner_state = {0};
    assert(0 == spiown_init(&owner_state, &g_operations, &g_fake));
    /* A transfer after close takes and returns the lock, then refuses. */
    assert(SPIBUS_STATE == spiown_begin(&owner_state, 7U));
    assert((7U == g_fake.last_timeout_ms) && (!g_fake.b_held));
    assert(0 == spiown_open(&owner_state, &g_bus, &g_settings));
    assert(0 == spiown_begin(&owner_state, 9U));
    assert((9U == g_fake.last_timeout_ms) && (g_fake.b_held));
    assert(0 == spiown_end(&owner_state));
    g_fake.lock_result = SPIBUS_TIMEOUT;
    assert(SPIBUS_TIMEOUT == spiown_begin(&owner_state, 3U));
    g_fake.lock_result = SPIBUS_IO;
    assert(SPIBUS_IO == spiown_begin(&owner_state, 3U));
    assert(SPIBUS_IO == spiown_close(&owner_state, &g_bus));
    assert((SPIOWN_OPEN == owner_state.phase) && (0U == g_fake.stops));
    g_fake.lock_result = 0;
    assert(0 == spiown_begin(&owner_state, 3U));
    g_fake.unlock_result = SPIBUS_IO;
    assert(SPIBUS_IO == spiown_end(&owner_state));
    g_fake.unlock_result = 0;
    assert(SPIBUS_IO == spiown_end(&owner_state));
    assert(0 == spiown_close(&owner_state, &g_bus));
    assert((!g_fake.b_held) && (1U == g_fake.creates));
}

/** @brief Retain a failed cleanup and block reuse until a retry succeeds. */
static void verify_retained_cleanup (void)
{
    reset_fake();
    spiown_t owner_state = {0};
    assert(0 == spiown_init(&owner_state, &g_operations, &g_fake));
    assert(0 == spiown_open(&owner_state, &g_bus, &g_settings));
    g_fake.stop_result = SPIBUS_STATE;
    assert(SPIBUS_STATE == spiown_close(&owner_state, &g_bus));
    assert((SPIOWN_RETAINED == owner_state.phase) && (!g_fake.b_held));
    /* Attach, clock and delay callbacks see a closed session. */
    assert(!spiown_is_open(&owner_state));
    assert(SPIBUS_STATE == spiown_begin(&owner_state, 5U));
    assert(SPIBUS_STATE == spiown_open(&owner_state, &g_bus, &g_settings));
    assert((1U == g_fake.starts) && (!g_fake.b_held));
    g_fake.stop_result = 0;
    assert(0 == spiown_close(&owner_state, &g_bus));
    assert((SPIOWN_CLOSED == owner_state.phase) && (2U == g_fake.stops));
    assert(0 == spiown_open(&owner_state, &g_bus, &g_settings));
    assert(0 == spiown_close(&owner_state, &g_bus));
    assert(1U == g_fake.creates);
}

/** @brief Leave the session closed and the lock free after a failed start. */
static void verify_failed_start (void)
{
    reset_fake();
    spiown_t owner_state = {0};
    assert(0 == spiown_init(&owner_state, &g_operations, &g_fake));
    g_fake.start_result = SPIBUS_STATE;
    assert(SPIBUS_STATE == spiown_open(&owner_state, &g_bus, &g_settings));
    assert((SPIOWN_CLOSED == owner_state.phase) && (!g_fake.b_held));
    assert(SPIBUS_STATE == spiown_begin(&owner_state, 5U));
    g_fake.start_result = 0;
    assert(0 == spiown_open(&owner_state, &g_bus, &g_settings));
    assert(0 == spiown_close(&owner_state, &g_bus));
    assert((2U == g_fake.starts) && (1U == g_fake.creates));
}

/* End of file. */

