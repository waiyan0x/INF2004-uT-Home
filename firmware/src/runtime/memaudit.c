/**
 * @file memaudit.c
 * @brief Retain saturating allocation and object-creation evidence.
 */
#include "memaudit.h"

static void add_count (uint32_t * p_value, size_t amount, bool * pb_saturated);

/** @brief Establish the normal-operation boundary exactly once. */
int memaudit_seal (memaudit_t * p_state)
{
    if ((NULL == p_state) || (p_state->b_sealed) || (!p_state->b_application))
    {
        return -1;
    }
    p_state->b_sealed = true;
    return 0;
}

/** @brief Mark application entry once, retaining earlier SDK boot events. */
int memaudit_begin (memaudit_t * p_state)
{
    if ((NULL == p_state) || (p_state->b_application) || (p_state->b_sealed))
    {
        return -1;
    }
    p_state->b_application = true;
    return 0;
}

/** @brief Count a kernel create attempt before the native function executes. */
int memaudit_create (memaudit_t * p_state, uint32_t kind)
{
    if ((NULL == p_state) || (kind >= UTHOME_AUDIT_KINDS))
    {
        return -1;
    }
    memaudit_phase_t * p_phase =
        p_state->b_sealed ? &p_state->normal : &p_state->startup;
    add_count(&p_phase->creates[kind], 1U, &p_state->b_saturated);
    return 0;
}

/** @brief Count successful allocation/free hooks without storing addresses. */
int memaudit_heap (memaudit_t * p_state, size_t size, bool b_free)
{
    if (NULL == p_state)
    {
        return -1;
    }
    memaudit_phase_t * p_phase =
        p_state->b_sealed ? &p_state->normal : &p_state->startup;
    if (b_free)
    {
        add_count(&p_phase->frees, 1U, &p_state->b_saturated);
    }
    else
    {
        add_count(&p_phase->allocations, 1U, &p_state->b_saturated);
        add_count(&p_phase->bytes, size, &p_state->b_saturated);
    }
    if ((p_state->b_application) && (!p_state->b_sealed))
    {
        memaudit_heap_t * p_application = &p_state->application;
        if (b_free)
        {
            add_count(&p_application->frees, 1U, &p_state->b_saturated);
        }
        else
        {
            add_count(&p_application->allocations, 1U, &p_state->b_saturated);
            add_count(&p_application->bytes, size, &p_state->b_saturated);
        }
    }
    return 0;
}

/** @brief Observe attempts into the kernel's independent allocator. */
int memaudit_kernel (memaudit_t * p_state, uint32_t kind)
{
    if ((NULL == p_state) || (kind >= UTHOME_AUDIT_ALLOCATORS))
    {
        return -1;
    }
    memaudit_phase_t * p_phase =
        p_state->b_sealed ? &p_state->normal : &p_state->startup;
    add_count(&p_phase->kernel_calls[kind], 1U, &p_state->b_saturated);
    if ((p_state->b_application) && (!p_state->b_sealed))
    {
        add_count(&p_state->application.kernel_calls[kind],
                  1U,
                  &p_state->b_saturated);
    }
    return 0;
}

/** @brief Saturation remains visible instead of wrapping to a false zero. */
static void add_count (uint32_t * p_value, size_t amount, bool * pb_saturated)
{
    if (amount > (UINT32_MAX - *p_value))
    {
        *p_value      = UINT32_MAX;
        *pb_saturated = true;
    }
    else
    {
        /* The subtraction bound establishes that amount fits uint32_t. */
        *p_value += (uint32_t)amount;
    }
}

/* End of file. */

