/**
 * @file digiin.h
 * @brief Borrow one configured digital input with a checked level read.
 */
#ifndef UTHOME_DIGIIN_H
#define UTHOME_DIGIIN_H
#include <stdbool.h>
typedef int digiin_get_t (void * p_context, bool * pb_high);

typedef struct
{
    digiin_get_t * p_get;
    void *         p_context;
} digiin_t;

/* Storage and pin are borrowed through explicit setup/cleanup. Reads do not
 * wait, debounce, or detect edges; pulse measurement uses pulseio separately.
 */
#endif
/* End of file. */

