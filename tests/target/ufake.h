/**
 * @file ufake.h
 * @brief Record UART boundary calls and inject synchronous failures.
 */
#ifndef UTHOME_UFAKE_H
#define UTHOME_UFAKE_H

#include "uartio.h"
#include "c3cost.h"

typedef enum
{
    UFAKE_INIT = 1,
    UFAKE_LOCK,
    UFAKE_UNLOCK,
    UFAKE_START,
    UFAKE_STOP,
    UFAKE_ATTRIBUTES,
    UFAKE_FAULT,
    UFAKE_AVAILABLE_RX,
    UFAKE_AVAILABLE_TX,
    UFAKE_DISCARD_TX,
    UFAKE_READ,
    UFAKE_WRITE
} ufake_event_t;

typedef struct
{
    bool     b_owner;
    bool     b_cleanup;
    bool     b_valid;
    bool     b_running;
    bool     b_held;
    bool     b_refill;
    bool     b_stuck;
    bool     b_invalid;
    uint32_t calls;
    uint32_t fail_call;
    int32_t  error;
    uint32_t rx_pending;
    uint32_t tx_pending;
    uint32_t tx_free;
    uint32_t read_size;
    uint32_t write_size;
    uint32_t fault_calls;
    int32_t  fault_before;
    int32_t  fault_after;
    uint32_t events[2048];
} ufake_state_t;

ufake_state_t * ufake_state (void);
int ufake_init (void);
bool ufake_valid (const uartio_config_t * p_config);
size_t ufake_capacity (void);
int ufake_lock (void);
int ufake_unlock (void);
int ufake_start (void);
int ufake_stop (void);
int ufake_attributes (const uartio_config_t * p_config);
int ufake_fault (void);
int ufake_available (bool b_transmit, size_t * p_count);
bool ufake_discard_tx (void);
int ufake_read (uint8_t * p_data, size_t size, size_t * p_count);
int ufake_write (const uint8_t * p_data, size_t size, size_t * p_count);
bool ufake_owner (void);
bool ufake_cleanup (void);
int ufake_now (uint64_t * p_now);
void ufake_begin (c3cost_t * p_state);
void ufake_acquired (c3cost_t * p_state);
void ufake_end (const c3cost_t * p_state,
                size_t           bytes,
                bool             b_timeout,
                bool             b_error);

#endif

/* End of file. */

