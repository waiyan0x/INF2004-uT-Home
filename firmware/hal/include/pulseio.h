/**
 * @file pulseio.h
 * @brief Own a trigger/input pair and measure one bounded high pulse.
 */
#ifndef UTHOME_PULSEIO_H
#define UTHOME_PULSEIO_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum
{
    PULSEIO_OK      = 0,
    PULSEIO_BUSY    = 1,
    PULSEIO_INVALID = -1,
    PULSEIO_STATE   = -2,
    PULSEIO_IO      = -3,
    PULSEIO_TIMEOUT = -4
} pulseio_result_t;

typedef struct
{
    uint8_t trigger_gpio;
    uint8_t input_gpio;
} pulseio_config_t;

typedef int pulseio_open_fn_t (void *                   p_context,
                               const pulseio_config_t * p_config);
typedef int pulseio_close_fn_t (void * p_context);
typedef int pulseio_measure_fn_t (void *     p_context,
                                  uint32_t   trigger_us,
                                  uint32_t   timeout_us,
                                  uint32_t * p_high_us);
typedef int pulseio_now_fn_t (void * p_context, uint64_t * p_now_ms);

typedef struct
{
    pulseio_open_fn_t *    p_open;
    pulseio_close_fn_t *   p_close;
    pulseio_measure_fn_t * p_measure;
    pulseio_now_fn_t *     p_now;
} pulseio_ops_t;

typedef struct
{
    const pulseio_ops_t * p_operations;
    void *                p_context;
    const void *          p_owner;
    bool                  b_ready;
} pulseio_t;

/* Borrowed, noncopyable storage. One lifecycle owner opens/closes with its user
 * stopped. Failed open/close retains ownership until checked cleanup succeeds.
 * Arm input before trigger; leave trigger low on every measurement exit.
 * The total capture budget includes trigger and both edges. A timeout can be
 * observed at the next scheduled wake; never accept data after the deadline.
 * Backends yield while waiting. No sample is usable on a nonzero result. */
int pulseio_open (pulseio_t *              p_port,
                  const pulseio_config_t * p_config,
                  const void *             p_owner);
int pulseio_close (pulseio_t * p_port, const void * p_owner);
int pulseio_measure (pulseio_t *  p_port,
                     const void * p_owner,
                     uint32_t     trigger_us,
                     uint32_t     timeout_us,
                     uint32_t *   p_high_us);
int pulseio_now (pulseio_t * p_port, const void * p_owner, uint64_t * p_now_ms);
#endif
/* End of file. */

