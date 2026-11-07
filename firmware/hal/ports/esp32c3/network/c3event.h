/**
 * @file c3event.h
 * @brief Fixed event-group ABI over startup-created native notifications.
 */
#ifndef UTHOME_C3EVENT_H
#define UTHOME_C3EVENT_H

#include <stdint.h>

int c3event_init (void);
void * c3event_create (void);
void * c3event_static (void * p_key);
void c3event_delete (void * p_handle);
uint32_t c3event_set (void * p_handle, uint32_t bits);
int32_t c3event_set_isr (void * p_handle, uint32_t bits, int32_t * p_woken);
uint32_t c3event_clear (void * p_handle, uint32_t bits);
int32_t c3event_clear_isr (void * p_handle, uint32_t bits);
uint32_t c3event_get (void * p_handle);
uint32_t c3event_wait (
    void * p_handle, uint32_t bits, int32_t clear, int32_t all, uint32_t ticks);
uint32_t c3event_sync (void *   p_handle,
                       uint32_t set_bits,
                       uint32_t wait_bits,
                       uint32_t ticks);

#endif

/* End of file. */

