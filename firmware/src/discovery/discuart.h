/**
 * @file discuart.h
 * @brief Collect bounded receive-only evidence using the existing UART parser.
 */
#ifndef UTHOME_DISCUART_H
#define UTHOME_DISCUART_H
#include "uartio.h"
typedef int discuart_wait_t (void);
typedef int discuart_clock_t (uint64_t * p_now);

typedef enum
{
    DISCUART_COMPLETE   = 0,
    DISCUART_BUSY       = 1,
    DISCUART_FAULT      = 2,
    DISCUART_INVALID    = 3,
    DISCUART_INCOMPLETE = 5
} discuart_result_t;

typedef struct
{
    discuart_result_t status;
    uint64_t          started_ms;
    uint32_t          elapsed_ms;
    uint32_t          received_bytes;
    uint32_t          valid_reports;
    uint32_t          framing_errors;
    uint32_t          discarded_bytes;
    uint32_t          receive_errors;
    uint32_t          pending_bytes;
    uint32_t          window_ms;
} discuart_data_t;

/* Serialized, noncopyable lifetime storage. Callbacks are bounded; wait yields
 * for at least one millisecond. No transmit calls, heap storage or model match.
 * The existing UART backend configures TX idle but no payload is transmitted.
 */
typedef struct
{
    uartio_t *         p_port;
    discuart_clock_t * p_now;
    discuart_wait_t *  p_wait;
    bool               b_busy;
} discuart_t;

int discuart_run (discuart_t *            p_context,
                  const uartio_config_t * p_config,
                  discuart_data_t *       p_result);
int discuart_release (discuart_t * p_context);
#endif
/* End of file. */

