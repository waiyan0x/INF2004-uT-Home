/**
 * @file ugtest_main.c
 * @brief Preserve UART/GPIO/PWM scenarios and use maintained sensor APIs.
 */
#ifdef NDEBUG
#error "Tests require assertions"
#endif
#include "ugtest_main.h"
#include "ughost.h"
#include "pwm_motor.h"
#include "uartfake.h"
#include "ld2450.h"
#include "gpiofake.h"
#include "hc_sr04.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint8_t g_frame[30] = {
    0xAAU, 0xFFU, 3U, 0U, 0x0EU, 3U, 0xB1U, 0x86U, 0x10U, 0U,
    0x40U, 1U,    0U, 0U, 0U,    0U, 0U,    0U,    0U,    0U,
    0U,    0U,    0U, 0U, 0U,    0U, 0U,    0U,    0x55U, 0xCCU};
static void edge (void * p_arg, bool b_level, uint64_t time);
static void verify_radar (void);
static void verify_sonar (void);

/** @brief Increment exactly once per selected rising edge. */
static void edge (void * p_arg, bool b_level, uint64_t time)
{
    (void)time;
    assert(b_level);
    unsigned * p_count = p_arg;
    ++(*p_count);
}

/** @brief Exercise the original standalone GPIO and motor behavior. */
int main (void)
{
    ughost_t host;
    ugio_t   port;
    ughost_init(&host, &port);
    const ugio_uart_config_t config = {1U, 16U, 17U, 256000U, 512U};
    assert(UG_OK == port.p_uart_open(port.p_context, &config));
    uint8_t large[513] = {0};
    size_t  count      = 0U;
    assert(UG_OVERFLOW == ughost_receive(&host, large, sizeof(large)));
    assert(UG_OVERFLOW ==
           port.p_uart_read(port.p_context, large, 1U, &count, 100U));
    assert(
        UG_OVERFLOW ==
        port.p_uart_write(port.p_context, large, sizeof(large), &count, 100U));
    assert(512U == count);
    assert(UG_OK == ughost_receive(&host, g_frame, sizeof(g_frame)));
    assert(UG_OK == port.p_uart_read(
                        port.p_context, large, sizeof(large), &count, 100U));
    assert(sizeof(g_frame) == count);
    assert(0 == memcmp(large, g_frame, count));
    assert(UG_TIMEOUT ==
           port.p_uart_read(port.p_context, large, 1U, &count, 100U));
    assert(UG_OK == port.p_gpio_config(port.p_context, 2U, UG_INPUT, false));
    unsigned edges = 0U;
    assert(UG_OK ==
           port.p_gpio_irq(port.p_context, 2U, UG_RISING, edge, &edges));
    assert(UG_OK == ughost_input(&host, 2U, true));
    assert(UG_OK == ughost_input(&host, 2U, false));
    assert(1U == edges);
    assert(UG_OK == port.p_gpio_irq(port.p_context, 2U, UG_RISING, NULL, NULL));
    assert(UG_OK == ughost_input(&host, 2U, true));
    assert(1U == edges);
    assert(UG_INVALID == port.p_gpio_write(port.p_context, 2U, true));
    assert(UG_OK == port.p_gpio_config(port.p_context, 4U, UG_OUTPUT, false));
    assert(UG_OK == port.p_gpio_config(port.p_context, 5U, UG_INPUT, false));
    host.echo_high_us  = 5800U;
    host.echo_delay_us = 100U;
    uint32_t high      = 0U;
    assert(UG_INVALID ==
           port.p_pulse_capture(port.p_context, 4U, 4U, 10U, 30000U, &high));
    assert(UG_OK ==
           port.p_pulse_capture(port.p_context, 4U, 5U, 10U, 30000U, &high));
    assert(5800U == high);
    assert(!host.b_level[4]);
    host.echo_high_us     = 30000U;
    const uint64_t before = host.now;
    assert(UG_TIMEOUT ==
           port.p_pulse_capture(port.p_context, 4U, 5U, 10U, 30000U, &high));
    assert(30000U == (host.now - before));
    pwm_motor_t motor;
    assert(UG_OK == pwm_motor_init(&motor, &port, 6U, 20000U));
    assert(0U == host.duty[6]);
    assert(UG_INVALID == pwm_motor_set(&motor, &port, 1001U));
    assert(UG_OK == pwm_motor_set(&motor, &port, 500U));
    assert(500U == host.duty[6]);
    assert(UG_OK == pwm_motor_stop(&motor, &port));
    assert(0U == host.duty[6]);
    verify_radar();
    verify_sonar();
    puts("UART/GPIO, current radar/sonar and PWM regressions passed.");
    return 0;
}

