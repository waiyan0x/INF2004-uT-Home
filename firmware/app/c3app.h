/**
 * @file c3app.h
 * @brief Run the general-purpose configuration application from usermain.
 */
#ifndef UTHOME_C3APP_H
#define UTHOME_C3APP_H
#include <stdint.h>
#include "costview.h"
/* Called once by the existing 8 KiB initial task with an idle task running.
 * Returns only on initialization, clock or kernel-delay failure. The caller
 * must halt rather than reinitialize retained configuration/driver contexts.
 * p_build_hash borrows 32 bytes of the actual image's ELF SHA-256. The C11
 * board entry point obtains it from the SDK; the application copies it. */
int c3app_run (const uint8_t *       p_build_hash,
               costview_stack_fn_t * p_idle,
               costview_cpu_fn_t *   p_cpu);
#endif
/* End of file. */

