/** @file oledview.h
 * @brief Render normalized observations into an existing SSD1306 frame. */
#ifndef UTHOME_OLEDVIEW_H
#define UTHOME_OLEDVIEW_H
#include "sampcache.h"
#include "ssd1306.h"
/* Display worker only. No I/O, heap, extra framebuffer or retained sample.
 * NULL means unavailable data. The current target profile uses 128x64.
 * Call the existing oledinst_step afterwards for bounded flush/retry. */
int oledview_render (ssd1306_t *               p_display,
                     const sampcache_entry_t * p_entry,
                     uint64_t                  now_ms);
#endif
/* End of file. */

