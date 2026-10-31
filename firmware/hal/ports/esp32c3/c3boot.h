/**
 * @file c3boot.h
 * @brief Bounded native startup pool for the static networking experiment.
 */
#ifndef UTHOME_C3BOOT_H
#define UTHOME_C3BOOT_H

#include <stdint.h>

/** @brief Replace only the pinned weak startup boundary in selected builds. */
void c3boot_start (void);
/** @brief Linker aliases retain the pinned kernel's int and pointer ABIs. */
int32_t c3boot_native_main (void);
extern void * gp_c3boot_begin;
extern void * gp_c3boot_end;

#endif

/* End of file. */

