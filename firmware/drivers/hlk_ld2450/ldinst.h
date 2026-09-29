/**
 * @file ldinst.h
 * @brief Bind a radar instance to the existing registry and periodic worker.
 */
#ifndef UTHOME_LDINST_H
#define UTHOME_LDINST_H
#include "ld2450.h"
#include "uthome/drvreg.h"
#include "uthome/msgbus.h"

typedef struct
{
    uartio_config_t   uart;
    uint32_t          timeout_ms;
    uint16_t          source_index;
    msgbus_queue_id_t h_destination;
} ldinst_config_t;

typedef struct
{
    uartio_t *      p_port;
    ld2450_t        sensor;
    ldinst_config_t config;
    uint32_t        sequence;
    int             last_io_error;
    int             last_queue_error;
    bool            b_configured;
} ldinst_t;

/* Zero each context and set p_port before binding. Schedule step at 10 ms using
 * the existing periodic worker. No device-specific branch belongs in runtime.
 * Read returns AGAIN without a sample; step converts it to a successful idle
 * iteration. Driver faults publish zeroed error samples; state/timebase
 * failures stop the worker. No sensor configuration commands are transmitted.
 */
const drvreg_descriptor_t * ldinst_descriptor (void);
int ldinst_step (void * p_context);
int ldinst_cleanup (void * p_context);
#endif
/* End of file. */

