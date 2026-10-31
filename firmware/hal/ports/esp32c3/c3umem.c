/**
 * @file c3umem.c
 * @brief Supply typed static slots instead of native UART heap allocations.
 */
#include "c3umem.h"
#include <stdbool.h>
#include <string.h>
#include <sys/machine.h>
#include <tk/tkernel.h>
#include "ser.h"

#if (DEVCNF_USE_ADC != 0) || (DEV_SER_UNITNM != 1) || (TK_SUPPORT_MEMLIB != 1)
#error "Static UART storage requires one serial unit and native ADC disabled"
#endif

typedef struct
{
    T_SER_DCB serial;
    T_MSDI    record;
    bool      b_serial_used;
    bool      b_record_used;
    bool      b_sealed;
} c3umem_slots_t;

typedef union
{
    c3umem_slots_t slots;
    unsigned char  bytes[UTHOME_C3UMEM_BYTES];
} c3umem_storage_t;

/* C99 constant expressions reject a native ABI that exceeds the reservation. */
typedef enum
{
    C3UMEM_FITS    = 1 / (sizeof(c3umem_slots_t) <= UTHOME_C3UMEM_BYTES),
    C3UMEM_ALIGNED = 1 / (UTHOME_C3UMEM_BYTES == sizeof(c3umem_storage_t))
} c3umem_limits_t;

static c3umem_storage_t g_storage = {0};

/** @brief Claim the single serial DCB, with its embedded RX and TX rings. */
void * c3umem_serial_alloc (size_t size)
{
    if ((sizeof(T_SER_DCB) != size) || (g_storage.slots.b_sealed) ||
        (g_storage.slots.b_serial_used))
    {
        return NULL;
    }
    memset(&g_storage.slots.serial, 0, sizeof(g_storage.slots.serial));
    g_storage.slots.b_serial_used = true;
    return &g_storage.slots.serial;
}

/** @brief Permit serial registration failure to unwind before sealing. */
void c3umem_serial_free (void * p_memory)
{
    if ((!g_storage.slots.b_sealed) && (&g_storage.slots.serial == p_memory))
    {
        g_storage.slots.b_serial_used = false;
    }
}

/** @brief Claim the only mSDI record in the supported UART-only native build.
 */
void * c3umem_record_alloc (size_t size)
{
    if ((sizeof(T_MSDI) != size) || (g_storage.slots.b_sealed) ||
        (g_storage.slots.b_record_used))
    {
        return NULL;
    }
    memset(&g_storage.slots.record, 0, sizeof(g_storage.slots.record));
    g_storage.slots.b_record_used = true;
    return &g_storage.slots.record;
}

/** @brief Permit mSDI registration failure to unwind before sealing. */
void c3umem_record_free (void * p_memory)
{
    if ((!g_storage.slots.b_sealed) && (&g_storage.slots.record == p_memory))
    {
        g_storage.slots.b_record_used = false;
    }
}

/** @brief Freeze the slots even when startup provisioning was incomplete. */
int c3umem_seal (void)
{
    g_storage.slots.b_sealed = true;
    return ((g_storage.slots.b_serial_used) && (g_storage.slots.b_record_used))
               ? 0
               : -1;
}

/* End of file. */

