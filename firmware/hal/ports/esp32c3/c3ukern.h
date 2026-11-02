/**
 * @file c3ukern.h
 * @brief Synchronous native UART operations beneath the lifecycle adapter.
 */
#ifndef UTHOME_C3UKERN_H
#define UTHOME_C3UKERN_H

#include "uartio.h"

/* Internal boundary: c3uart owns sequencing, locking and the boot-once guard.
 * Native handles are retained until reboot, including partial initialization.
 * Calls return UARTIO results; lock polls and byte operations never await I/O.
 * Caller buffers are borrowed only for each synchronous call. */
int c3ukern_init (void);
bool c3ukern_valid (const uartio_config_t * p_config);
size_t c3ukern_capacity (void);
int c3ukern_lock (void);
int c3ukern_unlock (void);
int c3ukern_start (void);
int c3ukern_stop (void);
int c3ukern_attributes (const uartio_config_t * p_config);
int c3ukern_fault (void);
int c3ukern_available (bool b_transmit, size_t * p_count);
/* Return true when one queued transmit byte was discarded. */
bool c3ukern_discard_tx (void);
/* Size is bounded by UTHOME_UART_BYTES. Read requires exactly size bytes;
 * write reports accepted bytes even on failure. Outputs start at zero. */
int c3ukern_read (uint8_t * p_data, size_t size, size_t * p_count);
int c3ukern_write (const uint8_t * p_data, size_t size, size_t * p_count);

#endif

/* End of file. */

