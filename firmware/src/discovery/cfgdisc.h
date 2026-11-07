/**
 * @file cfgdisc.h
 * @brief Admit one compiled connection preset and serialize raw observations.
 */
#ifndef UTHOME_CFGDISC_H
#define UTHOME_CFGDISC_H
#include "cfgctrl.h"
#include "disci2c.h"
#include "discuart.h"
#define UTHOME_DISC_REQUEST 24U
#define UTHOME_DISC_HEADER  80U
#define UTHOME_DISC_RECORD  16U

struct cfgbind_state;

typedef struct
{
    disci2c_open_t *  p_open;
    disci2c_close_t * p_close;
    uartio_t *        p_uart;
    discuart_wait_t * p_wait;
} cfgdisc_io_t;

typedef struct
{
    cfgctrl_t *            p_control;
    struct cfgbind_state * p_binding;
    disci2c_t              probe;
    discuart_t             uart_probe;
    uint8_t                build_hash[32];
} cfgdisc_t;

/* Zero-initialized lifetime storage. Init before cfgctrl_init, using the
 * returned controller operations and this context. This preserves discovery
 * cleanup ownership through the ordinary explicit RECOVER path. No I/O here.
 * The binding must be initialized and idle. Probe backends must be the same
 * permanent resources selected by its composition caller. Callback pointers
 * are copied; the borrowed UART context must outlive discovery. The hash is
 * the actual image's ELF SHA-256, not a host-supplied build name.
 */
int cfgdisc_init (cfgdisc_t *            p_context,
                  cfgctrl_t *            p_control,
                  struct cfgbind_state * p_binding,
                  const uint8_t *        p_build_hash,
                  const cfgdisc_io_t *   p_io);
const cfgctrl_ops_t * cfgdisc_operations (void);
int cfgdisc_observe (void *                    p_context,
                     const cfgwire_request_t * p_request,
                     uint8_t *                 p_data,
                     size_t                    capacity,
                     size_t *                  p_size);
#endif
/* End of file. */

