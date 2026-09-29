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
 * An unexpected native close error poisons this port until reboot: the kernel
 * frees the handle before reporting driver errors, so retrying that handle
 * would be unsafe. */
uartio_t * c3uart_port (void);
#endif
/* End of file. */

