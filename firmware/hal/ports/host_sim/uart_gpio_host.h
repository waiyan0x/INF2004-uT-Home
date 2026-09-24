#ifndef UG_HOST_H
#define UG_HOST_H
#include "uart_gpio_io.h"
#define UG_HOST_PINS 64
#define UG_HOST_BUFFER 512
typedef struct {
    uint64_t now;
    uint8_t rx[UG_HOST_BUFFER], tx[UG_HOST_BUFFER];
    size_t rx_len, rx_pos, tx_len, rx_capacity;
    bool uart_opened, overflow;
    bool configured[UG_HOST_PINS], level[UG_HOST_PINS];
    ug_direction direction[UG_HOST_PINS];
    ug_edge edges[UG_HOST_PINS];
    ug_edge_cb callbacks[UG_HOST_PINS];
    void *callback_args[UG_HOST_PINS];
    uint16_t duty[UG_HOST_PINS];
    uint32_t frequency[UG_HOST_PINS];
    uint32_t echo_delay_us, echo_high_us;
    ug_status capture_status;
} uart_gpio_host;
void uart_gpio_host_init(uart_gpio_host *, uart_gpio_io *);
ug_status uart_gpio_host_receive(uart_gpio_host *, const uint8_t *, size_t);
ug_status uart_gpio_host_input(uart_gpio_host *, unsigned, bool);
#endif
