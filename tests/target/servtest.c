/** @file servtest.c
 * @brief Check observation isolation, source freshness and supervisor behavior.
 */
#ifdef NDEBUG
#error Service tests require assertions
#endif
#include "servtest.h"
#include "telemetry.h"
#include "costview.h"
#include "statusmon.h"
#include "rulecalc.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static shared_capture_t g_capture = {0};
static cfgctrl_t   g_control = {.b_initialized = true, .active_revision = 17U};
static telemetry_t g_telemetry = {.p_control = &g_control};
static uint8_t     g_frame[UTHOME_CFGWIRE_HEADER + UTHOME_TELEMETRY_MAX] = {0};
static void verify_sources (uint64_t * p_clock);
static void verify_values (uint64_t now_ms);
static void verify_monitor (uint64_t * p_clock);
static void emit (void);
static void verify_rule (void);
static void verify_rule_metrics (void);
static int check_contention (void * p_context);
static int capture_audit (memaudit_t * p_state);
static void verify_cost (void);
static int cost_clock (uint64_t * p_now);
static int cost_stack (uint32_t * p_unused);
static int cost_cpu (costview_cpu_t * p_sample);
static int missing_cpu (costview_cpu_t * p_sample);

/** @brief Exercise production serialization and encode bytes for Python parity.
 */
void servtest_run (uint64_t * p_clock)
{
    assert(NULL != p_clock);
    assert(0 == activate_clear());
    verify_cost();
    verify_rule();
    verify_rule_metrics();
    verify_sources(p_clock);
    verify_values(*p_clock);
    emit();
    g_telemetry.p_audit = capture_audit;
    verify_monitor(p_clock);
    assert(0 == shared_reset(NULL, 0U));
    assert(0 == shared_capture(&g_capture));
    assert(!g_capture.samples.entries[0].b_expected);
    assert(0U == g_capture.queue_count);
    assert(0 == rt_port_shared_call(check_contention, NULL));
    puts("Services: independent sources, freshness, health, reset and wire "
         "parity passed.");
}

/** @brief Provide an independently recognizable high-word clock. */
static int cost_clock (uint64_t * p_now)
{
    *p_now = UINT64_C(0x100000005);
    return 0;
}

/** @brief Provide a checked empirical controller margin for encoding. */
static int cost_stack (uint32_t * p_unused)
{
    *p_unused = 4096U;
    return 0;
}

/** @brief Encode an independent high-word native tick and wrapping idle count.
 */
static int cost_cpu (costview_cpu_t * p_sample)
{
    *p_sample = (costview_cpu_t){.elapsed_ms = UINT64_C(0x200000009),
                                 .idle_ms    = UINT32_MAX - 2U,
                                 .tick_ms    = 1U};
    return 0;
}

/** @brief Partial provider output is never published after an error. */
static int missing_cpu (costview_cpu_t * p_sample)
{
    (void)cost_cpu(p_sample);
    return -1;
}

/** @brief Encode explicit missing clocks and unbound native slot history. */
static void verify_cost (void)
{
    static costview_t       view    = {.p_control = &g_control};
    size_t                  size    = 0U;
    const cfgwire_request_t request = {.command    = CFGWIRE_WORKCOST,
                                       .request_id = 27U};
    assert(costview_observe(NULL, g_frame, sizeof(g_frame), &size) != 0);
    assert(costview_observe(&view, g_frame, COSTVIEW_BYTES - 1U, &size) != 0);
    assert(0 ==
           costview_observe(&view, &g_frame[24], UTHOME_TELEMETRY_MAX, &size));
    assert((COSTVIEW_HEADER == size) && (2U == cfgbytes_get(&g_frame[28])));
    view.p_clock           = cost_clock;
    view.p_controller      = cost_stack;
    const uint32_t last_id = g_control.last_id;
    assert(0 ==
           costview_observe(&view, &g_frame[24], UTHOME_TELEMETRY_MAX, &size));
    assert((COSTVIEW_BYTES == size) && (0U == cfgbytes_get(&g_frame[28])));
    assert(last_id == g_control.last_id);
    assert(0 == cfgwire_observation(&request, g_frame, sizeof(g_frame), size));
    printf("WORKCOST:");
    for (size_t index = 0U; index < (size + 24U); ++index)
    {
        /* A byte fits the unsigned printf argument. */
        printf("%02x", (unsigned)g_frame[index]);
    }
    puts("");
    view.p_cpu = missing_cpu;
    assert(0 ==
           costview_observe(&view, &g_frame[24], UTHOME_TELEMETRY_MAX, &size));
    assert((2U == cfgbytes_get(&g_frame[24])) &&
           (0U == cfgbytes_get(&g_frame[84])) &&
           (0U == cfgbytes_get(&g_frame[88])));
    view.p_cpu = cost_cpu;
    assert(0 ==
           costview_observe(&view, &g_frame[24], UTHOME_TELEMETRY_MAX, &size));
    assert(last_id == g_control.last_id);
    assert(0 == cfgwire_observation(&request, g_frame, sizeof(g_frame), size));
    printf("SCHEDULER:");
    for (size_t index = 0U; index < (size + 24U); ++index)
    {
        printf("%02x", (unsigned)g_frame[index]);
    }
    puts("");
}

