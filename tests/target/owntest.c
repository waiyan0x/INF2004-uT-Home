/**
 * @file owntest.c
 * @brief Count every lock creation while sessions open, fail and close.
 */
#ifdef NDEBUG
#error "Ownership tests require assertions."
#endif
#include "owntest.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "i2cown.h"

#define OWNTEST_CYCLES  50U
#define OWNTEST_ADDRESS 0x76U

/* Records each backend call; the interface has no way to delete the lock. */
typedef struct
{
    unsigned creates;
    unsigned locks;
    unsigned unlocks;
    unsigned starts;
    unsigned stops;
    uint32_t last_timeout_ms;
    int      create_result;
    int      lock_result;
    int      unlock_result;
    int      start_result;
    int      stop_result;
    bool     b_owner;
    bool     b_held;
} owntest_fake_t;

static owntest_fake_t g_fake = {0};
static i2cbus_t       g_bus  = {0};

static int create_lock (void * p_context);
static bool owner (void * p_context);
static int lock_bus (void * p_context, uint32_t timeout_ms);
static int unlock_bus (void * p_context);
static int start_bus (void * p_context, const i2cbus_config_t * p_config);
static int stop_bus (void * p_context);
static void reset_fake (void);
static void verify_uninitialized (void);
static void verify_initialization (void);
static void verify_failed_initialization (void);
static void verify_repeated_sessions (void);
static void verify_owner_and_claims (void);
static void verify_in_flight_transfer (void);
static void verify_bounded_lock (void);
static void verify_retained_cleanup (void);
static void verify_failed_start (void);

static const i2cown_ops_t    g_operations = {.p_create = create_lock,
                                             .p_owner  = owner,
                                             .p_lock   = lock_bus,
                                             .p_unlock = unlock_bus,
                                             .p_start  = start_bus,
                                             .p_stop   = stop_bus};
static const i2cbus_config_t g_config     = {
        .port = 0U, .sda_gpio = 0U, .scl_gpio = 1U, .clock_hz = 100000U};

/** @brief Run each scenario against fresh permanent storage. */
void owntest_run (void)
{
    verify_uninitialized();
    verify_initialization();
    verify_failed_initialization();
    verify_repeated_sessions();
    verify_owner_and_claims();
    verify_in_flight_transfer();
    verify_bounded_lock();
    verify_retained_cleanup();
    verify_failed_start();
    puts("I2C permanent-lock ownership checks passed.");
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
        return I2CBUS_TIMEOUT;
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
        return I2CBUS_IO;
    }
    g_fake.b_held = false;
    return g_fake.unlock_result;
}

/** @brief Start a session only while the lock is held. */
static int start_bus (void * p_context, const i2cbus_config_t * p_config)
{
    assert((&g_fake == p_context) && (&g_config == p_config));
    assert(g_fake.b_held);
    ++g_fake.starts;
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
    g_fake = (owntest_fake_t){.b_owner = true};
    g_bus  = (i2cbus_t){0};
}

/** @brief Reject every session operation before startup initialization. */
static void verify_uninitialized (void)
{
    reset_fake();
    i2cown_t owner_state = {0};
    assert(I2CBUS_STATE == i2cown_open(&owner_state, &g_config));
    assert(I2CBUS_STATE == i2cown_close(&owner_state, &g_bus));
    assert(I2CBUS_STATE == i2cown_begin(&owner_state, 5U));
    assert(I2CBUS_STATE == i2cown_end(&owner_state));
    assert(!i2cown_is_open(&owner_state));
    assert(I2CBUS_INVALID == i2cown_init(NULL, &g_operations, &g_fake));
    const i2cown_ops_t incomplete = {.p_create = create_lock};
    assert(I2CBUS_INVALID == i2cown_init(&owner_state, &incomplete, &g_fake));
    assert(I2CBUS_INVALID == i2cown_init(&owner_state, &g_operations, NULL));
    assert(I2COWN_UNINIT == owner_state.status);
    assert((0U == g_fake.creates) && (0U == g_fake.locks));
}

/** @brief Create once; repeated initialization performs no creation call. */
static void verify_initialization (void)
{
    reset_fake();
    i2cown_t owner_state = {0};
    assert(0 == i2cown_init(&owner_state, &g_operations, &g_fake));
    for (unsigned index = 0U; index < 5U; ++index)
    {
        assert(0 == i2cown_init(&owner_state, &g_operations, &g_fake));
    }
    assert((1U == g_fake.creates) && (I2COWN_READY == owner_state.status));
    assert((I2COWN_CLOSED == owner_state.phase) && (0U == g_fake.locks));
}

