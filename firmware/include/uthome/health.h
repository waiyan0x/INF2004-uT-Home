/**
 * @file health.h
 * @brief Count and rate-limit stable system health events.
 */
#ifndef UTHOME_HEALTH_H
#define UTHOME_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Minimum interval before the same health event
 * may be emitted again.
 *
 * Repeated faults are still counted even when
 * their output is rate-limited.
 */
#define UTHOME_HEALTH_RATE_LIMIT_MS 1000U

/*
 * Stable health event codes.
 *
 * These values form part of the public firmware
 * contract and should not depend on ESP32 or
 * μT-Kernel implementation types.
 */
typedef enum
{
    UTHOME_HEALTH_CONFIG_INVALID = 0,
    UTHOME_HEALTH_PIN_CONFLICT,
    UTHOME_HEALTH_DEVICE_TIMEOUT,
    UTHOME_HEALTH_UART_FRAME_INVALID,
    UTHOME_HEALTH_QUEUE_OVERFLOW,
    UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
    UTHOME_HEALTH_DEVICE_IO,
    UTHOME_HEALTH_UART_RX_OVERFLOW,

    UTHOME_HEALTH_EVENT_COUNT
} health_event_t;

/*
 * Health operation result codes.
 */
typedef enum
{
    UTHOME_HEALTH_OK = 0,

    /*
     * Event was counted, but output should
     * be suppressed because of rate limiting.
     */
    UTHOME_HEALTH_RATE_LIMITED = 1,

    UTHOME_HEALTH_ERR_INVALID = -1
} health_result_t;

/*
 * Snapshot of current system health counters.
 */
typedef struct
{
    uint32_t event_count[UTHOME_HEALTH_EVENT_COUNT];
    uint32_t suppressed_count[UTHOME_HEALTH_EVENT_COUNT];

    uint32_t total_events;
    uint32_t total_suppressed;

    health_event_t last_event;
    uint32_t       last_event_time_ms;

    bool b_has_last_event;
} health_snapshot_t;

/*
 * Initialise the health monitor.
 */
int health_init (void);

/*
 * Report one health event.
 *
 * now_ms is the current monotonic runtime time.
 *
 * The event is always counted.
 * Repeated output may be rate-limited.
 */
int health_report (health_event_t event, uint32_t now_ms);

/*
 * Retrieve a copy of the current health state.
 */
int health_get_snapshot (health_snapshot_t * p_snapshot);

/*
 * Retrieve the total occurrence count for
 * one specific health event.
 */
int health_get_event_count (health_event_t event, uint32_t * p_count);

#endif

/* End of file. */