/** @brief Supply distinct counters to check both phases on the wire. */
static int capture_audit (memaudit_t * p_state)
{
    memset(p_state, 0, sizeof(*p_state));
    p_state->b_sealed                    = true;
    p_state->b_application               = true;
    p_state->b_saturated                 = true;
    p_state->startup.allocations         = 7U;
    p_state->startup.bytes               = 1024U;
    p_state->startup.frees               = 2U;
    p_state->normal.allocations          = 1U;
    p_state->normal.bytes                = 20U;
    p_state->normal.frees                = 3U;
    p_state->startup.kernel_calls[0]     = 8U;
    p_state->application.allocations     = 2U;
    p_state->application.bytes           = 16U;
    p_state->application.kernel_calls[0] = 1U;
    p_state->normal.kernel_calls[3]      = 4U;
    for (uint32_t index = 0U; index < UTHOME_AUDIT_KINDS; ++index)
    {
        p_state->startup.creates[index] = index;
        p_state->normal.creates[index]  = index + 11U;
    }
    return 0;
}

/** @brief Queue observation retains consumers, source identity and sequence
 * wrap. */
static void verify_sources (uint64_t * p_clock)
{
    const msgbus_queue_config_t queues[] = {{UTHOME_SAMPLE_DISTANCE,
                                             sizeof(sample_distance_t),
                                             1U,
                                             UTHOME_MSGBUS_DROP_OLDEST},
                                            {UTHOME_SAMPLE_ENVIRONMENT,
                                             sizeof(sample_environment_t),
                                             2U,
                                             UTHOME_MSGBUS_DROP_OLDEST},
                                            {UTHOME_SAMPLE_TARGETS,
                                             sizeof(sample_target_report_t),
                                             2U,
                                             UTHOME_MSGBUS_DROP_OLDEST},
                                            {UTHOME_SAMPLE_HEALTH,
                                             sizeof(sample_health_t),
                                             2U,
                                             UTHOME_MSGBUS_DROP_OLDEST}};
    assert(0 == shared_reset(queues, 4U));
    sampcache_source_t source = {.name         = "range_a",
                                 .h_queue      = 0,
                                 .type         = UTHOME_SAMPLE_DISTANCE,
                                 .source_index = 0U,
                                 .stale_ms     = 100U};
    assert(0 == shared_expect(&source));
    assert(-1 == shared_expect(&source));
    source.source_index = 1U;
    memcpy(source.name, "range_b", 8U);
    assert(0 == shared_expect(&source));
    assert(0 == shared_capture(&g_capture));
    sampcache_entry_t selected    = {0};
    uint64_t          captured_ms = 0U;
    assert(0 == shared_sample(1U, &selected, &captured_ms));
    assert(captured_ms == *p_clock);
    assert(0 == shared_source(1U, &selected, &captured_ms));
    assert((1U == selected.source.source_index) && (!selected.b_seen));
    assert(1 == shared_source(7U, &selected, &captured_ms));
    assert(!selected.b_expected);
    assert(-1 == shared_source(8U, &selected, &captured_ms));
    assert(0 == sampcache_select(1U, 0U, &selected));
    assert(0U == selected.source.source_index);
    assert(0 == sampcache_select(1U, 1U, &selected));
    assert(1U == selected.source.source_index);
    assert(0 == sampcache_select(1U, 2U, &selected));
    assert(0U == selected.source.source_index);
    assert(1 == sampcache_select(2U, 0U, &selected));
    assert(!selected.b_expected);
    assert(-1 == sampcache_select(0U, 0U, &selected));
    assert(0U == sampcache_quality(&g_capture.samples.entries[0], *p_clock));
    sample_distance_t sample   = {.distance_mm = 123U, .echo_us = 456U};
    sample.header.timestamp_ms = *p_clock;
    sample.header.sequence     = UINT32_MAX;
    sample.header.status       = UTHOME_SAMPLE_OK;
    assert(0 ==
           shared_publish(0, UTHOME_SAMPLE_DISTANCE, &sample, sizeof(sample)));
    assert(0 == shared_capture(&g_capture));
    assert(1U == g_capture.stats[0].current_depth);
    assert(0U == g_capture.stats[0].consumed_count);
    assert(1U == sampcache_quality(&g_capture.samples.entries[0], *p_clock));
    ++(*p_clock);
    sample.header.timestamp_ms = *p_clock;
    sample.header.sequence     = 0U;
    sample.distance_mm         = 321U;
    assert(0 ==
           shared_publish(0, UTHOME_SAMPLE_DISTANCE, &sample, sizeof(sample)));
    assert(0 == shared_capture(&g_capture));
    assert(321U == g_capture.samples.entries[0].value.distance.distance_mm);
    assert(1U == g_capture.health.event_count[UTHOME_HEALTH_QUEUE_OVERFLOW]);
    *p_clock += 100U;
    sample.header.timestamp_ms = *p_clock;
    sample.distance_mm         = 999U;
    /* Duplicate sequence cannot extend freshness despite its newer timestamp.
     */
    assert(0 ==
           shared_publish(0, UTHOME_SAMPLE_DISTANCE, &sample, sizeof(sample)));
    assert(0 == shared_capture(&g_capture));
    assert(2U == sampcache_quality(&g_capture.samples.entries[0], *p_clock));
    assert(321U == g_capture.samples.entries[0].value.distance.distance_mm);
    assert(0U == sampcache_quality(&g_capture.samples.entries[1], *p_clock));
    sample_distance_t consumed = {0};
    assert(0 == shared_consume(
                    0, UTHOME_SAMPLE_DISTANCE, &consumed, sizeof(consumed)));
    assert(999U == consumed.distance_mm);
    assert(0 == shared_capture(&g_capture));
    assert(321U == g_capture.samples.entries[0].value.distance.distance_mm);
}

