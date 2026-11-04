/**
 * @file c3bport.c
 * @brief Keep kernel headers outside the NimBLE include namespace.
 */
#include "c3bport.h"

#include "c3audit.h"
#include <tk/syslib.h>

/** @brief Protect callback state with the exact previous interrupt mask. */
uint32_t c3bport_enter (void)
{
    return disint();
}

/** @brief Restore the caller's mask, including nested exclusion. */
void c3bport_leave (uint32_t saved)
{
    enaint(saved);
}

/** @brief Native RELTIM and the owned millisecond parameter are both uint32. */
int c3bport_delay (uint32_t milliseconds)
{
    return (E_OK == tk_dly_tsk(milliseconds)) ? 0 : -1;
}

/** @brief End execution without deleting the permanent native task object. */
void c3bport_exit (void)
{
    tk_ext_tsk();
}

/** @brief Capture through the existing audited synchronization boundary. */
int c3bport_capture (memaudit_t * p_audit)
{
    return c3audit_capture(p_audit);
}

/** @brief Preserve the same startup/normal boundary as the USB core. */
int c3bport_seal (void)
{
    return c3audit_seal();
}

/* End of file. */

