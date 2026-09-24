#include "uart_gpio_host.h"
#include <string.h>
static uint64_t now(void *c) { return ((uart_gpio_host *)c)->now; }
static ug_status uart_open(void *c, const ug_uart_config *cfg) {
    if (!cfg || !cfg->baud_rate || !cfg->rx_buffer_bytes ||
        cfg->rx_buffer_bytes > UG_HOST_BUFFER || cfg->rx_pin == cfg->tx_pin ||
        cfg->rx_pin >= UG_HOST_PINS || cfg->tx_pin >= UG_HOST_PINS) return UG_INVALID;
    uart_gpio_host *h = c;
    h->uart_opened = true;
    h->rx_capacity = cfg->rx_buffer_bytes;
    h->rx_len = h->rx_pos = h->tx_len = 0;
    h->overflow = false;
    return UG_OK;
}
static ug_status uart_read(void *c, uint8_t *dst, size_t cap, size_t *n, uint32_t budget) {
    uart_gpio_host *h = c;
    if (!n) return UG_INVALID;
    *n = 0;
    if (!dst || !cap || !h->uart_opened) return UG_INVALID;
    if (h->overflow) { h->overflow = false; return UG_OVERFLOW; }
    size_t available = h->rx_len - h->rx_pos;
    if (!available) { h->now += budget; return UG_TIMEOUT; }
    *n = available < cap ? available : cap;
    memcpy(dst, h->rx + h->rx_pos, *n);
    h->rx_pos += *n;
    return UG_OK;
}
static ug_status uart_write(void *c, const uint8_t *src, size_t len, size_t *n, uint32_t budget) {
    uart_gpio_host *h = c;
    (void)budget;
    if (!n) return UG_INVALID;
    *n = 0;
    if (!src || !h->uart_opened) return UG_INVALID;
    size_t room = UG_HOST_BUFFER - h->tx_len;
    *n = len < room ? len : room;
    memcpy(h->tx + h->tx_len, src, *n);
    h->tx_len += *n;
    return *n == len ? UG_OK : UG_OVERFLOW;
}
static ug_status config(void *c, unsigned p, ug_direction dir, bool initial) {
    uart_gpio_host *h = c;
    if (p >= UG_HOST_PINS || (dir != UG_INPUT && dir != UG_OUTPUT)) return UG_INVALID;
    h->configured[p] = true; h->direction[p] = dir; h->level[p] = initial;
    return UG_OK;
}
static ug_status read_pin(void *c, unsigned p, bool *v) {
    uart_gpio_host *h = c;
    if (p >= UG_HOST_PINS || !v || !h->configured[p]) return UG_INVALID;
    *v = h->level[p]; return UG_OK;
}
static ug_status write_pin(void *c, unsigned p, bool v) {
    uart_gpio_host *h = c;
    if (p >= UG_HOST_PINS || !h->configured[p] || h->direction[p] != UG_OUTPUT)
        return UG_INVALID;
    h->level[p] = v; return UG_OK;
}
static ug_status irq(void *c, unsigned p, ug_edge edge, ug_edge_cb cb, void *arg) {
    uart_gpio_host *h = c;
    if (p >= UG_HOST_PINS || !h->configured[p] || h->direction[p] != UG_INPUT ||
        (edge != UG_RISING && edge != UG_FALLING && edge != UG_BOTH)) return UG_INVALID;
    h->callbacks[p] = cb; h->callback_args[p] = arg; h->edges[p] = edge;
    return UG_OK;
}
static ug_status capture(void *c, unsigned trig, unsigned echo, uint32_t pulse,
                          uint32_t budget, uint32_t *high) {
    uart_gpio_host *h = c;
    if (!high || !pulse || trig == echo || trig >= UG_HOST_PINS || echo >= UG_HOST_PINS ||
        !h->configured[trig] || !h->configured[echo] ||
        h->direction[trig] != UG_OUTPUT || h->direction[echo] != UG_INPUT) return UG_INVALID;
    h->level[trig] = false;
    if (h->capture_status != UG_OK) {
        if (h->capture_status == UG_TIMEOUT) h->now += budget;
        return h->capture_status;
    }
    uint64_t total = (uint64_t)pulse + h->echo_delay_us + h->echo_high_us;
    if (!h->echo_high_us || total > budget) { h->now += budget; return UG_TIMEOUT; }
    h->now += total; *high = h->echo_high_us; return UG_OK;
}
static ug_status pwm(void *c, unsigned p, uint32_t hz, uint16_t duty) {
    uart_gpio_host *h = c;
    if (p >= UG_HOST_PINS || !hz || duty > 1000) return UG_INVALID;
    h->duty[p] = duty; h->frequency[p] = hz; return UG_OK;
}
void uart_gpio_host_init(uart_gpio_host *h, uart_gpio_io *io) {
    memset(h, 0, sizeof(*h));
    *io = (uart_gpio_io){h, now, uart_open, uart_read, uart_write, config,
                  read_pin, write_pin, irq, capture, pwm};
}
ug_status uart_gpio_host_receive(uart_gpio_host *h, const uint8_t *bytes, size_t n) {
    if (!h || !bytes || !h->uart_opened) return UG_INVALID;
    size_t remaining = h->rx_len - h->rx_pos;
    if (n > h->rx_capacity - remaining) {
        h->overflow = true; h->rx_len = h->rx_pos = 0; return UG_OVERFLOW;
    }
    memmove(h->rx, h->rx + h->rx_pos, remaining);
    memcpy(h->rx + remaining, bytes, n);
    h->rx_pos = 0; h->rx_len = remaining + n; return UG_OK;
}
ug_status uart_gpio_host_input(uart_gpio_host *h, unsigned p, bool level) {
    if (!h || p >= UG_HOST_PINS || !h->configured[p] || h->direction[p] != UG_INPUT)
        return UG_INVALID;
    bool changed = h->level[p] != level;
    h->level[p] = level;
    if (changed && h->callbacks[p] && (h->edges[p] == UG_BOTH ||
        h->edges[p] == (level ? UG_RISING : UG_FALLING)))
        h->callbacks[p](h->callback_args[p], level, h->now);
    return UG_OK;
}
