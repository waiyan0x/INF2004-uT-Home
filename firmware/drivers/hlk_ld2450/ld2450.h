#ifndef UG_LD2450_H
#define UG_LD2450_H
#include "uart_gpio_io.h"
typedef struct {
    int16_t x_mm, y_mm, speed_cm_s;
    uint16_t resolution_mm;
    bool present;
} ug_target;
typedef struct { ug_target targets[3]; uint64_t timestamp_us; } ug_target_report;
typedef struct {
    uint8_t bytes[30];
    size_t used;
    uint32_t frames, framing_errors, discarded_bytes, timeouts, overflows;
} ug_ld2450;
void ug_ld2450_init(ug_ld2450 *driver);
/* Feed one byte; true means a complete report was written. */
bool ug_ld2450_feed(ug_ld2450 *, uint8_t, uint64_t, ug_target_report *);
/* One complete report per call; partial frames persist until timeout/error.
 * Output is untouched on failure. Byte-at-a-time reads avoid losing reports. */
ug_status ug_ld2450_read(ug_ld2450 *, const uart_gpio_io *, uint32_t timeout_us,
                         ug_target_report *);
#endif
