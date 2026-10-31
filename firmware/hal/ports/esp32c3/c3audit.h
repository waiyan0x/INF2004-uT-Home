/**
 * @file c3audit.h
 * @brief Observe SDK heap hooks and native kernel create attempts.
 */
#ifndef UTHOME_C3AUDIT_H
#define UTHOME_C3AUDIT_H
#include "memaudit.h"
#include <tk/tkernel.h>

/* SDK hook ABI: void *(allocation), size_t bytes and uint32_t capabilities.
 * These callbacks never retain pointers, allocate, log or block. */
void c3audit_alloc (void * p_memory, size_t size, uint32_t capabilities);
void c3audit_free (void * p_memory);
/* The controller seals before restore/normal requests and captures counters.
 * Counters persist for the entire boot, including earlier SDK initialization.
 */
int c3audit_seal (void);
/* Called once at usermain entry, before the runtime owner is initialized. */
int c3audit_begin (void);
int c3audit_capture (memaudit_t * p_state);
/* Observe the sentinel of the single reviewed pre-application kernel
 * allocation: 8192 requested + 256 system bytes for the controller stack.
 * Native task introspection must confirm its owner/range. Multiple or
 * differently sized boot allocations make this unavailable instead of guessing
 * a range. */
int c3audit_stack_unused (uint32_t * p_unused);

/* The kernel uses signed SZ, distinct from the SDK's unsigned size_t ABI.
 * Count entry attempts; native calloc/realloc internals are not counted twice.
 */
void * c3audit_kmalloc (SZ size);
void * c3audit_native_malloc (SZ size);
void * c3audit_kcalloc (SZ count, SZ size);
void * c3audit_native_calloc (SZ count, SZ size);
void * c3audit_krealloc (void * p_memory, SZ size);
void * c3audit_native_realloc (void * p_memory, SZ size);
void c3audit_kfree (void * p_memory);
void c3audit_native_free (void * p_memory);

/* Exact native create signatures. Scoped definitions rename only the native
 * implementations; linker wrapping sends callers through counted entry points.
 * Native validation and results are unchanged, including failed attempts. */
ID c3audit_create_task (const T_CTSK * p_config);
ID c3audit_native_task (const T_CTSK * p_config);
ID c3audit_create_mutex (const T_CMTX * p_config);
ID c3audit_native_mutex (const T_CMTX * p_config);
ID c3audit_create_sem (const T_CSEM * p_config);
ID c3audit_native_sem (const T_CSEM * p_config);
ID c3audit_create_flag (const T_CFLG * p_config);
ID c3audit_native_flag (const T_CFLG * p_config);
ID c3audit_create_mailbox (const T_CMBX * p_config);
ID c3audit_native_mailbox (const T_CMBX * p_config);
ID c3audit_create_message (const T_CMBF * p_config);
ID c3audit_native_message (const T_CMBF * p_config);
ID c3audit_create_fixed_pool (const T_CMPF * p_config);
ID c3audit_native_fixed_pool (const T_CMPF * p_config);
ID c3audit_create_pool (const T_CMPL * p_config);
ID c3audit_native_pool (const T_CMPL * p_config);
ID c3audit_create_port (const T_CPOR * p_config);
ID c3audit_native_port (const T_CPOR * p_config);
ID c3audit_create_cycle (const T_CCYC * p_config);
ID c3audit_native_cycle (const T_CCYC * p_config);
ID c3audit_create_alarm (const T_CALM * p_config);
ID c3audit_native_alarm (const T_CALM * p_config);

#endif

/* End of file. */

