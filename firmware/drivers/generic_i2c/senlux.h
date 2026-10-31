/**
 * @file senlux.h
 * @brief Bounded SEN0390 enable and read state for a reviewed adapter.
 */
#ifndef UTHOME_SENLUX_H
#define UTHOME_SENLUX_H

#include "digiout.h"
#include "geni2c.h"

#define UTHOME_SENLUX_WAITING 1

/* Initialize to zero; the bus and enable handle must outlive this context.
 * A failed open can retain a claim and must be followed by checked close.
 * Calls for one context are serialized. WAITING publishes no light value.
 */
typedef struct
{
    geni2c_t    sensor;
    digiout_t * p_enable;
    uint64_t    ready_ms;
    bool        b_high;
    bool        b_started;
} senlux_t;

int senlux_open (senlux_t *  p_device,
                 i2cbus_t *  p_bus,
                 digiout_t * p_enable,
                 uint32_t    timeout_ms);
int senlux_read (senlux_t * p_device, uint32_t * p_millilux);
int senlux_close (senlux_t * p_device);

#endif

/* End of file. */

