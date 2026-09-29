/**
 * @file bmpinst.h
 * @brief Bind a BMP388 instance to the generic component and worker contracts.
 */
#ifndef UTHOME_BMPINST_H
#define UTHOME_BMPINST_H

#include "bmp388.h"
#include "uthome/drvreg.h"
#include "uthome/msgbus.h"

/* Scalar settings copied by activate; bus/context pointers stay in storage
 * owned by the application's binding adapter. source_index is the plan index.
 */
typedef struct
{
    uint8_t           address;
    uint16_t          source_index;
    uint32_t          timeout_ms;
    msgbus_queue_id_t h_destination;
} bmpinst_config_t;

typedef struct
{
    i2cbus_t *       p_bus;
    bmp388_t         sensor;
    bmpinst_config_t config;
    uint32_t         sequence;
    int              last_io_error;
    int              last_queue_error;
    bool             b_configured;
} bmpinst_t;

/* Zero-initialize each distinct context, then set p_bus before registry bind.
 * Binding itself performs no I/O. Configure/read are the component operations;
 * step and cleanup are supplied to activate_device_t.worker.
 */
const drvreg_descriptor_t * bmpinst_descriptor (void);
int bmpinst_step (void * p_context);
int bmpinst_cleanup (void * p_context);

#endif

/* End of file. */

