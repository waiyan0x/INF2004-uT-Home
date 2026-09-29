/**
 * @file digiout.h
 * @brief Borrow an already configured digital output with checked writes.
 */
#ifndef UTHOME_DIGIOUT_H
#define UTHOME_DIGIOUT_H
#include <stdbool.h>
typedef int digiout_set_t (void * p_context, bool b_high);

typedef struct
{
    digiout_set_t * p_set;
    void *          p_context;
} digiout_t;

/* Set returns zero on success, completes promptly without sleeping, and retains
 * context ownership. A line is exclusive to one user; setup/cleanup is
 * separate. */
#endif
/* End of file. */

