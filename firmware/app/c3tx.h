/**
 * @file c3tx.h
 * @brief Serialized application-owned runtime MQTT transaction service.
 */
#ifndef UTHOME_C3TX_H
#define UTHOME_C3TX_H
struct cfgbind_state;
#include "uthome/txjob.h"
#include "cfgmux.h"
int c3tx_start (const struct cfgbind_state * p_binding, const cfgmux_t * p_mux);
/* Prevent configuration changes while asynchronous UART/cleanup retains pins.
 */
bool c3tx_busy (void);
/* Same application owner as USB, HTTP and MQTT. Never called by an ISR. */
void c3tx_step (uint64_t now_ms, uint32_t generation, bool b_online);
const txjob_view_t * c3tx_view (void);
#endif

/* End of file. */

