/** @file signalout.h
 * @brief Timed digital indication for an LED or an active buzzer. */
#ifndef UTHOME_SIGNALOUT_H
#define UTHOME_SIGNALOUT_H
#include "digiout.h"
#include <stdint.h>

typedef struct
{
    digiout_t output;
    uint32_t  on_ms;
    uint32_t  off_ms;
    bool      b_active_high;
} signalout_config_t;

typedef struct
{
    signalout_config_t config;
    uint64_t           epoch_ms;
    uint64_t           last_ms;
    bool               b_open;
    bool               b_enabled;
    bool               b_on;
    bool               b_fault;
} signalout_t;

/* Single caller; zero-initialize. Caller exclusively owns an already configured
 * inactive line. Active buzzer only: no passive-buzzer carrier is generated.
 * off_ms=0 means steady ON while enabled; otherwise repeat on/off intervals.
 * step never sleeps or catches up missed edges; output changes on each call.
 * Return 0 success, -1 bad arguments, -2 latched I/O fault, -3 clock rollback.
 * After I/O failure, close/reopen is required to enable output again. */
int signalout_open (signalout_t *              p_signal,
                    const signalout_config_t * p_config,
                    uint64_t                   now_ms);
int signalout_step (signalout_t * p_signal,
                    bool          b_enabled,
                    bool          b_muted,
                    uint64_t      now_ms);
int signalout_close (signalout_t * p_signal);
#endif
/* End of file. */

