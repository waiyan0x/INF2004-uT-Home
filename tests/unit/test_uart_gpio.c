#include "uart_gpio_host.h"
#include "ld2450.h"
#include "hc_sr04.h"
#include "pwm_motor.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint8_t frame[30] = {
    0xaa,0xff,3,0, 0x0e,3,0xb1,0x86,0x10,0,0x40,1,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0x55,0xcc
};
static void edge(void *arg, bool level, uint64_t time) {
    (void)time; assert(level); (*(unsigned *)arg)++;
}
int main(void) {
    uart_gpio_host host; uart_gpio_io io; uart_gpio_host_init(&host, &io);
    ug_uart_config cfg = {1,16,17,256000,512};
    assert(io.uart_open(io.ctx, &cfg) == UG_OK);
    ug_ld2450 radar; ug_ld2450_init(&radar);
    ug_target_report report = {0};
    for (size_t split = 0; split < 30; ++split) {
        ug_ld2450_init(&radar);
        for (size_t i = 0; i < split; ++i)
            assert(!ug_ld2450_feed(&radar, frame[i], 0, &report));
        for (size_t i = split; i < 30; ++i)
            assert(ug_ld2450_feed(&radar, frame[i], 123, &report) == (i == 29));
        assert(report.targets[0].x_mm == -782);
        assert(report.targets[0].y_mm == 1713);
        assert(report.targets[0].speed_cm_s == -16);
        assert(report.targets[0].resolution_mm == 320);
        assert(report.targets[0].present && !report.targets[1].present);
        assert(report.timestamp_us == 123);
    }
    uint8_t bad[30]; memcpy(bad, frame, 30); bad[29] = 0;
    assert(uart_gpio_host_receive(&host, bad, 30) == UG_OK);
    assert(uart_gpio_host_receive(&host, frame, 30) == UG_OK);
    assert(uart_gpio_host_receive(&host, frame, 30) == UG_OK);
    assert(ug_ld2450_read(&radar, &io, 250000, &report) == UG_OK);
    assert(radar.framing_errors == 1);
    assert(ug_ld2450_read(&radar, &io, 250000, &report) == UG_OK);
    assert(uart_gpio_host_receive(&host, frame, 12) == UG_OK);
    ug_target_report previous = report;
    assert(ug_ld2450_read(&radar, &io, 250000, &report) == UG_TIMEOUT);
    assert(!radar.used && radar.timeouts == 1);
    assert(memcmp(&report, &previous, sizeof(report)) == 0);
    uint8_t large[513] = {0};
    assert(uart_gpio_host_receive(&host, large, sizeof(large)) == UG_OVERFLOW);
    assert(ug_ld2450_read(&radar, &io, 100, &report) == UG_OVERFLOW);
    assert(radar.overflows == 1);
    size_t sent;
    assert(io.uart_write(io.ctx, large, sizeof(large), &sent, 100) == UG_OVERFLOW);
    assert(sent == 512);
    assert(io.gpio_config(io.ctx, 2, UG_INPUT, false) == UG_OK);
    unsigned count = 0;
    assert(io.gpio_irq(io.ctx, 2, UG_RISING, edge, &count) == UG_OK);
    assert(uart_gpio_host_input(&host, 2, true) == UG_OK);
    assert(uart_gpio_host_input(&host, 2, false) == UG_OK);
    assert(count == 1);
    assert(io.gpio_irq(io.ctx, 2, UG_RISING, NULL, NULL) == UG_OK);
    assert(uart_gpio_host_input(&host, 2, true) == UG_OK && count == 1);
    assert(io.gpio_write(io.ctx, 2, true) == UG_INVALID);
    ug_hc_sr04 sonar; ug_distance distance = {0};
    assert(ug_hc_sr04_init(&sonar, &io, 4, 4, 30000) == UG_INVALID);
    assert(ug_hc_sr04_init(&sonar, &io, 4, 5, 30000) == UG_OK);
    host.echo_high_us = 5800; host.echo_delay_us = 100;
    assert(ug_hc_sr04_measure(&sonar, &io, &distance) == UG_OK);
    assert(distance.distance_mm == 1000 && !host.level[4]);
    assert(ug_hc_sr04_measure(&sonar, &io, &distance) == UG_BUSY);
    host.now += 61000; host.echo_high_us = 30000;
    uint64_t before = host.now;
    assert(ug_hc_sr04_measure(&sonar, &io, &distance) == UG_TIMEOUT);
    assert(host.now - before == 30000 && distance.distance_mm == 1000);
    host.now += 61000; host.echo_high_us = 1;
    assert(ug_hc_sr04_measure(&sonar, &io, &distance) == UG_IO_ERROR);
    ug_motor motor;
    assert(ug_motor_init(&motor, &io, 6, 20000) == UG_OK && host.duty[6] == 0);
    assert(ug_motor_set(&motor, &io, 1001) == UG_INVALID);
    /* Example runtime decision: valid near sample -> half speed; fault -> stop. */
    assert(ug_motor_set(&motor, &io, distance.distance_mm < 1500 ? 500 : 0) == UG_OK);
    assert(host.duty[6] == 500);
    assert(ug_motor_stop(&motor, &io) == UG_OK && host.duty[6] == 0);
    puts("UART/GPIO: all host assertions passed (UART, radar, GPIO, sonar, PWM).");
    return 0;
}