/** @brief Sanitize invalid/future data and preserve signed target fields. */
static void verify_values (uint64_t now_ms)
{
    sampcache_source_t source = {.name         = "environment",
                                 .h_queue      = 1,
                                 .type         = UTHOME_SAMPLE_ENVIRONMENT,
                                 .source_index = 2U,
                                 .stale_ms     = 1000U};
    assert(0 == shared_expect(&source));
    sample_environment_t environment = {.pressure_pa = NAN};
    environment.header.timestamp_ms  = now_ms;
    environment.header.source_index  = 2U;
    environment.header.status        = UTHOME_SAMPLE_OK;
    assert(0 == shared_publish(1,
                               UTHOME_SAMPLE_ENVIRONMENT,
                               &environment,
                               sizeof(environment)));
    assert(0 == shared_capture(&g_capture));
    assert(3U == sampcache_quality(&g_capture.samples.entries[2], now_ms));
    environment.pressure_pa         = 100000.0F;
    environment.temperature_c       = -12.5F;
    environment.header.sequence     = 1U;
    environment.header.timestamp_ms = now_ms + 1000U;
    assert(0 == shared_publish(1,
                               UTHOME_SAMPLE_ENVIRONMENT,
                               &environment,
                               sizeof(environment)));
    assert(0 == shared_capture(&g_capture));
    assert(UTHOME_SAMPLE_INVALID == g_capture.samples.entries[2].header.status);
    environment.header.sequence     = 2U;
    environment.header.timestamp_ms = now_ms;
    assert(0 == shared_publish(1,
                               UTHOME_SAMPLE_ENVIRONMENT,
                               &environment,
                               sizeof(environment)));
    source = (sampcache_source_t){.name         = "targets",
                                  .h_queue      = 2,
                                  .type         = UTHOME_SAMPLE_TARGETS,
                                  .source_index = 3U,
                                  .stale_ms     = 400U};
    assert(0 == shared_expect(&source));
    const sample_target_report_t targets = {
        .header  = {.timestamp_ms = now_ms,
                    .source_index = 3U,
                    .status       = UTHOME_SAMPLE_OK},
        .targets = {{.x_mm          = -123,
                     .y_mm          = 456,
                     .speed_cm_s    = -7,
                     .resolution_mm = 100U,
                     .b_present     = true}}};
    assert(0 ==
           shared_publish(2, UTHOME_SAMPLE_TARGETS, &targets, sizeof(targets)));
}

