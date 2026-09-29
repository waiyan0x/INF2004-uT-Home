/**
 * @file pwmio.h
 * @brief Own a PWM output with explicit zero-duty setup and cleanup.
 */
#ifndef UTHOME_PWMIO_H
#define UTHOME_PWMIO_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum
{
    PWMIO_OK      = 0,
    PWMIO_BUSY    = 1,
    PWMIO_INVALID = -1,
    PWMIO_STATE   = -2,
    PWMIO_IO      = -3
} pwmio_result_t;

typedef struct
{
    uint32_t frequency_hz;
    uint8_t  gpio;
} pwmio_config_t;

typedef int pwmio_open_fn_t (void * p_context, const pwmio_config_t * p_config);
typedef int pwmio_close_fn_t (void * p_context);
typedef int pwmio_set_fn_t (void * p_context, uint16_t duty_permille);

typedef struct
{
    pwmio_open_fn_t *  p_open;
    pwmio_close_fn_t * p_close;
    pwmio_set_fn_t *   p_set;
} pwmio_ops_t;

typedef struct
{
    const pwmio_ops_t * p_operations;
    void *              p_context;
    const void *        p_owner;
    uint16_t            duty_permille;
    bool                b_ready;
} pwmio_t;

/* Borrowed, noncopyable storage. Backends open at zero duty, set bounded duty
 * in [0,1000], and drive low before releasing hardware. A failed call keeps
 * ownership; retry cleanup with users stopped. duty_permille records only the
 * last successful request and is not physical actuator feedback. */
int pwmio_open (pwmio_t *              p_port,
                const pwmio_config_t * p_config,
                const void *           p_owner);
int pwmio_set (pwmio_t * p_port, const void * p_owner, uint16_t duty_permille);
int pwmio_close (pwmio_t * p_port, const void * p_owner);
#endif
/* End of file. */

