#ifndef UG_HC_SR04_H
#define UG_HC_SR04_H
#include "uart_gpio_io.h"
typedef struct {
    unsigned trigger_pin, echo_pin;
    uint32_t timeout_us;
    uint64_t last_start_us;
    bool started;
} ug_hc_sr04;
typedef struct { uint32_t distance_mm, echo_us; uint64_t timestamp_us; } ug_distance;
ug_status ug_hc_sr04_init(ug_hc_sr04 *, const uart_gpio_io *, unsigned, unsigned, uint32_t);
ug_status ug_hc_sr04_measure(ug_hc_sr04 *, const uart_gpio_io *, ug_distance *);
#endif
