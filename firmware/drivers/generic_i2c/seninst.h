/**
 * @file seninst.h
 * @brief Bind a SEN0390 light sensor to the existing worker and queue.
 */
#ifndef UTHOME_SENINST_H
#define UTHOME_SENINST_H

#include "senlux.h"
#include "uthome/drvreg.h"
#include "uthome/msgbus.h"

typedef struct
{
    uint16_t          source_index;
    uint32_t          timeout_ms;
    msgbus_queue_id_t h_destination;
} seninst_config_t;

/* The binder owns the borrowed bus and enable handle until cleanup succeeds. */
typedef struct
{
    i2cbus_t *       p_bus;
    digiout_t *      p_enable;
    senlux_t         sensor;
    seninst_config_t config;
    uint32_t         sequence;
    int              last_io_error;
    int              last_queue_error;
    bool             b_configured;
} seninst_t;

const drvreg_descriptor_t * seninst_descriptor (void);
int seninst_step (void * p_context);
int seninst_cleanup (void * p_context);

#endif

/* End of file. */

