/**
 * @file i2cbus.h
 * @brief Bounded synchronous I2C transfers with one absolute deadline.
 */
#ifndef UTHOME_I2CBUS_H
#define UTHOME_I2CBUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UTHOME_I2C_TX_MAX       30U
#define UTHOME_I2C_RX_MAX       32U
#define UTHOME_I2C_PAUSE_MAX_MS 10U

typedef enum
{
    I2CBUS_OK          = 0,
    I2CBUS_INVALID     = -1,
    I2CBUS_TIMEOUT     = -2,
    I2CBUS_NACK        = -3,
    I2CBUS_IO          = -4,
    I2CBUS_STATE       = -5,
    I2CBUS_UNSUPPORTED = -6
} i2cbus_status_t;

typedef struct
{
    uint8_t  port;
    uint8_t  sda_gpio;
    uint8_t  scl_gpio;
    uint32_t clock_hz;
} i2cbus_config_t;

typedef struct
{
    uint8_t         address;
    const uint8_t * p_write;
    size_t          write_size;
    uint8_t *       p_read;
    size_t          read_size;
    uint64_t        deadline_ms;
    uint32_t        read_pause_ms;
} i2cbus_request_t;

typedef int i2cbus_transfer_fn_t (void *                   p_context,
                                  const i2cbus_request_t * p_request);
typedef int i2cbus_now_fn_t (void * p_context, uint64_t * p_now);
typedef int i2cbus_delay_fn_t (void *   p_context,
                               uint32_t duration_us,
                               uint64_t deadline_ms);

typedef struct
{
    i2cbus_transfer_fn_t * p_transfer;
    i2cbus_now_fn_t *      p_now;
    i2cbus_delay_fn_t *    p_delay;
} i2cbus_ops_t;

/* The backend owns this bus and the controller until close succeeds.
 * All device contexts borrow it. Claims are changed only by the serialized
 * lifecycle owner with affected workers stopped. Transfers serialize on the
 * backend's bus mutex; callers must never share a receive buffer concurrently.
 */
typedef struct
{
    const i2cbus_ops_t * p_operations;
    void *               p_context;
    uint8_t              claims[16];
} i2cbus_t;

/* Empty write or read selects a one-direction transfer. Both nonempty means
 * repeated START with no intervening STOP. No address-only scanning operation.
 * RX contents are usable only on success. Deadline includes lock contention;
 * read_pause_ms requests a bounded pause after the read address, without STOP
 * or releasing bus ownership. A backend that cannot do this must return
 * I2CBUS_UNSUPPORTED. Deadline includes the pause; no callback may restart it.
 */
int i2cbus_transfer (i2cbus_t * p_bus, const i2cbus_request_t * p_request);
int i2cbus_now (i2cbus_t * p_bus, uint64_t * p_now);
int i2cbus_deadline (i2cbus_t * p_bus,
                     uint32_t   timeout_ms,
                     uint64_t * p_deadline);
int i2cbus_delay (i2cbus_t * p_bus, uint32_t duration_us, uint64_t deadline_ms);
int i2cbus_claim (i2cbus_t * p_bus, uint8_t address);
int i2cbus_release (i2cbus_t * p_bus, uint8_t address);
bool i2cbus_has_claims (const i2cbus_t * p_bus);

#endif

/* End of file. */

