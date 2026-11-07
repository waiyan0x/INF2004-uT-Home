/**
 * @file c3bwait.c
 * @brief Contain the pinned native bus mutex, delay and clock interfaces.
 */
#include "c3bwait.h"

#include <tk/tkernel.h>
#include "esp_private/esp_clk.h"
#include "esp_rom_sys.h"

/** @brief Create one inherited-priority mutex without retaining another copy.
 */
int c3bwait_create (void)
{
    const T_CMTX mutex   = {.mtxatr = TA_INHERIT};
    const ID     h_mutex = tk_cre_mtx(&mutex);
    return (h_mutex > 0) ? h_mutex : 0;
}

/** @brief Translate the native finite-wait result without retrying. */
int c3bwait_lock (int h_mutex, uint32_t timeout_ms)
{
    if (timeout_ms > UINT32_C(2147483647))
    {
        return C3BWAIT_ERROR;
    }
    /* The checked budget fits the nonnegative native TMO range. The pinned
     * kernel ID and this boundary's handle are both signed int on the target.
     */
    const ER result = tk_loc_mtx(h_mutex, (TMO)timeout_ms);
    return (E_OK == result)
               ? 0
               : ((E_TMOUT == result) ? C3BWAIT_TIMEOUT : C3BWAIT_ERROR);
}

/** @brief Release a held mutex; the adapter retains failure ownership. */
int c3bwait_unlock (int h_mutex)
{
    return (E_OK == tk_unl_mtx(h_mutex)) ? 0 : C3BWAIT_ERROR;
}

/** @brief Yield one native millisecond without extending a caller deadline. */
int c3bwait_yield (void)
{
    return (E_OK == tk_dly_tsk(1U)) ? 0 : C3BWAIT_ERROR;
}

/** @brief Delay a bounded microsecond interval with interrupts left enabled. */
void c3bwait_spin_us (uint32_t duration_us)
{
    esp_rom_delay_us(duration_us);
}

/** @brief Read the pinned board's crystal clock for peripheral divisors. */
int c3bwait_source_hz (void)
{
    return esp_clk_xtal_freq();
}

/* End of file. */

