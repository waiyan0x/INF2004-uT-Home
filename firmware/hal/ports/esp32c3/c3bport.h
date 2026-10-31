/**
 * @file c3bport.h
 * @brief Header-isolated native services for the BLE diagnostic component.
 */
#ifndef UTHOME_C3BPORT_H
#define UTHOME_C3BPORT_H

#include "memaudit.h"
#include <stdint.h>

uint32_t c3bport_enter (void);
void c3bport_leave (uint32_t saved);
int c3bport_delay (uint32_t milliseconds);
void c3bport_exit (void);
int c3bport_capture (memaudit_t * p_audit);
int c3bport_seal (void);

#endif

/* End of file. */