/** @brief Keep a failed creation final: no retry, no session, no transfer. */
static void verify_failed_initialization (void)
{
    reset_fake();
    g_fake.create_result = I2CBUS_IO;
    i2cown_t owner_state = {0};
    assert(I2CBUS_IO == i2cown_init(&owner_state, &g_operations, &g_fake));
    g_fake.create_result = 0;
    assert(I2CBUS_STATE == i2cown_init(&owner_state, &g_operations, &g_fake));
    assert(I2CBUS_STATE == i2cown_open(&owner_state, &g_config));
    assert(I2CBUS_STATE == i2cown_begin(&owner_state, 5U));
    assert(I2CBUS_STATE == i2cown_close(&owner_state, &g_bus));
    assert((1U == g_fake.creates) && (I2COWN_FAILED == owner_state.status));
    assert((0U == g_fake.locks) && (0U == g_fake.starts));
}

/** @brief Reuse the one lock across many sessions and transfers. */
static void verify_repeated_sessions (void)
{
    reset_fake();
    i2cown_t owner_state = {0};
    assert(0 == i2cown_init(&owner_state, &g_operations, &g_fake));
    for (unsigned cycle = 0U; cycle < OWNTEST_CYCLES; ++cycle)
    {
        assert(0 == i2cown_open(&owner_state, &g_config));
        assert(i2cown_is_open(&owner_state));
        assert(0 == i2cown_begin(&owner_state, 5U));
        assert(0 == i2cown_end(&owner_state));
        assert(0 == i2cown_close(&owner_state, &g_bus));
        assert(!i2cown_is_open(&owner_state));
    }
    assert(1U == g_fake.creates);
    assert((OWNTEST_CYCLES == g_fake.starts) &&
           (OWNTEST_CYCLES == g_fake.stops));
    assert((3U * OWNTEST_CYCLES) == g_fake.locks);
    assert((g_fake.locks == g_fake.unlocks) && (!g_fake.b_held));
}

/** @brief Restrict lifecycle changes to the owner and claim-free buses. */
static void verify_owner_and_claims (void)
{
    reset_fake();
    i2cown_t owner_state = {0};
    assert(0 == i2cown_init(&owner_state, &g_operations, &g_fake));
    g_fake.b_owner = false;
    assert(I2CBUS_STATE == i2cown_open(&owner_state, &g_config));
    g_fake.b_owner = true;
    assert(0 == i2cown_open(&owner_state, &g_config));
    assert(I2CBUS_STATE == i2cown_open(&owner_state, &g_config));
    g_fake.b_owner = false;
    assert(I2CBUS_STATE == i2cown_close(&owner_state, &g_bus));
    g_fake.b_owner = true;
    /* Record the claim as i2cbus_claim does for a configured device. */
    g_bus.claims[OWNTEST_ADDRESS / 8U] =
        (uint8_t)(1U << (OWNTEST_ADDRESS % 8U));
    assert(I2CBUS_STATE == i2cown_close(&owner_state, &g_bus));
    assert((0U == g_fake.stops) && (i2cown_is_open(&owner_state)));
    g_bus.claims[OWNTEST_ADDRESS / 8U] = 0U;
    assert(I2CBUS_INVALID == i2cown_close(&owner_state, NULL));
    assert(0 == i2cown_close(&owner_state, &g_bus));
    assert(I2CBUS_STATE == i2cown_close(&owner_state, &g_bus));
    assert((1U == g_fake.starts) && (1U == g_fake.stops));
    assert((1U == g_fake.creates) && (!g_fake.b_held));
}

