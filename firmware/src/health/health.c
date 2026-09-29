/**
 * @file health.c
 * @brief Count health events and rate-limit repeated event emissions.
 */
#include "uthome/health.h"

#include <stdbool.h>
#include <string.h>

typedef struct
{
    uint32_t count;
    uint32_t suppressed_count;

    uint32_t last_emit_time_ms;
    bool     b_has_emitted;
} health_event_state_t;

static health_event_state_t g_event_state[UTHOME_HEALTH_EVENT_COUNT] = {0U};

static health_snapshot_t g_snapshot = {0U};

/*
 * Check whether an event code is valid.
 */
static bool valid_event (health_event_t event);

/**
 * @brief Clear all event counters and rate-limit history.
 * @return Health success, rate-limit indication, or validation error as
 * applicable.
 */
int health_init (void)
{
    memset(g_event_state, 0, sizeof(g_event_state));

    memset(&g_snapshot, 0, sizeof(g_snapshot));

    return UTHOME_HEALTH_OK;
}

/**
 * @brief Count an occurrence and decide whether its output may be emitted.
 * @param event Health event code to inspect or record.
 * @param now_ms Monotonic time in milliseconds with unsigned wraparound
 * semantics.
 * @return Health success, rate-limit indication, or validation error as
 * applicable.
 */
int health_report (health_event_t event, uint32_t now_ms)
{
    if (!valid_event(event))
    {
        return UTHOME_HEALTH_ERR_INVALID;
    }

    health_event_state_t * p_state = &g_event_state[event];

    /*
     * Every occurrence is counted,
     * even if its output is suppressed.
     */
    ++p_state->count;

    ++g_snapshot.event_count[event];
    ++g_snapshot.total_events;

    g_snapshot.last_event         = event;
    g_snapshot.last_event_time_ms = now_ms;
    g_snapshot.b_has_last_event   = true;

    /*
     * First occurrence is always emitted.
     */
    if (!p_state->b_has_emitted)
    {
        p_state->b_has_emitted     = true;
        p_state->last_emit_time_ms = now_ms;

        return UTHOME_HEALTH_OK;
    }

    const uint32_t elapsed = now_ms - p_state->last_emit_time_ms;

    /*
     * Same event occurred too soon.
     *
     * Count it, but signal that serial/display
     * output should be suppressed.
     */
    if (elapsed < UTHOME_HEALTH_RATE_LIMIT_MS)
    {
        ++p_state->suppressed_count;

        ++g_snapshot.suppressed_count[event];
        ++g_snapshot.total_suppressed;

        return UTHOME_HEALTH_RATE_LIMITED;
    }

    /*
     * Enough time has passed.
     * Permit the event to be emitted again.
     */
    p_state->last_emit_time_ms = now_ms;

    return UTHOME_HEALTH_OK;
}

/**
 * @brief Copy the current health counters to the caller.
 * @param p_snapshot Destination for a copy of the current health snapshot.
 * @return Health success, rate-limit indication, or validation error as
 * applicable.
 */
int health_get_snapshot (health_snapshot_t * p_snapshot)
{
    if (NULL == p_snapshot)
    {
        return UTHOME_HEALTH_ERR_INVALID;
    }

    *p_snapshot = g_snapshot;

    return UTHOME_HEALTH_OK;
}

/**
 * @brief Retrieve the occurrence count for one valid event.
 * @param event Health event code to inspect or record.
 * @param p_count Destination for the event occurrence count.
 * @return Health success, rate-limit indication, or validation error as
 * applicable.
 */
int health_get_event_count (health_event_t event, uint32_t * p_count)
{
    if ((!valid_event(event)) || (NULL == p_count))
    {
        return UTHOME_HEALTH_ERR_INVALID;
    }

    *p_count = g_event_state[event].count;

    return UTHOME_HEALTH_OK;
}

/**
 * @brief Check that an event is within the public event enumeration.
 * @param event Health event code to inspect or record.
 * @return true if valid, otherwise false.
 */
static bool valid_event (health_event_t event)
{
    return (event >= UTHOME_HEALTH_CONFIG_INVALID) &&
           (event < UTHOME_HEALTH_EVENT_COUNT);
}

/* End of file. */

