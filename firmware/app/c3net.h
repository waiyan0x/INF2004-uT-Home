/**
 * @file c3net.h
 * @brief Opt-in experimental wireless services composed by the app owner.
 */
#ifndef UTHOME_C3NET_H
#define UTHOME_C3NET_H
#include "cfgctrl.h"
#include <stdint.h>
/* Start before audit sealing, pump after USB/config service. Never applies a
 * profile directly or changes task/notification/timer compatibility.
 * Errors are diagnostic only: callers continue local sensor/display operation.
 */
int c3net_start (const cfgctrl_t * p_control);
/* Same boot identity used by diagnostics and the MQTT transaction owner. */
uint64_t c3net_boot (void);
int c3net_step (const cfgctrl_t * p_control, uint64_t now_ms);
/* Wi-Fi-only, owner-serialized 24-byte status; no credentials or I/O. */
int c3net_observe (uint8_t * p_data, size_t capacity, size_t * p_size);
#endif

/* End of file. */

