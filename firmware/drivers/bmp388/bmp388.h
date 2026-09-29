/**
 * @file bmp388.h
 * @brief Instance-owned BMP388 wrapper around the pinned Bosch sensor API.
 */
#ifndef UTHOME_BMP388_H
#define UTHOME_BMP388_H

#include "i2cbus.h"
#include "bmp3.h"

typedef struct
{
    float pressure_pa;
    float temperature_c;
} bmp388_value_t;

/* Initialize storage to zero. Calls for one instance must be serialized.
 * The bus is borrowed until close succeeds, including failed partial setup.
 * Vendor types are confined to this driver; no SDK/kernel types are exposed.
 */
typedef struct
{
    i2cbus_t *           p_bus;
    struct bmp3_dev      vendor;
    struct bmp3_settings settings;
    uint64_t             deadline_ms;
    uint32_t             timeout_ms;
    int                  bus_error;
    uint8_t              address;
    bool                 b_claimed;
    bool                 b_touched;
    bool                 b_ready;
} bmp388_t;

int bmp388_open (bmp388_t * p_sensor,
                 i2cbus_t * p_bus,
                 uint8_t    address,
                 uint32_t   timeout_ms);
int bmp388_read (bmp388_t * p_sensor, bmp388_value_t * p_value);
int bmp388_close (bmp388_t * p_sensor);

#endif

/* End of file. */

