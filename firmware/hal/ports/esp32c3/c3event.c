/**
 * @file c3event.c
 * @brief Bounded event broadcasts without runtime allocation or object
 * creation.
 */
#include "c3event.h"

#include "netevent.h"
#include "netwait.h"
#include "esp_timer.h"
#include "esp_private/cache_utils.h"
#include "sdkconfig.h"
#include <tk/tkernel.h>
#include <tk/syslib.h>

static netevent_t g_pool                             = {0};
static ID         gh_notifications[NETEVENT_WAITERS] = {0};
static bool       gb_attempted                       = false;
static bool       gb_ready                           = false;

static void notify (uint32_t mask);
static uint32_t wait_bits (void *   p_handle,
                           uint32_t bits,
                           bool     b_clear,
                           bool     b_all,
                           uint32_t set_bits,
                           uint32_t ticks);
static uint32_t await_result (uint32_t token, netwait_t const * p_deadline);

/** @brief Reserve notification slots once, before SDK clients run. */
int c3event_init (void)
{
    const T_CSEM semaphore = {.sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1};
    if (gb_attempted)
    {
        return gb_ready ? 0 : -1;
    }
    gb_attempted = true;
    for (uint32_t index = 0U; index < NETEVENT_WAITERS; ++index)
    {
        gh_notifications[index] = tk_cre_sem(&semaphore);
        if (0 >= gh_notifications[index])
        {
            /* Failed startup retains its partial resources until reset. */
            return -1;
        }
    }
    gb_ready = true;
    return 0;
}

/** @brief The dynamic SDK spelling leases fixed metadata. */
void * c3event_create (void)
{
    uint32_t saved    = 0U;
    void *   p_handle = NULL;
    if (gb_ready && (spi_flash_cache_enabled()))
    {
        saved    = disint();
        p_handle = netevent_create(&g_pool, NULL);
        enaint(saved);
    }
    return p_handle;
}

/** @brief The static SDK spelling retains its supplied opaque identity. */
void * c3event_static (void * p_key)
{
    uint32_t saved    = 0U;
    void *   p_handle = NULL;
    if (gb_ready && (NULL != p_key) && (spi_flash_cache_enabled()))
    {
        saved    = disint();
        p_handle = netevent_create(&g_pool, p_key);
        enaint(saved);
    }
    return p_handle;
}

/** @brief Deletion wakes pending callers and waits for their logical release.
 */
void c3event_delete (void * p_handle)
{
    uint32_t saved = 0U;
    uint32_t wake  = 0U;
    if (gb_ready && (spi_flash_cache_enabled()))
    {
        saved = disint();
        if (0 == netevent_release(&g_pool, p_handle, &wake))
        {
            notify(wake);
        }
        enaint(saved);
    }
}

/** @brief Return the bits remaining after all satisfied clear-on-exit waits. */
uint32_t c3event_set (void * p_handle, uint32_t bits)
{
    uint32_t saved  = 0U;
    uint32_t result = 0U;
    uint32_t wake   = 0U;
    if (gb_ready && (spi_flash_cache_enabled()))
    {
        saved = disint();
        if (0 == netevent_set(&g_pool, p_handle, bits, &result, &wake))
        {
            notify(wake);
        }
        enaint(saved);
    }
    return result;
}

/** @brief Bounded direct broadcast; native dispatch occurs at the ISR tail. */
int32_t c3event_set_isr (void * p_handle, uint32_t bits, int32_t * p_woken)
{
    uint32_t saved  = 0U;
    uint32_t result = 0U;
    uint32_t wake   = 0U;
    int      status = -1;
    if (gb_ready && (spi_flash_cache_enabled()))
    {
        saved  = disint();
        status = netevent_set(&g_pool, p_handle, bits, &result, &wake);
        if (0 == status)
        {
            notify(wake);
            if ((0U != wake) && (NULL != p_woken))
            {
                *p_woken = 1;
            }
        }
        enaint(saved);
    }
    return (0 == status) ? 1 : 0;
}

/** @brief Clear only the requested bits and report their previous state. */
uint32_t c3event_clear (void * p_handle, uint32_t bits)
{
    uint32_t saved  = 0U;
    uint32_t result = 0U;
    if (gb_ready && (spi_flash_cache_enabled()))
    {
        saved = disint();
        if (0 != netevent_clear(&g_pool, p_handle, bits, &result))
        {
            result = 0U;
        }
        enaint(saved);
    }
    return result;
}

