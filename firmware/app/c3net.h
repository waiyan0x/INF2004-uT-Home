/**
 * @file c3net.h
 * @brief Opt-in experimental wireless services composed by the app owner.
 */
#ifndef UTHOME_C3NET_H
#define UTHOME_C3NET_H
#include "cfgmux.h"
#include <stdint.h>
/* Start before audit sealing, pump after USB/config service. Never applies a
 * profile directly or changes task/notification/timer compatibility. HTTP
 * submissions are routed only through the supplied serialized cfgmux owner.
 * Errors are diagnostic only: callers continue local sensor/display operation.
 */
int c3net_start (const cfgctrl_t * p_control, cfgmux_t * p_mux);
int c3net_step (const cfgctrl_t * p_control, uint64_t now_ms);
#endif

/* End of file. */

