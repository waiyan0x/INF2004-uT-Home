/**
 * @file txsim.h
 * @brief Host test boundary through the real generic I2C driver and bus.
 */
#ifndef UTHOME_TXSIM_H
#define UTHOME_TXSIM_H
#include "txwire.h"
void txsim_init (void);
void txsim_exchange (const uint8_t * p_input, size_t size, uint8_t * p_output);
void txsim_fault (int status);
uint32_t txsim_calls (void);
uint32_t txsim_observed (uint32_t field);
size_t txsim_size (uint32_t field);
#endif

/* End of file. */

