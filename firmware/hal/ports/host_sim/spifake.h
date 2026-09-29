/**
 * @file spifake.h
 * @brief Capture bounded SPI transactions and inject transport/line failures.
 */
#ifndef UTHOME_SPIFAKE_H
#define UTHOME_SPIFAKE_H
#include "spibus.h"
#include "digiout.h"
#define UTHOME_SPI_LOG 32U

typedef struct
{
    bool     b_high;
    bool     b_fail;
    unsigned calls;
    bool     b_levels[16];
} spifake_line_t;

typedef struct
{
    spibus_config_t config;
    size_t          size;
    bool            b_data;
    uint8_t         bytes[UTHOME_SPI_BYTES];
} spifake_record_t;

typedef struct
{
    spibus_t         bus;
    uint64_t         now_ms;
    uint32_t         transfer_ms;
    unsigned         transfers;
    unsigned         fail_at;
    int              error;
    bool             b_attach_fail;
    bool             b_detach_fail;
    bool             b_selected;
    uint32_t         delayed_ms;
    spifake_line_t * p_data_lines[32];
    spifake_record_t records[UTHOME_SPI_LOG];
} spifake_t;

/* Single-threaded fixture. Logged D/C is an optional borrowed auxiliary line;
 * RX echoes TX or returns 0xA5 for read-only calls. No physical timing proof.
 */
void spifake_init (spifake_t * p_fake);
int spifake_set (void * p_context, bool b_high);
#endif
/* End of file. */

