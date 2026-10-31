/**
 * @file uartcheck.h
 * @brief Board acceptance of the real UART adapter on an electrically quiet
 * port.
 */
#ifndef UTHOME_UARTCHECK_H
#define UTHOME_UARTCHECK_H
#include <tk/tkernel.h>
int uartcheck_run (void);
/* Test-only replacement at the c3uart call site; forwards to the real kernel.
 */
ER uartcheck_read (ID h_device, W start, void * p_data, SZ size, SZ * p_count);
#endif
/* End of file. */

