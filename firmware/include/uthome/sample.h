/**
 * @file sample.h
 * @brief Define copied samples shared by drivers, queues and services.
 *
 * These are in-process records, not a USB or storage encoding. Adapters map
 * driver results explicitly to sample status. See firmware/CONTRACTS.md.
 */
#ifndef UTHOME_SAMPLE_H
#define UTHOME_SAMPLE_H

#include <stdbool.h>
#include <stdint.h>

#define UTHOME_SAMPLE_VERSION     1U
#define UTHOME_SAMPLE_MAX_TARGETS 3U
#define UTHOME_SAMPLE_NO_SOURCE   UINT16_MAX

/* Queue type identifiers. New formats receive new identifiers. */
typedef enum
{
    UTHOME_SAMPLE_ENVIRONMENT = 1U,
    UTHOME_SAMPLE_TARGETS     = 2U,
    UTHOME_SAMPLE_DISTANCE    = 3U,
    UTHOME_SAMPLE_HEALTH      = 4U
} sample_type_t;

/* A zero-initialized record is unknown, never a successful measurement.
 * These values are not component, HAL or kernel return codes. */
typedef enum
{
    UTHOME_SAMPLE_UNKNOWN = 0U,
    UTHOME_SAMPLE_OK      = 1U,
    UTHOME_SAMPLE_TIMEOUT = 2U,
    UTHOME_SAMPLE_IO      = 3U,
    UTHOME_SAMPLE_INVALID = 4U
} sample_status_t;

/* source_index identifies the device's position in the active plan, including
 * disabled instances. Global health uses NO_SOURCE. Queues must be drained or
 * cleared with producers and consumers stopped before replacing that plan.
 * timestamp_ms is kernel monotonic time at measurement/event completion.
 * sequence starts at zero per source and plan, advances per publication attempt
 * including failures, and wraps modulo 2^32. Status is a sample_status_t value.
 */
typedef struct
{
    uint64_t timestamp_ms;
    uint32_t sequence;
    uint16_t source_index;
    uint16_t status;
} sample_header_t;

/* Finite compensated values; measurements are usable only with status OK. */
typedef struct
{
    sample_header_t header;
    float           pressure_pa;
    float           temperature_c;
} sample_environment_t;

/* Distance in millimetres and measured echo pulse width in microseconds. */
typedef struct
{
    sample_header_t header;
    uint32_t        distance_mm;
    uint32_t        echo_us;
} sample_distance_t;

/* Preserve target slots; an absent slot has b_present false and zero values. */
typedef struct
{
    int16_t  x_mm;
    int16_t  y_mm;
    int16_t  speed_cm_s;
    uint16_t resolution_mm;
    bool     b_present;
} sample_target_t;

typedef struct
{
    sample_header_t header;
    sample_target_t targets[UTHOME_SAMPLE_MAX_TARGETS];
} sample_target_report_t;

/* event is a health_event_t value; occurrences is the cumulative event count.
 * Header status OK means the event record is valid, not that the node is well.
 */
typedef struct
{
    sample_header_t header;
    uint32_t        occurrences;
    uint16_t        event;
} sample_health_t;

#endif

/* End of file. */