/** @brief Monitor timing, historical health semantics and missed heartbeats. */
static void verify_monitor (uint64_t * p_clock)
{
    statusmon_t monitor = {0};
    assert(0 == statusmon_configure(&monitor, 1000U, 8U, *p_clock));
    assert(0 == shared_watch("worker", 100U));
    assert(0 == statusmon_step(&monitor, *p_clock));
    *p_clock += 101U;
    assert(0 == statusmon_step(&monitor, *p_clock));
    assert(0 == shared_capture(&g_capture));
    assert(1U ==
           g_capture.health.event_count[UTHOME_HEALTH_TASK_HEARTBEAT_MISSED]);
    assert(!g_capture.samples.entries[11].b_seen);
    *p_clock += 899U;
    assert(0 == statusmon_step(&monitor, *p_clock));
    assert(0 == shared_capture(&g_capture));
    assert(g_capture.samples.entries[11].b_seen);
    assert(UTHOME_SAMPLE_NO_SOURCE ==
           g_capture.samples.entries[11].source.source_index);
    assert(1U ==
           g_capture.health.event_count[UTHOME_HEALTH_TASK_HEARTBEAT_MISSED]);
    assert(0 == shared_heartbeat("worker"));
    *p_clock += 101U;
    assert(0 <= statusmon_step(&monitor, *p_clock));
    assert(0 == shared_capture(&g_capture));
    assert(2U ==
           g_capture.health.event_count[UTHOME_HEALTH_TASK_HEARTBEAT_MISSED]);
    emit();
}

/** @brief Let the Python decoder independently verify real C-encoded bytes. */
static void emit (void)
{
    size_t                  size    = 0U;
    const cfgwire_request_t request = {.command    = CFGWIRE_TELEMETRY,
                                       .request_id = 27U};
    assert(0 == telemetry_observe(
                    &g_telemetry, &g_frame[24], UTHOME_TELEMETRY_MAX, &size));
    assert(0 == cfgwire_observation(&request, g_frame, sizeof(g_frame), size));
    printf("TELEMETRY:");
    for (size_t index = 0U; index < (size + 24U); ++index)
    {
        /* A byte is representable in unsigned for the printf conversion. */
        printf("%02x", (unsigned)g_frame[index]);
    }
    puts("");
}

/** @brief Shared capture cannot recursively enter its nonblocking guard. */
static int check_contention (void * p_context)
{
    /* This test uses static capture storage; no callback data is needed. */
    (void)p_context;
    assert(UTHOME_PORT_ERR_BUSY == shared_capture(&g_capture));
    sampcache_entry_t entry  = {0};
    uint64_t          now_ms = 0U;
    assert(UTHOME_PORT_ERR_BUSY == shared_sample(1U, &entry, &now_ms));
    assert(UTHOME_PORT_ERR_BUSY == shared_source(0U, &entry, &now_ms));
    return 0;
}

