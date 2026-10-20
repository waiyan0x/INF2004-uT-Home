/**
 * @file c3uart.h
 * @brief Wrap the pinned kernel's exclusive UART1 device.
 */
#ifndef UTHOME_C3UART_H
#define UTHOME_C3UART_H
#include "uartio.h"
/* Factory performs no I/O. Open/close require the runtime lifecycle owner with
 * this port's worker stopped. UART1 uses native TX21/RX20, 9600..460800 baud,
 * 5..8 data bits, 1/2 stop bits and none/odd/even parity. RX capacity is
 * compiled by the kernel, not dynamically allocated. The resource patch
 * supplies 512 usable bytes. GPIO20/21 remain reserved even while closed. USB
 * is separate. No other code may directly access sera or change its attributes.
 * Call c3uart_init during startup, after runtime owner initialization and
 * before restore/activation. Failed initialization is sticky until reboot.
 * Open/close claim/release logical ownership; native handles live until reboot.
 * Workers must be stopped for lifecycle calls. Close aborts queued TX and
 * discards RX; it cannot recall a byte already transmitted. Cleanup failure
 * retains ownership. Unlock failure poisons the port until reboot. */
int c3uart_init (void);
uartio_t * c3uart_port (void);
#endif
/* End of file. */