/** @brief Keep fragmentation, decoding, timeout and overflow coverage. */
static void verify_radar (void)
{
    sample_target_report_t report = {0};
    for (size_t split = 0U; split < sizeof(g_frame); ++split)
    {
        ldparse_t parser = {0};
        for (size_t index = 0U; index < split; ++index)
        {
            assert(!ldparse_feed(&parser, g_frame[index], 0U, &report));
        }
        for (size_t index = split; index < sizeof(g_frame); ++index)
        {
            assert((29U == index) ==
                   ldparse_feed(&parser, g_frame[index], 123U, &report));
        }
        assert(-782 == report.targets[0].x_mm);
        assert(1713 == report.targets[0].y_mm);
        assert(-16 == report.targets[0].speed_cm_s);
        assert(320U == report.targets[0].resolution_mm);
        assert(report.targets[0].b_present);
        assert(!report.targets[1].b_present);
        assert(123U == report.header.timestamp_ms);
    }
    uartfake_t fake;
    uartfake_init(&fake);
    ld2450_t              radar  = {0};
    const uartio_config_t config = {.baud            = 256000U,
                                    .rx_buffer_bytes = 512U,
                                    .port            = 1U,
                                    .tx_gpio         = 17U,
                                    .rx_gpio         = 16U,
                                    .data_bits       = 8U,
                                    .stop_bits       = 1U};
    assert(0 == ld2450_open(&radar, &fake.port, &config, 250U));
    uint8_t bad[30];
    memcpy(bad, g_frame, sizeof(bad));
    bad[29] = 0U;
    assert(0 == uartfake_feed(&fake, bad, sizeof(bad)));
    assert(0 == uartfake_feed(&fake, g_frame, sizeof(g_frame)));
    assert(0 == ld2450_poll(&radar, &report));
    assert(1U == radar.parser.framing_errors);
    assert(0 == uartfake_feed(&fake, g_frame, 12U));
    assert(UARTIO_AGAIN == ld2450_poll(&radar, &report));
    fake.now_ms = 251U;
    assert(UARTIO_TIMEOUT == ld2450_poll(&radar, &report));
    assert(1U == radar.timeouts);
    assert(UARTIO_AGAIN == ld2450_poll(&radar, &report));
    uint8_t large[513] = {0};
    assert(UARTIO_OVERFLOW == uartfake_feed(&fake, large, sizeof(large)));
    assert(UARTIO_OVERFLOW == ld2450_poll(&radar, &report));
    assert(1U == radar.overflows);
    assert(0 == ld2450_close(&radar));
}

/** @brief Use the current sonar contract: millisecond spacing and zero on
 * error. */
static void verify_sonar (void)
{
    pinpool_t  pins = {.allowed = 0x30U};
    gpiofake_t fake;
    gpiofake_init(&fake, &pins);
    hc_sr04_t        sonar  = {0};
    hc_sr04_value_t  value  = {0};
    pulseio_config_t config = {.trigger_gpio = 4U, .input_gpio = 4U};
    assert(PULSEIO_INVALID ==
           hc_sr04_open(&sonar, &fake.pulse, &config, 30000U));
    assert(0 == hc_sr04_close(&sonar));
    config.input_gpio = 5U;
    assert(0 == hc_sr04_open(&sonar, &fake.pulse, &config, 30000U));
    fake.high_us = 5800U;
    assert(0 == hc_sr04_read(&sonar, &value));
    assert(1000U == value.distance_mm);
    assert(PULSEIO_BUSY == hc_sr04_read(&sonar, &value));
    fake.now_ms += 62U;
    fake.measure_error = PULSEIO_TIMEOUT;
    assert(PULSEIO_TIMEOUT == hc_sr04_read(&sonar, &value));
    assert(0U == value.distance_mm);
    fake.now_ms += 62U;
    fake.measure_error = 0;
    fake.high_us       = 1U;
    assert(PULSEIO_INVALID == hc_sr04_read(&sonar, &value));
    assert(0 == hc_sr04_close(&sonar));
}

/* End of file. */