/** @brief Refuse to close, or to reopen, while another task holds the lock. */
static void verify_in_flight_transfer (void)
{
    reset_fake();
    i2cown_t owner_state = {0};
    assert(0 == i2cown_init(&owner_state, &g_operations, &g_fake));
    assert(0 == i2cown_open(&owner_state, &g_config));
    assert(0 == i2cown_begin(&owner_state, 5U));
    assert(I2CBUS_STATE == i2cown_close(&owner_state, &g_bus));
    assert((0U == g_fake.last_timeout_ms) && (0U == g_fake.stops));
    assert(I2COWN_OPEN == owner_state.phase);
    assert(0 == i2cown_end(&owner_state));
    assert(0 == i2cown_close(&owner_state, &g_bus));
    /* A stale transfer attempt may hold the lock briefly while closed. */
    g_fake.b_held = true;
    assert(I2CBUS_STATE == i2cown_open(&owner_state, &g_config));
    assert((0U == g_fake.last_timeout_ms) && (1U == g_fake.starts));
    g_fake.b_held = false;
    assert(0 == i2cown_open(&owner_state, &g_config));
    assert(0 == i2cown_close(&owner_state, &g_bus));
    assert(1U == g_fake.creates);
}

/** @brief Pass the remaining budget through; failures leave nothing held. */
static void verify_bounded_lock (void)
{
    reset_fake();
    i2cown_t owner_state = {0};
    assert(0 == i2cown_init(&owner_state, &g_operations, &g_fake));
    assert(I2CBUS_STATE == i2cown_begin(&owner_state, 7U));
    assert((7U == g_fake.last_timeout_ms) && (!g_fake.b_held));
    assert(0 == i2cown_open(&owner_state, &g_config));
    assert(0 == i2cown_begin(&owner_state, 9U));
    assert((9U == g_fake.last_timeout_ms) && (g_fake.b_held));
    assert(0 == i2cown_end(&owner_state));
    g_fake.lock_result = I2CBUS_TIMEOUT;
    assert(I2CBUS_TIMEOUT == i2cown_begin(&owner_state, 3U));
    g_fake.lock_result = I2CBUS_IO;
    assert(I2CBUS_IO == i2cown_begin(&owner_state, 3U));
    assert(I2CBUS_IO == i2cown_close(&owner_state, &g_bus));
    assert((I2COWN_OPEN == owner_state.phase) && (0U == g_fake.stops));
    g_fake.lock_result = 0;
    assert(0 == i2cown_begin(&owner_state, 3U));
    g_fake.unlock_result = I2CBUS_IO;
    assert(I2CBUS_IO == i2cown_end(&owner_state));
    g_fake.unlock_result = 0;
    assert(I2CBUS_IO == i2cown_end(&owner_state));
    assert(0 == i2cown_close(&owner_state, &g_bus));
    assert((!g_fake.b_held) && (1U == g_fake.creates));
}

/** @brief Retain a failed cleanup and block reuse until a retry succeeds. */
static void verify_retained_cleanup (void)
{
    reset_fake();
    i2cown_t owner_state = {0};
    assert(0 == i2cown_init(&owner_state, &g_operations, &g_fake));
    assert(0 == i2cown_open(&owner_state, &g_config));
    g_fake.stop_result = I2CBUS_STATE;
    assert(I2CBUS_STATE == i2cown_close(&owner_state, &g_bus));
    assert((I2COWN_RETAINED == owner_state.phase) && (!g_fake.b_held));
    assert(!i2cown_is_open(&owner_state));
    assert(I2CBUS_STATE == i2cown_begin(&owner_state, 5U));
    assert(I2CBUS_STATE == i2cown_open(&owner_state, &g_config));
    assert((1U == g_fake.starts) && (!g_fake.b_held));
    g_fake.stop_result = 0;
    assert(0 == i2cown_close(&owner_state, &g_bus));
    assert((I2COWN_CLOSED == owner_state.phase) && (2U == g_fake.stops));
    assert(0 == i2cown_open(&owner_state, &g_config));
    assert(0 == i2cown_close(&owner_state, &g_bus));
    assert(1U == g_fake.creates);
}

/** @brief Leave the session closed and the lock free after a failed start. */
static void verify_failed_start (void)
{
    reset_fake();
    i2cown_t owner_state = {0};
    assert(0 == i2cown_init(&owner_state, &g_operations, &g_fake));
    g_fake.start_result = I2CBUS_STATE;
    assert(I2CBUS_STATE == i2cown_open(&owner_state, &g_config));
    assert((I2COWN_CLOSED == owner_state.phase) && (!g_fake.b_held));
    assert(I2CBUS_STATE == i2cown_begin(&owner_state, 5U));
    g_fake.start_result = 0;
    assert(0 == i2cown_open(&owner_state, &g_config));
    assert(0 == i2cown_close(&owner_state, &g_bus));
    assert((2U == g_fake.starts) && (1U == g_fake.creates));
}

/* End of file. */

