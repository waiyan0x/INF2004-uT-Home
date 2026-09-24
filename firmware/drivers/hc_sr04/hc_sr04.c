#include "hc_sr04.h"
ug_status ug_hc_sr04_init(ug_hc_sr04 *d, const uart_gpio_io *io,
                          unsigned trigger, unsigned echo, uint32_t timeout) {
    if (!d || !io || !io->gpio_config || !io->pulse_capture || !io->now_us ||
        trigger == echo || timeout < 100 || timeout > 60000) return UG_INVALID;
    ug_status s = io->gpio_config(io->ctx, trigger, UG_OUTPUT, false);
    if (s != UG_OK) return s;
    s = io->gpio_config(io->ctx, echo, UG_INPUT, false);
    if (s != UG_OK) return s;
    *d = (ug_hc_sr04){trigger, echo, timeout, 0, false};
    return UG_OK;
}
ug_status ug_hc_sr04_measure(ug_hc_sr04 *d, const uart_gpio_io *io, ug_distance *out) {
    if (!d || !io || !io->now_us || !io->pulse_capture || !out || !d->timeout_us)
        return UG_INVALID;
    uint64_t now = io->now_us(io->ctx);
    if (d->started && now - d->last_start_us < 61000) return UG_BUSY;
    d->last_start_us = now;
    d->started = true;
    uint32_t high = 0;
    ug_status s = io->pulse_capture(io->ctx, d->trigger_pin, d->echo_pin,
                                   10, d->timeout_us, &high);
    if (s != UG_OK) return s;
    if (!high || high > d->timeout_us) return UG_IO_ERROR;
    uint32_t mm = (high * 10u + 29u) / 58u;
    if (mm < 20 || mm > 4000) return UG_IO_ERROR;
    *out = (ug_distance){mm, high, io->now_us(io->ctx)};
    return UG_OK;
}
