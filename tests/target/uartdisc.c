/** @file uartdisc.c
 * @brief Inject reports, noise, clock faults and retained UART cleanup.
 */
#ifdef NDEBUG
#error "Discovery tests require assertions."
#endif
#include "uartdisc.h"
#include "uartfake.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uartfake_t            g_fake       = {0};
static uartio_ops_t          g_operations = {0};
static unsigned              g_mode       = 0U;
static unsigned              g_writes     = 0U;
static unsigned              g_waits      = 0U;
static const uartio_config_t g_config     = {.baud            = 256000U,
                                             .rx_buffer_bytes = 512U,
                                             .port            = 1U,
                                             .tx_gpio         = 21U,
                                             .rx_gpio         = 20U,
                                             .data_bits       = 8U,
                                             .stop_bits       = 1U};
static void reset (unsigned mode);
static int now_ms (uint64_t * p_now);
static int forbidden_write (void *          p_context,
                            const uint8_t * p_data,
                            size_t          size,
                            size_t *        p_count);
static void verify_cleanup (discuart_t * p_context);

/** @brief Count zero-target frames and fragmented reports without any TX. */
void uartdisc_run (void)
{
    discuart_t context = {
        .p_port = &g_fake.port, .p_now = now_ms, .p_wait = uartdisc_wait};
    discuart_data_t result = {0};
    for (unsigned mode = 0U; mode <= 10U; ++mode)
    {
        reset(mode);
        assert(0 == discuart_run(&context, &g_config, &result));
        assert((0U == g_writes) && (NULL == g_fake.port.p_owner));
        if ((0U == mode) || (8U == mode))
        {
            assert((DISCUART_COMPLETE == result.status) &&
                   (3U == result.valid_reports) && (1000U == result.window_ms));
        }
        else if (1U == mode)
        {
            assert((DISCUART_COMPLETE == result.status) &&
                   (0U == result.received_bytes));
        }
        else if (2U == mode)
        {
            assert((DISCUART_COMPLETE == result.status) &&
                   (1U == result.discarded_bytes));
        }
        else if ((4U == mode) || (7U == mode))
        {
            assert(DISCUART_INCOMPLETE == result.status);
            assert(g_waits <= 2000U);
        }
        else if (9U == mode)
        {
            assert((DISCUART_COMPLETE == result.status) &&
                   (0U != result.framing_errors));
        }
        else if (10U == mode)
        {
            assert((DISCUART_COMPLETE == result.status) &&
                   (2U == result.valid_reports));
        }
        else
        {
            assert(DISCUART_FAULT == result.status);
        }
    }
    verify_cleanup(&context);
    puts("UART discovery: zero targets, fragments, silence, noise, "
         "receive/clock faults, no TX and retained cleanup passed.");
}

/** @brief Deterministic scheduler double feeds only during the listening
 * window. */
int uartdisc_wait (void)
{
    ++g_waits;
    if (4U != g_mode)
    {
        ++g_fake.now_ms;
    }
    if ((5U == g_mode) && (10U == g_waits))
    {
        g_fake.now_ms -= 2U;
    }
    if (7U == g_mode)
    {
        g_fake.now_ms += 2000U;
    }
    if ((3U == g_mode) && (5U == g_waits))
    {
        g_fake.read_error = UARTIO_OVERFLOW;
    }
    if ((2U == g_mode) && (5U == g_waits))
    {
        const uint8_t noise = 1U;
        assert(0 == uartfake_feed(&g_fake, &noise, 1U));
    }
    if ((1U != g_mode) && ((10U == g_waits) || (60U == g_waits) ||
                           ((110U == g_waits) && (10U != g_mode))))
    {
        uint8_t frame[30] = {0xAAU, 0xFFU, 3U, 0U};
        frame[28]         = 0x55U;
        frame[29]         = (9U == g_mode) ? 0U : 0xCCU;
        assert(0 == uartfake_feed(&g_fake, frame, sizeof(frame)));
    }
    return (6U == g_mode) ? -1 : 0;
}

