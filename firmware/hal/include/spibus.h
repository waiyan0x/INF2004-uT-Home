/**
 * @file spibus.h
 * @brief Bound SPI transfers and retain distinct devices on a shared bus.
 */
#ifndef UTHOME_SPIBUS_H
#define UTHOME_SPIBUS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UTHOME_SPI_DEVICES 4U
#define UTHOME_SPI_BYTES   1024U
#define UTHOME_SPI_NO_PIN  UINT8_MAX

typedef enum
{
    SPIBUS_OK          = 0,
    SPIBUS_INVALID     = -1,
    SPIBUS_TIMEOUT     = -2,
    SPIBUS_IO          = -3,
    SPIBUS_STATE       = -4,
    SPIBUS_UNSUPPORTED = -5
} spibus_result_t;

typedef struct
{
    uint8_t  cs_gpio;
    uint8_t  mode;
    uint32_t clock_hz;
} spibus_config_t;

typedef struct
{
    const uint8_t * p_write;
    uint8_t *       p_read;
    size_t          size;
    uint64_t        deadline_ms;
} spibus_request_t;

typedef int spibus_attach_t (void *                  p_context,
                             const spibus_config_t * p_config,
                             bool                    b_attach);
typedef int spibus_transfer_t (void *                   p_context,
                               const spibus_config_t *  p_config,
                               const spibus_request_t * p_request);
typedef int spibus_now_t (void * p_context, uint64_t * p_now);
typedef int
spibus_delay_t (void * p_context, uint32_t duration_ms, uint64_t deadline_ms);

typedef struct
{
    spibus_attach_t *   p_attach;
    spibus_transfer_t * p_transfer;
    spibus_now_t *      p_now;
    spibus_delay_t *    p_delay;
} spibus_ops_t;

typedef struct
{
    const spibus_ops_t * p_operations;
    void *               p_context;
    const void *         p_devices[UTHOME_SPI_DEVICES];
    uint8_t              chip_selects[UTHOME_SPI_DEVICES];
} spibus_t;

typedef struct
{
    spibus_t *      p_bus;
    spibus_config_t config;
    const void *    p_owner;
} spibus_device_t;

/* Zero caller-owned devices. One lifecycle owner attaches/claims/detaches only
 * after affected users stop. Bus, callbacks, device and owner remain alive.
 * Transfers are MSB-first, active-low CS. One backend lock/CS window spans all
 * bytes, including internal FIFO chunks. RX failure contents are unusable.
 * Backend must include contention in the absolute deadline and deassert CS on
 * every exit. NULL TX clocks zeros; NULL RX discards input. */
int spibus_attach (spibus_t *              p_bus,
                   spibus_device_t *       p_device,
                   const spibus_config_t * p_config);
int spibus_detach (spibus_device_t * p_device);
int spibus_claim (spibus_device_t * p_device, const void * p_owner);
int spibus_release (spibus_device_t * p_device, const void * p_owner);
int spibus_transfer (spibus_device_t *        p_device,
                     const spibus_request_t * p_request);
int spibus_now (spibus_t * p_bus, uint64_t * p_now);
int spibus_deadline (spibus_t * p_bus,
                     uint32_t   timeout_ms,
                     uint64_t * p_deadline);
int spibus_delay (spibus_t * p_bus, uint32_t duration_ms, uint64_t deadline_ms);
bool spibus_has_devices (const spibus_t * p_bus);
#endif
/* End of file. */

