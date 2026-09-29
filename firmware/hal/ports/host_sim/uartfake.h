/**
 * @file uartfake.h
 * @brief Deterministic UART buffers and faults for host tests.
 */
#ifndef UTHOME_UARTFAKE_H
#define UTHOME_UARTFAKE_H
#include "uartio.h"
#define UTHOME_UART_FAKE_BYTES 512U

typedef struct
{
    uartio_t        port;
    uartio_config_t config;
    uint8_t         receive[UTHOME_UART_FAKE_BYTES];
    uint8_t         transmit[UTHOME_UART_FAKE_BYTES];
    size_t          received;
    size_t          transmitted;
    size_t          read_limit;
    uint64_t        now_ms;
    uint32_t        reads;
    int             open_error;
    int             close_error;
    int             read_error;
    int             flush_error;
    bool            b_overflow;
    bool            b_open;
} uartfake_t;

void uartfake_init (uartfake_t * p_fake);
int uartfake_feed (uartfake_t * p_fake, const uint8_t * p_data, size_t size);
#endif
/* End of file. */

