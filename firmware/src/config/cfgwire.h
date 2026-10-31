/**
 * @file cfgwire.h
 * @brief Receive one bounded request without a transport or JSON dependency.
 */
#ifndef UTHOME_CFGWIRE_H
#define UTHOME_CFGWIRE_H
#include <stdbool.h>
#include "cfgbytes.h"
#define UTHOME_CFGWIRE_HEADER  24U
#define UTHOME_CFGWIRE_REPLY   48U
#define UTHOME_CFGWIRE_PAYLOAD 2048U
#define UTHOME_CFGWIRE_TIMEOUT 2000U

typedef enum
{
    CFGWIRE_APPLY        = 1,
    CFGWIRE_QUERY        = 2,
    CFGWIRE_SAVE         = 3,
    CFGWIRE_RECOVER      = 4,
    CFGWIRE_PROBE        = 5,
    CFGWIRE_TELEMETRY    = 6,
    CFGWIRE_CAPABILITIES = 7,
    CFGWIRE_DISCOVER     = 8,
    CFGWIRE_WORKCOST     = 9
} cfgwire_command_t;

typedef struct
{
    uint32_t          request_id;
    uint32_t          revision;
    size_t            size;
    const uint8_t *   p_data;
    cfgwire_command_t command;
} cfgwire_request_t;

typedef struct
{
    uint8_t  header[UTHOME_CFGWIRE_HEADER];
    uint8_t  data[UTHOME_CONFIG_BYTES];
    size_t   header_used;
    size_t   used;
    size_t   expected;
    uint64_t start_ms;
    uint64_t last_ms;
    bool     b_ready;
} cfgwire_t;

/* Zero-initialize; one serialized task supplies a monotonic millisecond clock.
 * feed returns 1 for a retained complete frame, 0 while waiting/scanning,
 * -1 for corruption/timeout and -2 for invalid arguments or a retained frame.
 * expire must also run when no bytes arrive. reset discards only this frame.
 * The borrowed request is valid until reset. Syntax/admission happens later. */
void cfgwire_reset (cfgwire_t * p_receiver);
int cfgwire_feed (cfgwire_t * p_receiver, uint8_t byte, uint64_t now_ms);
int cfgwire_expire (cfgwire_t * p_receiver, uint64_t now_ms);
int cfgwire_request (const cfgwire_t *   p_receiver,
                     cfgwire_request_t * p_request);
/* Reply payload is six explicitly encoded uint32 fields, defined by cfgctrl.
 * output points to at least UTHOME_CFGWIRE_REPLY bytes. */
int cfgwire_reply (const cfgwire_request_t * p_request,
                   const uint32_t *          p_fields,
                   uint8_t *                 p_output);
/* Frame a telemetry/capability/discovery payload at output+HEADER. No
 * overlap copy; capacity must cover header+size. This read-only command never
 * reaches cfgctrl. */
int cfgwire_observation (const cfgwire_request_t * p_request,
                         uint8_t *                 p_output,
                         size_t                    capacity,
                         size_t                    size);
#endif
/* End of file. */

