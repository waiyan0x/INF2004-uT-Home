/**
 * @file c3nq.h
 * @brief Owned queue ABI for the pinned single-core networking shim.
 */
#ifndef UTHOME_C3NQ_H
#define UTHOME_C3NQ_H

#include <stddef.h>
#include <stdint.h>

/** @brief Reserve all native objects at early startup; never retry failure. */
int c3nq_init (void);
/** @brief Pinned early-init boundary; native rename retains exact ER ABI. */
int32_t c3nq_bootstrap (void);
int32_t c3nq_native_tables (void);
int32_t c3nq_scheduler_state (void);

void * c3nq_create (uint32_t length, uint32_t item_bytes, uint8_t kind);
void * c3nq_static (uint32_t  length,
                    uint32_t  item_bytes,
                    uint8_t * p_storage,
                    void *    p_key,
                    uint8_t   kind);
void * c3nq_counting (uint32_t maximum, uint32_t initial);
void * c3nq_counting_static (uint32_t maximum, uint32_t initial, void * p_key);
void * c3nq_mutex (uint8_t kind);
void * c3nq_mutex_static (uint8_t kind, void * p_key);
int32_t c3nq_send (void *       p_handle,
                   void const * p_item,
                   uint32_t     ticks,
                   int32_t      position);
int32_t c3nq_send_isr (void *       p_handle,
                       void const * p_item,
                       int32_t *    p_woken,
                       int32_t      position);
int32_t c3nq_receive (void * p_handle, void * p_item, uint32_t ticks);
int32_t c3nq_receive_isr (void * p_handle, void * p_item, int32_t * p_woken);
int32_t c3nq_peek (void * p_handle, void * p_item, uint32_t ticks);
int32_t c3nq_peek_isr (void * p_handle, void * p_item);
int32_t c3nq_take (void * p_handle, uint32_t ticks);
int32_t c3nq_give_isr (void * p_handle, int32_t * p_woken);
int32_t c3nq_take_recursive (void * p_handle, uint32_t ticks);
int32_t c3nq_give_recursive (void * p_handle);
void c3nq_delete (void * p_handle);
uint32_t c3nq_waiting (void * p_handle);
uint32_t c3nq_spaces (void * p_handle);
int32_t c3nq_empty_isr (void * p_handle);
int32_t c3nq_reset (void * p_handle);
int32_t c3nq_generic_reset (void * p_handle, int32_t new_queue);

#if defined(UTHOME_WIFI_PROBE) && UTHOME_WIFI_PROBE
typedef struct
{
    uint32_t calls;
    uint32_t failures;
    uint32_t length;
    uint32_t item_bytes;
    uint32_t kind;
} c3nq_diagnostic_t;

/** @brief Copy dynamic-API counters and last failure; isolated probe only. */
int c3nq_diagnostic (c3nq_diagnostic_t * p_report);
#endif

#endif

/* End of file. */

