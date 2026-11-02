/**
 * @file busfake.h
 * @brief Deterministic register/timing boundary for the real C3 bus adapters.
 */
#ifndef UTHOME_BUSFAKE_H
#define UTHOME_BUSFAKE_H

#include "c3bwait.h"
#include "c3ihw.h"
#include "c3shw.h"
#include "c3cost.h"

int busfake_wait_create (void);
int busfake_wait_lock (int h_mutex, uint32_t timeout_ms);
int busfake_wait_unlock (int h_mutex);
int busfake_wait_yield (void);
void busfake_wait_spin_us (uint32_t duration_us);
int busfake_wait_source_hz (void);
void busfake_i_pins_open (const i2cbus_config_t * p_config);
void busfake_i_stop (const i2cbus_config_t * p_config);
void busfake_i_reset (uint32_t source_hz, uint32_t clock_hz);
void busfake_i_clear_fifo (void);
void busfake_i_clear_status (void);
void busfake_i_clear_commands (void);
void busfake_i_command (uint32_t        slot,
                        c3ihw_command_t operation,
                        uint32_t        count,
                        bool            b_last_read);
void busfake_i_write (const uint8_t * p_bytes, uint32_t count);
void busfake_i_start (void);
uint32_t busfake_i_status (void);
void busfake_i_hold (c3ihw_saved_t * p_saved);
void busfake_i_restore (const c3ihw_saved_t * p_saved);
uint32_t busfake_i_read_count (void);
void busfake_i_read (uint8_t * p_bytes, uint32_t count);
void busfake_s_pins_open (const c3spi_config_t * p_config);
void busfake_s_stop (const c3spi_config_t * p_config);
void busfake_s_attach (uint8_t pin);
void busfake_s_detach (uint8_t pin);
void busfake_s_select (uint8_t pin, bool b_selected);
void busfake_s_reset (void);
int busfake_s_clock (int source_hz, uint32_t clock_hz);
int busfake_s_configure (int source_hz, const spibus_config_t * p_config);
bool busfake_s_ready (bool b_update);
void busfake_s_prepare (const uint8_t * p_bytes, size_t size, bool b_read);
void busfake_s_start (void);
void busfake_s_read (uint8_t * p_bytes, size_t size);

int busfake_now (uint64_t * p_now);
bool busfake_owner (void);
bool busfake_pin_valid (uint8_t pin);
int busfake_pin_reserve (uint32_t mask, const void * p_owner);
int busfake_pin_release (uint32_t mask, const void * p_owner);
void busfake_cost_begin (c3cost_t * p_state);
void busfake_cost_acquired (c3cost_t * p_state);
void busfake_cost_end (const c3cost_t * p_state,
                       size_t           bytes,
                       bool             b_timeout,
                       bool             b_error);
void busfake_verify (int scenario);

#endif

/* End of file. */

