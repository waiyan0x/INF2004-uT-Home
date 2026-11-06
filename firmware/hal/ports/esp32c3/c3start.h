/**
 * @file c3start.h
 * @brief Report isolated native startup phases without changing their result.
 */
#ifndef UTHOME_C3START_H
#define UTHOME_C3START_H

#include <sys/machine.h>
#include <tk/syslib.h>

/** @brief Trace device-manager entry and preserve its exact result. */
ER c3start_devmgr (void);
/** @brief Native device-manager definition retained by a source rename. */
ER c3start_native_devmgr (void);
/** @brief Trace the device-manager lock result and native event-flag ID. */
ER c3start_mlock (FastMLock * p_lock, const UB * p_name);
/** @brief Native multi-lock definition retained by a source rename. */
ER c3start_native_mlock (FastMLock * p_lock, const UB * p_name);
/** @brief Trace system-device startup and preserve its exact result. */
ER c3start_device (void);
/** @brief Native system-device definition retained by a source rename. */
ER c3start_native_device (void);
/** @brief Trace serial-device initialization and preserve its exact result. */
ER c3start_serial (UW unit);
/** @brief Native serial-device definition retained by a source rename. */
ER c3start_native_serial (UW unit);
/** @brief Trace ADC-device initialization and preserve its exact result. */
ER c3start_adc (UW unit);
/** @brief Native ADC-device definition retained by a source rename. */
ER c3start_native_adc (UW unit);

#endif

/* End of file. */

