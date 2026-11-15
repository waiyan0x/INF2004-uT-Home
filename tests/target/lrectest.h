/**
 * @file lrectest.h
 * @brief Host interrupt-mask double for the fixed lwIP record adapter.
 */
#ifndef UTHOME_LRECTEST_H
#define UTHOME_LRECTEST_H
#include <stddef.h>
#include <stdint.h>

uint32_t lrectest_enter (void);
void lrectest_leave (uint32_t saved);
uint32_t lrectest_mask (void);
size_t lrectest_calls (void);

#endif

/* End of file. */

