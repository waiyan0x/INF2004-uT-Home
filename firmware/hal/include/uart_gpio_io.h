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
typedef enum { UG_OK, UG_INVALID, UG_TIMEOUT, UG_OVERFLOW,
               UG_IO_ERROR, UG_UNSUPPORTED, UG_BUSY } ug_status;
typedef enum { UG_INPUT, UG_OUTPUT } ug_direction;
typedef enum { UG_RISING, UG_FALLING, UG_BOTH } ug_edge;
typedef void (*ug_edge_cb)(void *arg, bool level, uint64_t timestamp_us);
typedef struct {
    unsigned port, rx_pin, tx_pin;
    uint32_t baud_rate;
    size_t rx_buffer_bytes;
    /* Starter supports 8 data bits, no parity, 1 stop bit only. */
} ug_uart_config;
typedef struct {
    void *ctx;
    uint64_t (*now_us)(void *);
    ug_status (*uart_open)(void *, const ug_uart_config *);
    ug_status (*uart_read)(void *, uint8_t *, size_t, size_t *, uint32_t);
    ug_status (*uart_write)(void *, const uint8_t *, size_t, size_t *, uint32_t);
    ug_status (*gpio_config)(void *, unsigned, ug_direction, bool initial_level);
    ug_status (*gpio_read)(void *, unsigned, bool *);
    ug_status (*gpio_write)(void *, unsigned, bool);
    /* Callback may run in ISR context: no blocking, logging or allocation.
     * NULL callback detaches. Port must quiesce callbacks before teardown. */
    ug_status (*gpio_irq)(void *, unsigned, ug_edge, ug_edge_cb, void *);
    /* Atomically arm echo capture BEFORE emitting trigger; restore trigger low
     * on every exit. Budget covers trigger + both echo edges. */
    ug_status (*pulse_capture)(void *, unsigned trigger, unsigned echo,
                               uint32_t trigger_us, uint32_t timeout_us,
                               uint32_t *high_us);
    /* Set/update active-high duty in permille [0,1000]; zero disables output. */
    ug_status (*pwm_set)(void *, unsigned, uint32_t hz, uint16_t permille);
} uart_gpio_io;
#endif
