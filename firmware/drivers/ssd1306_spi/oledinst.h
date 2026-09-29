/**
 * @file oledinst.h
 * @brief Bind an SSD1306 frame sink to the generic component registry.
 */
#ifndef UTHOME_OLEDINST_H
#define UTHOME_OLEDINST_H
#include "ssd1306.h"
#include "uthome/drvreg.h"

typedef struct
{
    spibus_device_t * p_spi;
    digiout_t         data_line;
    digiout_t         reset;
    ssd1306_t         display;
    int               last_io_error;
} oledinst_t;

/* Configure bytes are ssd1306_config_t. Borrowed SPI/outputs are prebound;
 * configure and cleanup run under the lifecycle owner. The display worker alone
 * writes/copies frames and calls step; arbitrary concurrent component_write is
 * unsupported. Formatting, queue consumption and stale-input policy belong to
 * a separate display service, not this controller driver. */
const drvreg_descriptor_t * oledinst_descriptor (void);
int oledinst_step (void * p_context);
int oledinst_cleanup (void * p_context);
#endif
/* End of file. */

