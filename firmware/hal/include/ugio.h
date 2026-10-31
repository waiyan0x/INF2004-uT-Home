#ifndef UG_IO_H
#define UG_IO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Provisional UART/GPIO contract; no kernel/SDK dependencies or allocation.
 * One task owns each device. Ports serialize shared resources.
 * timeout_us is a TOTAL budget, never an infinite-wait sentinel.
 * UART transfers can be partial: always inspect transferred, even on error.
 * Zero UART timeout means poll. All other operations are bounded/nonblocking.
 */
typedef enum
{
    UG_OK,
    UG_INVALID,
    UG_TIMEOUT,
    UG_OVERFLOW,
    UG_IO_ERROR,
    UG_UNSUPPORTED,
    UG_BUSY
} ugio_status_t;

typedef enum
{
    UG_INPUT,
    UG_OUTPUT
} ugio_direction_t;

typedef enum
{
    UG_RISING,
    UG_FALLING,
    UG_BOTH
} ugio_edge_t;

typedef void ugio_edge_fn_t (void * p_arg, bool b_level, uint64_t timestamp_us);

typedef struct
{
    unsigned port;
    unsigned rx_pin;
    unsigned tx_pin;
    uint32_t baud_rate;
    size_t   rx_buffer_bytes;
} ugio_uart_config_t;

typedef uint64_t ugio_now_fn_t (void * p_context);
typedef ugio_status_t ugio_open_fn_t (void *                     p_context,
                                      const ugio_uart_config_t * p_config);
typedef ugio_status_t ugio_read_fn_t (void *    p_context,
                                      uint8_t * p_data,
                                      size_t    capacity,
                                      size_t *  p_count,
                                      uint32_t  budget);
typedef ugio_status_t ugio_write_fn_t (void *          p_context,
                                       const uint8_t * p_data,
                                       size_t          size,
                                       size_t *        p_count,
                                       uint32_t        budget);
typedef ugio_status_t ugio_config_fn_t (void *           p_context,
                                        unsigned         pin,
                                        ugio_direction_t direction,
                                        bool             b_initial);
typedef ugio_status_t
ugio_input_fn_t (void * p_context, unsigned pin, bool * pb_value);
typedef ugio_status_t
ugio_output_fn_t (void * p_context, unsigned pin, bool b_value);
typedef ugio_status_t ugio_irq_fn_t (void *           p_context,
                                     unsigned         pin,
                                     ugio_edge_t      edge,
                                     ugio_edge_fn_t * p_callback,
                                     void *           p_arg);
typedef ugio_status_t ugio_pulse_fn_t (void *     p_context,
                                       unsigned   trigger,
                                       unsigned   echo,
                                       uint32_t   trigger_us,
                                       uint32_t   timeout_us,
                                       uint32_t * p_high_us);
typedef ugio_status_t ugio_pwm_fn_t (void *   p_context,
                                     unsigned pin,
                                     uint32_t frequency_hz,
                                     uint16_t permille);

typedef struct
{
    void *             p_context;
    ugio_now_fn_t *    p_now_us;
    ugio_open_fn_t *   p_uart_open;
    ugio_read_fn_t *   p_uart_read;
    ugio_write_fn_t *  p_uart_write;
    ugio_config_fn_t * p_gpio_config;
    ugio_input_fn_t *  p_gpio_read;
    ugio_output_fn_t * p_gpio_write;
    /* ISR callback: no blocking/logging/allocation. NULL detaches. */
    ugio_irq_fn_t * p_gpio_irq;
    /* Arm echo before trigger; restore low on every exit. */
    ugio_pulse_fn_t * p_pulse_capture;
    /* Active-high permille [0,1000]; zero disables. */
    ugio_pwm_fn_t * p_pwm_set;
} ugio_t;
#endif
/* End of file. */

