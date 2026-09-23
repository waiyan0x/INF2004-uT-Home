#include "uthome/health.h"

#include <stdbool.h>
#include <string.h>


typedef struct {
    uint32_t count;
    uint32_t suppressed_count;

    uint32_t last_emit_time_ms;
    bool has_emitted;
} health_event_state_t;


static health_event_state_t
    g_event_state[UTHOME_HEALTH_EVENT_COUNT];

static uthome_health_snapshot_t
    g_snapshot;


/*
 * Check whether an event code is valid.
 */
static bool valid_event(
    uthome_health_event_t event)
{
    return event >= 0 &&
           event < UTHOME_HEALTH_EVENT_COUNT;
}


int uthome_health_init(void)
{
    memset(
        g_event_state,
        0,
        sizeof(g_event_state)
    );

    memset(
        &g_snapshot,
        0,
        sizeof(g_snapshot)
    );

    return UTHOME_HEALTH_OK;
}


int uthome_health_report(
    uthome_health_event_t event,
    uint32_t now_ms)
{
    if (!valid_event(event)) {
        return UTHOME_HEALTH_ERR_INVALID;
    }

    health_event_state_t *state =
        &g_event_state[event];

    /*
     * Every occurrence is counted,
     * even if its output is suppressed.
     */
    ++state->count;

    ++g_snapshot.event_count[event];
    ++g_snapshot.total_events;

    g_snapshot.last_event = event;
    g_snapshot.last_event_time_ms = now_ms;
    g_snapshot.has_last_event = 1;


    /*
     * First occurrence is always emitted.
     */
    if (!state->has_emitted) {

        state->has_emitted = true;
        state->last_emit_time_ms = now_ms;

        return UTHOME_HEALTH_OK;
    }


    uint32_t elapsed =
        now_ms - state->last_emit_time_ms;


    /*
     * Same event occurred too soon.
     *
     * Count it, but signal that serial/display
     * output should be suppressed.
     */
    if (elapsed <
        UTHOME_HEALTH_RATE_LIMIT_MS) {

        ++state->suppressed_count;

        ++g_snapshot.suppressed_count[event];
        ++g_snapshot.total_suppressed;

        return UTHOME_HEALTH_RATE_LIMITED;
    }


    /*
     * Enough time has passed.
     * Permit the event to be emitted again.
     */
    state->last_emit_time_ms = now_ms;

    return UTHOME_HEALTH_OK;
}


int uthome_health_get_snapshot(
    uthome_health_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return UTHOME_HEALTH_ERR_INVALID;
    }

    *snapshot = g_snapshot;

    return UTHOME_HEALTH_OK;
}


int uthome_health_get_event_count(
    uthome_health_event_t event,
    uint32_t *count)
{
    if (!valid_event(event) ||
        count == NULL) {

        return UTHOME_HEALTH_ERR_INVALID;
    }

    *count =
        g_event_state[event].count;

    return UTHOME_HEALTH_OK;
}