/** @brief ISR clear reports accepted work, independent of previous bit value.
 */
int32_t c3event_clear_isr (void * p_handle, uint32_t bits)
{
    uint32_t saved  = 0U;
    uint32_t result = 0U;
    int      status = -1;
    if (gb_ready && (spi_flash_cache_enabled()))
    {
        saved  = disint();
        status = netevent_clear(&g_pool, p_handle, bits, &result);
        enaint(saved);
    }
    return (0 == status) ? 1 : 0;
}

/** @brief A zero clear mask reads without changing event state. */
uint32_t c3event_get (void * p_handle)
{
    return c3event_clear(p_handle, 0U);
}

/** @brief Ordinary waits use the configured SDK tick rate. */
uint32_t c3event_wait (
    void * p_handle, uint32_t bits, int32_t clear, int32_t all, uint32_t ticks)
{
    return wait_bits(p_handle, bits, 0 != clear, 0 != all, 0U, ticks);
}

/** @brief Barrier contribution and wait registration share one critical
 * section. */
uint32_t c3event_sync (void *   p_handle,
                       uint32_t set_bits,
                       uint32_t wanted,
                       uint32_t ticks)
{
    return wait_bits(p_handle, wanted, true, true, set_bits, ticks);
}

/** @brief Native binary counts are hints; stale signals cannot satisfy a wait.
 */
static void notify (uint32_t mask)
{
    for (uint32_t index = 0U; index < NETEVENT_WAITERS; ++index)
    {
        if (0U != (mask & (UINT32_C(1) << index)))
        {
            const ER status = tk_sig_sem(gh_notifications[index], 1);
            /* A full hint is already sufficient; state remains authoritative.
             */
            if ((E_OK != status) && (E_QOVR != status))
            {
                gb_ready = false;
            }
        }
    }
}

/** @brief Validate context before registering, then block outside exclusion. */
static uint32_t wait_bits (void *   p_handle,
                           uint32_t bits,
                           bool     b_clear,
                           bool     b_all,
                           uint32_t set_bits,
                           uint32_t ticks)
{
    uint32_t  saved    = 0U;
    uint32_t  result   = 0U;
    uint32_t  token    = NETEVENT_WAITERS;
    uint32_t  wake     = 0U;
    netwait_t deadline = {0};
    T_RSYS    state    = {0};
    int       status   = -1;
    if ((!gb_ready) || (!spi_flash_cache_enabled()) ||
        (E_OK != tk_ref_sys(&state)) || (0U != (state.sysstat & TSS_INDP)))
    {
        return 0U;
    }
    if (0U != ticks)
    {
        const int64_t now = esp_timer_get_time();
        if ((0 > now) || (TSS_TSK != state.sysstat) ||
            (0 !=
             netwait_init(&deadline, (uint64_t)now, ticks, CONFIG_FREERTOS_HZ)))
        {
            return 0U;
        }
    }
    saved  = disint();
    status = netevent_begin(&g_pool,
                            p_handle,
                            bits,
                            b_clear,
                            b_all,
                            set_bits,
                            0U != ticks,
                            &token,
                            &result,
                            &wake);
    notify(wake);
    enaint(saved);
    if (0 == status)
    {
        result = await_result(token, &deadline);
    }
    return (0 <= status) ? result : 0U;
}

/** @brief Recompute the absolute deadline after every stale/early wakeup. */
static uint32_t await_result (uint32_t token, netwait_t const * p_deadline)
{
    bool b_failed = false;
    for (;;)
    {
        uint32_t      saved     = 0U;
        uint32_t      result    = 0U;
        uint32_t      remaining = 0U;
        const int64_t now       = esp_timer_get_time();
        int           status    = -1;
        if ((0 > now) ||
            (0 != netwait_remaining(p_deadline, (uint64_t)now, &remaining)))
        {
            b_failed = true;
        }
        saved  = disint();
        status = netevent_finish(
            &g_pool, token, b_failed || (0U == remaining), &result);
        enaint(saved);
        if (0 != status)
        {
            return (0 < status) ? result : 0U;
        }
        /* netwait bounds finite values to the signed native timeout range. */
        const TMO timeout =
            (UINT32_MAX == remaining) ? TMO_FEVR : (TMO)remaining;
        const ER error = tk_wai_sem(gh_notifications[token], 1, timeout);
        b_failed       = (E_OK != error) && (E_TMOUT != error);
    }
}

/* End of file. */

