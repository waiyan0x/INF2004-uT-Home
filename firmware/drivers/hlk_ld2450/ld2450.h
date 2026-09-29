/**
 * @file ld2450.h
 * @brief Poll a caller-owned LD2450 stream without waiting for a report.
 */
#ifndef UTHOME_LD2450_H
#define UTHOME_LD2450_H
#include "ldparse.h"
#include "uartio.h"

typedef struct
{
    uartio_t * p_port;
    ldparse_t  parser;
    uint64_t   last_report_ms;
    uint32_t   timeout_ms;
    uint32_t   timeouts;
    uint32_t   overflows;
    uint32_t   io_errors;
    bool       b_ready;
    bool       b_flush;
} ld2450_t;

/* Zero distinct storage before use; keep it alive through successful close.
 * Open requires 8N1 and >=512 RX bytes; baud must match the actual sensor.
 * Baseline: 256000 baud, 200..500 ms timeout, one poll every 10 ms. A poll
 * handles at most 64 bytes and returns the latest complete frame in that batch.
 * AGAIN is normal, publishes nothing. Errors zero the output. Timeout measures
 * time since a valid report/start, never time spent waiting in one call. */
int ld2450_open (ld2450_t *              p_sensor,
                 uartio_t *              p_port,
                 const uartio_config_t * p_config,
                 uint32_t                timeout_ms);
int ld2450_poll (ld2450_t * p_sensor, sample_target_report_t * p_report);
int ld2450_close (ld2450_t * p_sensor);
#endif
/* End of file. */

