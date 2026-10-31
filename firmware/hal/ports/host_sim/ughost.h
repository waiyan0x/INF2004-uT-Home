#ifndef UG_HOST_H
#define UG_HOST_H
#include "ugio.h"
#define UG_HOST_PINS   64
#define UG_HOST_BUFFER 512

typedef struct
{
    uint64_t         now;
    uint8_t          receive[UG_HOST_BUFFER];
    uint8_t          transmit[UG_HOST_BUFFER];
    size_t           rx_len;
    size_t           rx_pos;
    size_t           tx_len;
    size_t           rx_capacity;
    bool             b_uart_opened;
    bool             b_overflow;
    bool             b_configured[UG_HOST_PINS];
    bool             b_level[UG_HOST_PINS];
    ugio_direction_t direction[UG_HOST_PINS];
    ugio_edge_t      edges[UG_HOST_PINS];
    ugio_edge_fn_t * p_callbacks[UG_HOST_PINS];
    void *           p_callback_args[UG_HOST_PINS];
    uint16_t         duty[UG_HOST_PINS];
    uint32_t         frequency[UG_HOST_PINS];
    uint32_t         echo_delay_us;
    uint32_t         echo_high_us;
    ugio_status_t    capture_status;
} ughost_t;

void ughost_init (ughost_t *, ugio_t *);
ugio_status_t
ughost_receive (ughost_t * p_host, const uint8_t * p_bytes, size_t count);
ugio_status_t ughost_input (ughost_t *, unsigned, bool);
#endif

/* End of file. */

