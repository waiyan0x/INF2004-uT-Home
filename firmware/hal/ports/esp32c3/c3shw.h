/**
 * @file c3shw.h
 * @brief Synchronous SPI2 register operations beneath bounded polling.
 */
#ifndef UTHOME_C3SHW_H
#define UTHOME_C3SHW_H

#include "c3spi.h"

/* Internal boundary: caller owns the bus mutex and validated pin/clock inputs.
 * These calls never poll, yield or allocate. Chunk buffers contain 64 padded
 * bytes even for a shorter transfer because the SDK copies rounded words. */
void c3shw_pins_open (const c3spi_config_t * p_config);
void c3shw_stop (const c3spi_config_t * p_config);
void c3shw_attach (uint8_t pin);
void c3shw_detach (uint8_t pin);
void c3shw_select (uint8_t pin, bool b_selected);
void c3shw_reset (void);
int c3shw_clock (int source_hz, uint32_t clock_hz);
int c3shw_configure (int source_hz, const spibus_config_t * p_config);
bool c3shw_ready (bool b_update);
void c3shw_prepare (const uint8_t * p_bytes, size_t size, bool b_read);
void c3shw_start (void);
void c3shw_read (uint8_t * p_bytes, size_t size);

#endif

/* End of file. */

