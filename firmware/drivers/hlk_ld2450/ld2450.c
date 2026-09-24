#include "ld2450.h"
#include <string.h>
static uint16_t le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
/* LD2450 uses bit 15 = positive, NOT two's-complement. */
static int16_t signed_value(const uint8_t *p) {
    uint16_t v = le16(p);
    return (int16_t)((v & 0x8000u) ? (int)(v & 0x7fffu) : -(int)v);
}
void ug_ld2450_init(ug_ld2450 *d) { if (d) memset(d, 0, sizeof(*d)); }
bool ug_ld2450_feed(ug_ld2450 *d, uint8_t b, uint64_t now,
                     ug_target_report *out) {
    static const uint8_t header[4] = {0xaa, 0xff, 0x03, 0x00};
    if (!d || !out) return false;
    d->bytes[d->used++] = b;
    while (d->used && memcmp(d->bytes, header, d->used < 4 ? d->used : 4)) {
        memmove(d->bytes, d->bytes + 1, --d->used);
        d->discarded_bytes++;
    }
    if (d->used != 30) return false;
    if (d->bytes[28] != 0x55 || d->bytes[29] != 0xcc) {
        d->framing_errors++;
        memmove(d->bytes, d->bytes + 1, --d->used);
        d->discarded_bytes++;
        return false;
    }
    ug_target_report report = {0};
    report.timestamp_us = now;
    for (size_t i = 0; i < 3; ++i) {
        const uint8_t *p = d->bytes + 4 + i * 8;
        ug_target *t = &report.targets[i];
        t->x_mm = signed_value(p);
        t->y_mm = signed_value(p + 2);
        t->speed_cm_s = signed_value(p + 4);
        t->resolution_mm = le16(p + 6);
        /* Empty target slots contain eight zero bytes. */
        for (size_t j = 0; j < 8; ++j) t->present |= p[j] != 0;
    }
    *out = report;
    d->used = 0;
    d->frames++;
    return true;
}
ug_status ug_ld2450_read(ug_ld2450 *d, const uart_gpio_io *io,
                         uint32_t timeout, ug_target_report *out) {
    if (!d || !io || !io->now_us || !io->uart_read || !out || !timeout)
        return UG_INVALID;
    uint64_t start = io->now_us(io->ctx);
    /* Also bound work if a broken port supplies endless noise without time. */
    for (size_t work = 0; work < 4096; ++work) {
        uint64_t elapsed = io->now_us(io->ctx) - start;
        if (elapsed >= timeout) break;
        uint8_t b;
        size_t n = 0;
        ug_status s = io->uart_read(io->ctx, &b, 1, &n,
                                   timeout - (uint32_t)elapsed);
        if (s != UG_OK) {
            d->used = 0;
            if (s == UG_TIMEOUT) d->timeouts++;
            if (s == UG_OVERFLOW) d->overflows++;
            return s;
        }
        if (n != 1) { d->used = 0; return UG_IO_ERROR; }
        if (ug_ld2450_feed(d, b, io->now_us(io->ctx), out)) return UG_OK;
    }
    d->used = 0;
    d->timeouts++;
    return UG_TIMEOUT;
}