/** @brief Exercise real admission and export a production reply for Python. */
void uartdisc_check (cfgdisc_t * p_context, const cfgwire_request_t * p_request)
{
    const discuart_t original = p_context->uart_probe;
    p_context->uart_probe     = (discuart_t){
            .p_port = &g_fake.port, .p_now = now_ms, .p_wait = uartdisc_wait};
    uint8_t payload[24] = {0};
    memcpy(payload, p_request->p_data, sizeof(payload));
    cfgbytes_put(&payload[4], 2U);
    cfgwire_request_t request = *p_request;
    request.p_data            = payload;
    uint8_t frame[136]        = {0};
    size_t  size              = 0U;
    reset(0U);
    assert(0 == cfgdisc_observe(p_context, &request, &frame[24], 112U, &size));
    assert((112U == size) && (0U == cfgbytes_get(&frame[28])));
    assert(0 == cfgwire_observation(&request, frame, sizeof(frame), size));
    printf("UARTDISCOVERY:");
    for (size_t index = 0U; index < sizeof(frame); ++index)
    {
        printf("%02X", (unsigned)frame[index]); /* Promotion matches printf. */
    }
    puts("");
    g_fake.close_error = UARTIO_IO;
    assert(0 == cfgdisc_observe(p_context, &request, &frame[24], 112U, &size));
    assert(p_context->p_control->b_recovery);
    assert(0 != cfgdisc_operations()->p_recover(p_context));
    g_fake.close_error = 0;
    assert(0 == cfgdisc_operations()->p_recover(p_context));
    p_context->p_control->b_recovery = false;
    p_context->uart_probe            = original;
}

/** @brief Keep one spy counter outside the fake's close-time buffer reset. */
static void reset (unsigned mode)
{
    uartfake_init(&g_fake);
    g_operations             = *g_fake.port.p_operations;
    g_operations.p_write     = forbidden_write;
    g_fake.port.p_operations = &g_operations;
    g_mode                   = mode;
    g_writes                 = 0U;
    g_waits                  = 0U;
    g_fake.now_ms            = 1000U;
    g_fake.read_limit        = (8U == mode) ? 1U : UTHOME_UART_BYTES;
}

/** @brief The clock is independent of port ownership. */
static int now_ms (uint64_t * p_now)
{
    *p_now = g_fake.now_ms;
    return 0;
}

/** @brief No discovery path may issue even one payload write. */
static int forbidden_write (void *          p_context,
                            const uint8_t * p_data,
                            size_t          size,
                            size_t *        p_count)
{
    assert((NULL != p_context) && (NULL != p_data) && (0U != size));
    ++g_writes;
    *p_count = 0U;
    return UARTIO_IO;
}

/** @brief Never steal another owner or discard a partial-open/failed-close
 * claim. */
static void verify_cleanup (discuart_t * p_context)
{
    discuart_data_t result = {0};
    reset(0U);
    assert(0 == uartio_open(&g_fake.port, &g_config, &g_fake));
    assert(0 == discuart_run(p_context, &g_config, &result));
    assert((DISCUART_BUSY == result.status) && (0U == g_waits));
    assert(0 == uartio_close(&g_fake.port, &g_fake));
    g_fake.open_error  = UARTIO_IO;
    g_fake.close_error = UARTIO_IO;
    assert(0 == discuart_run(p_context, &g_config, &result));
    assert((DISCUART_FAULT == result.status) &&
           (p_context == g_fake.port.p_owner));
    assert(0 != discuart_release(p_context));
    g_fake.close_error = 0;
    assert(0 == discuart_release(p_context));
    assert(NULL == g_fake.port.p_owner);
    reset(0U);
    g_fake.flush_error = UARTIO_IO;
    assert(0 == discuart_run(p_context, &g_config, &result));
    assert((DISCUART_FAULT == result.status) && (0U == g_waits));
}

/* End of file. */

