/** @file uartdisc.h
 * @brief Exercise receive-only discovery and ownership failures.
 */
#ifndef UTHOME_UARTDISC_H
#define UTHOME_UARTDISC_H
#include "cfgdisc.h"
void uartdisc_run (void);
int uartdisc_wait (void);
void uartdisc_check (cfgdisc_t *               p_context,
                     const cfgwire_request_t * p_request);
#endif
/* End of file. */

