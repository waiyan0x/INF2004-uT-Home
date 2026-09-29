/**
 * @file hcinst.h
 * @brief Bind a HC-SR04 instance to the generic component and worker contracts.
 */
#ifndef UTHOME_HCINST_H
#define UTHOME_HCINST_H

#include "hc_sr04.h"
#include "uthome/drvreg.h"
#include "uthome/msgbus.h"

/* Scalar settings copied by activate; port/context pointers stay in storage
 * owned by the application's binding adapter. source_index is the plan index.
 */
typedef struct
{
    pulseio_config_t  pulse;
    uint16_t          source_index;
    uint32_t          timeout_us;
    msgbus_queue_id_t h_destination;
} hcinst_config_t;

typedef struct
{
    pulseio_t *     p_port;
    hc_sr04_t       sensor;
    hcinst_config_t config;
    uint32_t        sequence;
    int             last_io_error;
    int             last_queue_error;
    bool            b_configured;
} hcinst_t;

/* Zero-initialize each distinct context, then set p_port before registry bind.
 * Binding itself performs no I/O. Configure/read are the component operations;
 * step and cleanup are supplied to activate_device_t.worker.
 */
const drvreg_descriptor_t * hcinst_descriptor (void);
int hcinst_step (void * p_context);
int hcinst_cleanup (void * p_context);

#endif

/* End of file. */

