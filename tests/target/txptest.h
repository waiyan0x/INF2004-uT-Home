/**
 * @file txptest.h
 * @brief Host transaction-owner fixture with real HAL contract implementations.
 */
#ifndef UTHOME_TXPTEST_H
#define UTHOME_TXPTEST_H
#include "cfgbind.h"
#include "cfgmux.h"
#include <stdint.h>
const cfgbind_io_t * txptest_bind (void);
bool txptest_idle (const cfgbind_state_t * p_state);
bool txptest_mux (const cfgmux_t * p_mux);
int txptest_empty (const void * p_owner, bool * pb_empty);
int txptest_request (uint8_t * p_data, size_t capacity, size_t * p_size);
int txptest_result (const uint8_t * p_data, size_t size);
int txptest_start (void);
int txptest_send (const uint8_t * p_data, size_t size);
void txptest_pump (uint64_t now_ms, bool b_online, bool b_idle);
int txptest_reply (uint8_t * p_data);
int txptest_feed (const uint8_t * p_data, size_t size);
uint32_t txptest_observed (uint32_t field);
void txptest_drain (bool b_empty);
#endif

/* End of file. */

