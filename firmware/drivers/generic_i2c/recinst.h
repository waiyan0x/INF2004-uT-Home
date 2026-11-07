/**
 * @file recinst.h
 * @brief Execute admitted I2C register recipes in an existing fixed worker.
 */
#ifndef UTHOME_RECINST_H
#define UTHOME_RECINST_H

#include "geni2c.h"
#include "uthome/drvreg.h"
#include "uthome/msgbus.h"
#include "uthome/sample.h"

typedef struct
{
    geni2c_recipe_t   recipe;
    uint8_t           select_offset;
    uint8_t           select_length;
    uint16_t          source_index;
    uint32_t          timeout_ms;
    msgbus_queue_id_t h_destination;
} recinst_config_t;

/* The serialized lifecycle owner supplies an independently admitted recipe.
 * The worker borrows the bus until stopped and successfully cleaned up.
 * Zero-initialize this context before its first binding; no heap is used.
 */
typedef struct
{
    i2cbus_t *       p_bus;
    geni2c_t         sensor;
    recinst_config_t config;
    uint32_t         sequence;
    int              last_io_error;
    int              last_queue_error;
    bool             b_configured;
} recinst_t;

const drvreg_descriptor_t * recinst_descriptor (void);
int recinst_step (void * p_context);
int recinst_cleanup (void * p_context);

#endif

/* End of file. */

