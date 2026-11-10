/**
 * @file txport.h
 * @brief Generic transaction adapters using existing claimed HAL interfaces.
 */
#ifndef UTHOME_TXPORT_H
#define UTHOME_TXPORT_H
#include "uthome/txjob.h"
#include "cfgbind.h"
typedef int txport_tx_empty_t (const void * p_owner, bool * pb_empty);

typedef struct
{
    const cfgbind_io_t * p_io;
    txport_tx_empty_t *  p_tx_empty;
    txjob_rule_t         rule;
    i2cbus_t *           p_i2c;
    spibus_t *           p_spi;
    uartio_t *           p_uart;
    spibus_device_t      spi;
    digiout_t            control_line;
    uint64_t             deadline_ms;
    uint64_t             settle_ms;
    uint8_t              received;
    uint8_t              written;
    uint8_t              receive[TXJOB_RX];
    bool                 b_i2c_claimed;
    bool                 b_spi_claimed;
    bool                 b_settling;
} txport_t;

/* Lifetime storage; the application owner checks that all peripheral workers
 * are stopped before begin. Call close even after begin/poll failure and keep
 * the context alive until close succeeds. No kernel object is created here. */
txjob_status_t
txport_begin (void * p_context, const txjob_rule_t * p_rule, uint64_t now_ms);
txjob_status_t txport_poll (void *    p_context,
                            uint64_t  now_ms,
                            uint8_t * p_written,
                            uint8_t * p_data,
                            uint8_t * p_size);
int txport_close (void * p_context);
#endif

/* End of file. */

