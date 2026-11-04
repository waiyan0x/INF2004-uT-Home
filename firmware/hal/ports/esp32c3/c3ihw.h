/**
 * @file c3ihw.h
 * @brief Synchronous I2C register operations beneath the bounded adapter.
 */
#ifndef UTHOME_C3IHW_H
#define UTHOME_C3IHW_H

#include "i2cbus.h"

#define C3IHW_TIMEOUT     UINT32_C(1)
#define C3IHW_ARBITRATION UINT32_C(2)
#define C3IHW_NACK        UINT32_C(4)
#define C3IHW_END         UINT32_C(8)
#define C3IHW_COMPLETE    UINT32_C(16)

typedef enum
{
    C3IHW_RESTART_COMMAND = 0,
    C3IHW_WRITE_COMMAND,
    C3IHW_READ_COMMAND,
    C3IHW_END_COMMAND,
    C3IHW_STOP_COMMAND
} c3ihw_command_t;

typedef struct
{
    int      timeout;
    uint32_t scl;
    uint32_t main;
} c3ihw_saved_t;

/* Internal boundary: the adapter owns the bus mutex and validated settings.
 * Operations never poll, allocate or yield. FIFO calls use at most 32 bytes;
 * command slots are 0..7. Status normalizes raw, unmasked hardware bits. */
void c3ihw_pins_open (const i2cbus_config_t * p_config);
void c3ihw_stop (const i2cbus_config_t * p_config);
void c3ihw_reset (uint32_t source_hz, uint32_t clock_hz);
void c3ihw_clear_fifo (void);
void c3ihw_clear_status (void);
void c3ihw_clear_commands (void);
void c3ihw_command (uint32_t        slot,
                    c3ihw_command_t operation,
                    uint32_t        count,
                    bool            b_last_read);
void c3ihw_write (const uint8_t * p_bytes, uint32_t count);
void c3ihw_start (void);
uint32_t c3ihw_status (void);
void c3ihw_hold (c3ihw_saved_t * p_saved);
void c3ihw_restore (const c3ihw_saved_t * p_saved);
uint32_t c3ihw_read_count (void);
void c3ihw_read (uint8_t * p_bytes, uint32_t count);

#endif

/* End of file. */

