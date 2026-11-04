/**
 * @file oledwork.c
 * @brief Execute the configured bounded output service.
 */
#include "oledwork.h"

#include "shared.h"
#include "oledview.h"

/** @brief Render one copied observation, releasing the guard before SPI I/O. */
int oledwork_step (oledwork_t * p_display)
{
    sampcache_entry_t entry  = {0};
    uint64_t          now_ms = 0U;
    const int result = shared_sample(p_display->sources, &entry, &now_ms);
    /* Contention shows unavailable data and retries on the next worker period;
     * a previous FRESH label must not remain indefinitely on a blocked reader.
     */
    if (0 != oledview_render(&p_display->instance.display,
                             (0 == result) ? &entry : NULL,
                             now_ms))
    {
        return SPIBUS_STATE;
    }
    return oledinst_step(&p_display->instance);
}

/* End of file. */