/** @brief Check hysteresis, equality and immediate fail-closed transitions. */
static void verify_rule (void)
{
    rulecalc_config_t config = {.metric        = RULECALC_DISTANCE,
                                .threshold     = 400,
                                .hysteresis    = 50U,
                                .cooldown_ms   = 200U,
                                .max_age_ms    = 500U,
                                .duty_permille = 500U};
    rulecalc_state_t  state  = {0};
    sampcache_entry_t entry  = {
         .b_expected = true,
         .b_seen     = true,
         .source     = {.type = UTHOME_SAMPLE_DISTANCE, .stale_ms = 1000U},
         .header     = {.timestamp_ms = 1000U, .status = UTHOME_SAMPLE_OK}};
    uint16_t duty                    = 99U;
    entry.value.distance.distance_mm = 400U;
    assert(0 == rulecalc_step(&config, &state, &entry, 1000U, &duty));
    assert(0U == duty);
    entry.value.distance.distance_mm = 399U;
    assert(0 == rulecalc_step(&config, &state, &entry, 1001U, &duty));
    assert(500U == duty);
    entry.value.distance.distance_mm = 449U;
    assert(0 == rulecalc_step(&config, &state, &entry, 1201U, &duty));
    assert(500U == duty);
    entry.value.distance.distance_mm = 450U;
    assert(0 == rulecalc_step(&config, &state, &entry, 1202U, &duty));
    assert(0U == duty);
    entry.value.distance.distance_mm = 100U;
    assert(0 == rulecalc_step(&config, &state, &entry, 1203U, &duty));
    assert(0U == duty);
    assert(0 == rulecalc_step(&config, &state, &entry, 1402U, &duty));
    assert(500U == duty);
    assert(0 == rulecalc_step(&config, &state, &entry, 1500U, &duty));
    assert(0U == duty); /* Exact maximum age overrides cooldown. */
    entry.header.timestamp_ms = 1501U;
    assert(0 == rulecalc_step(&config, &state, &entry, 1501U, &duty));
    assert(0U == duty);
    assert(0 == rulecalc_step(&config, &state, &entry, 1700U, &duty));
    assert(500U == duty);
    entry.header.status = UTHOME_SAMPLE_INVALID;
    assert(0 == rulecalc_step(&config, &state, &entry, 1701U, &duty));
    assert(0U == duty);
    entry.header.status              = UTHOME_SAMPLE_OK;
    config.cooldown_ms               = 0U;
    config.b_above                   = true;
    entry.header.timestamp_ms        = 1800U;
    entry.value.distance.distance_mm = 401U;
    assert(0 == rulecalc_step(&config, &state, &entry, 1800U, &duty));
    assert(500U == duty);
    entry.value.distance.distance_mm = 351U;
    assert(0 == rulecalc_step(&config, &state, &entry, 1801U, &duty));
    assert(500U == duty);
    entry.value.distance.distance_mm = 350U;
    assert(0 == rulecalc_step(&config, &state, &entry, 1802U, &duty));
    assert(0U == duty);
    config.hysteresis                = 0U;
    entry.value.distance.distance_mm = 400U;
    assert(0 == rulecalc_step(&config, &state, &entry, 1803U, &duty));
    assert(0U == duty);
    entry.value.distance.distance_mm = UINT32_MAX;
    assert(0 == rulecalc_step(&config, &state, &entry, 1804U, &duty));
    assert(500U == duty);
    assert(0 == rulecalc_step(&config, &state, &entry, 1803U, &duty));
    assert(0U == duty); /* Clock regression overrides a valid measurement. */
    assert(0 == rulecalc_step(&config, &state, NULL, 1804U, &duty));
    assert(0U == duty);
    entry.source.source_index = 1U;
    assert(0 == rulecalc_step(&config, &state, &entry, 1805U, &duty));
    assert(0U == duty);
    config.duty_permille = 1001U;
    assert(-1 == rulecalc_step(&config, &state, &entry, 1805U, &duty));
    assert(0U == duty);
}

/** @brief Route environment and target metrics with independent rule state. */
static void verify_rule_metrics (void)
{
    rulecalc_config_t config = {.metric        = RULECALC_TEMPERATURE,
                                .threshold     = -1000,
                                .max_age_ms    = 500U,
                                .duty_permille = 250U};
    rulecalc_state_t  first  = {0};
    rulecalc_state_t  second = {0};
    sampcache_entry_t entry  = {
         .b_expected = true,
         .b_seen     = true,
         .source     = {.type = UTHOME_SAMPLE_ENVIRONMENT, .stale_ms = 1000U},
         .header     = {.timestamp_ms = 1000U, .status = UTHOME_SAMPLE_OK}};
    uint16_t duty                         = 0U;
    entry.value.environment.temperature_c = -12.5F;
    assert(0 == rulecalc_step(&config, &first, &entry, 1000U, &duty));
    assert(250U == duty);
    entry.value.environment.temperature_c = -9.5F;
    assert(0 == rulecalc_step(&config, &second, &entry, 1000U, &duty));
    assert((0U == duty) && (first.b_active) && (!second.b_active));
    entry.value.environment.temperature_c = NAN;
    assert(0 == rulecalc_step(&config, &first, &entry, 1001U, &duty));
    assert(0U == duty);
    config.metric                       = RULECALC_PRESSURE;
    config.threshold                    = 100000;
    config.b_above                      = true;
    entry.value.environment.pressure_pa = 100001.0F;
    assert(0 == rulecalc_step(&config, &first, &entry, 1002U, &duty));
    assert(250U == duty);
    entry.header.timestamp_ms = 1004U;
    assert(0 == rulecalc_step(&config, &first, &entry, 1003U, &duty));
    assert(0U == duty);
    entry.header.timestamp_ms = 1003U;
    config.metric             = RULECALC_TARGETS;
    config.threshold          = 0;
    entry.source.type         = UTHOME_SAMPLE_TARGETS;
    memset(&entry.value, 0, sizeof(entry.value));
    entry.value.targets.targets[2].b_present = true;
    assert(0 == rulecalc_step(&config, &first, &entry, 1004U, &duty));
    assert(250U == duty);
    entry.source.type = UTHOME_SAMPLE_DISTANCE;
    assert(0 == rulecalc_step(&config, &first, &entry, 1005U, &duty));
    assert(0U == duty);
}

/* End of file. */

