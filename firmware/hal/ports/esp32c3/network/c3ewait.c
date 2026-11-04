/**
 * @file c3ewait.c
 * @brief Contain pinned kernel and SDK calls without changing event policy.
 */
#include "c3ewait.h"

#include "esp_timer.h"
#include "esp_private/cache_utils.h"
#include "sdkconfig.h"
#include <tk/tkernel.h>
#include <tk/syslib.h>

/** @brief Reserve one binary hint; failure retains any earlier resources. */
int c3ewait_create (void)
{
    const T_CSEM semaphore = {.sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1};
    const ID     h_notification = tk_cre_sem(&semaphore);
    return (h_notification > 0) ? h_notification : 0;
}

/** @brief A full binary hint is distinct from a broken notification. */
int c3ewait_signal (int h_notification)
{
    const ER result = tk_sig_sem(h_notification, 1);
    return (E_OK == result)
               ? 0
               : ((E_QOVR == result) ? C3EWAIT_FULL : C3EWAIT_ERROR);
}

/** @brief Translate one wait, preserving finite and infinite native budgets. */
int c3ewait_wait (int h_notification, uint32_t milliseconds)
{
    if ((UINT32_MAX != milliseconds) && (INT32_MAX < milliseconds))
    {
        return C3EWAIT_ERROR;
    }
    /* The checked finite value fits native TMO; infinite has its own value. */
    const TMO timeout =
        (UINT32_MAX == milliseconds) ? TMO_FEVR : (TMO)milliseconds;
    const ER result = tk_wai_sem(h_notification, 1, timeout);
    return (E_OK == result)
               ? 0
               : ((E_TMOUT == result) ? C3EWAIT_TIMEOUT : C3EWAIT_ERROR);
}

/** @brief Save the exact previous interrupt mask. */
uint32_t c3ewait_enter (void)
{
    return disint();
}

/** @brief Restore the caller's mask after metadata and hint publication. */
void c3ewait_leave (uint32_t saved)
{
    enaint(saved);
}

/** @brief Native notification paths require flash cache to be available. */
bool c3ewait_available (void)
{
    return spi_flash_cache_enabled();
}

/** @brief Polling permits startup; blocking requires ordinary task context. */
int c3ewait_context (bool b_blocking)
{
    T_RSYS state = {0};
    if ((E_OK != tk_ref_sys(&state)) || (0U != (state.sysstat & TSS_INDP)) ||
        (b_blocking && (TSS_TSK != state.sysstat)))
    {
        return C3EWAIT_ERROR;
    }
    return 0;
}

/** @brief Preserve negative clock failure for the deadline owner. */
int64_t c3ewait_now (void)
{
    return esp_timer_get_time();
}

/** @brief Use the effective SDK tick frequency, not a duplicated constant. */
uint32_t c3ewait_tick_hz (void)
{
    return CONFIG_FREERTOS_HZ;
}

/* End of file. */

