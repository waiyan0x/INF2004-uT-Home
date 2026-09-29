/**
 * @file uartio.h
 * @brief Own one UART and perform bounded, nonblocking byte transfers.
 */
#ifndef UTHOME_UARTIO_H
#define UTHOME_UARTIO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UTHOME_UART_BYTES 64U

typedef enum
{
    UARTIO_OK       = 0,
    UARTIO_AGAIN    = 1,
    UARTIO_INVALID  = -1,
    UARTIO_STATE    = -2,
    UARTIO_IO       = -3,
    UARTIO_OVERFLOW = -4,
    UARTIO_FRAME    = -5,
    UARTIO_TIMEOUT  = -6
} uartio_result_t;

typedef struct
{
    uint32_t baud;
    uint16_t rx_buffer_bytes;
    uint8_t  port;
    uint8_t  tx_gpio;
    uint8_t  rx_gpio;
    uint8_t  data_bits;
    uint8_t  stop_bits;
    uint8_t  parity; /* 0 = none, 1 = odd, 2 = even; no flow control. */
} uartio_config_t;

typedef int uartio_open_fn_t (void *                  p_context,
                              const uartio_config_t * p_config);
typedef int uartio_close_fn_t (void * p_context);
typedef int uartio_read_fn_t (void *    p_context,
                              uint8_t * p_data,
                              size_t    capacity,
                              size_t *  p_count);
typedef int uartio_write_fn_t (void *          p_context,
                               const uint8_t * p_data,
                               size_t          size,
                               size_t *        p_count);
typedef int uartio_now_fn_t (void * p_context, uint64_t * p_now);

typedef struct
{
    uartio_open_fn_t *  p_open;
    uartio_close_fn_t * p_close;
    uartio_read_fn_t *  p_read;
    uartio_write_fn_t * p_write;
    uartio_close_fn_t * p_flush;
    uartio_now_fn_t *   p_now;
} uartio_ops_t;

typedef struct
{
    const uartio_ops_t * p_operations;
    void *               p_context;
    const void *         p_owner;
    bool                 b_ready;
} uartio_t;

/* Port, callbacks and owner are borrowed and must stay alive. Do not copy a
 * live port. One lifecycle owner opens/closes with users stopped. A failed open
 * may retain a claim: close with the same owner before reuse. Close failure
 * retains ownership for retry. Each transfer is limited to 64 bytes. Backends
 * serialize calls without waiting for input/space; AGAIN means retry later.
 * Read errors set count zero; all returned bytes must then be discarded.
 * Write count reports accepted bytes, including partial failures; acceptance
 * does not prove wire completion. Flush discards only a bounded RX snapshot. */
int uartio_open (uartio_t *              p_port,
                 const uartio_config_t * p_config,
                 const void *            p_owner);
int uartio_close (uartio_t * p_port, const void * p_owner);
int uartio_read (uartio_t *   p_port,
                 const void * p_owner,
                 uint8_t *    p_data,
                 size_t       capacity,
                 size_t *     p_count);
int uartio_write (uartio_t *      p_port,
                  const void *    p_owner,
                  const uint8_t * p_data,
                  size_t          size,
                  size_t *        p_count);
int uartio_flush (uartio_t * p_port, const void * p_owner);
int uartio_now (uartio_t * p_port, const void * p_owner, uint64_t * p_now);
#endif
/* End of file. */

