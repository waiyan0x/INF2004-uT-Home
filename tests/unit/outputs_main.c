/** @file outputs_main.c
 * @brief Exercise output timing, failure recovery, pin conflicts and rules. */
#include "outputs_main.h"
#include "fanctrl.h"
#include "signalout.h"
#include "pinpool.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#ifdef NDEBUG
#error "Output tests require active assertions."
#endif

typedef struct
{
    bool     b_level;
    unsigned failures;
    unsigned writes;
} line_t;

static int set (void * p_context, bool b_high);
static void relays (void);
static void signals (void);
static void temperature (void);
static void ownership (void);

/** @brief Run portable feature tests without any target hardware. */
int outputs_main_run (void)
{
    relays();
    signals();
    temperature();
    ownership();
    puts("PASS: relay polarity/dwell/faults; LED/buzzer timing/mute; "
         "temperature rules; pin conflicts.");
    return 0;
}

/** @brief Provide the standalone host executable entry point. */
int main (void)
{
    return outputs_main_run();
}

/** @brief Inject a finite number of failed digital writes. */
static int set (void * p_context, bool b_high)
{
    line_t * p_line = p_context;
    p_line->writes++;
    if (0U != p_line->failures)
    {
        p_line->failures--;
        return -1;
    }
    p_line->b_level = b_high;
    return 0;
}

/** @brief Verify startup, both polarities, forced OFF and retained failures. */
static void relays (void)
{
    for (unsigned polarity = 0U; polarity < 2U; ++polarity)
    {
        line_t                  line   = {0};
        relayctl_t              relay  = {0};
        const relayctl_config_t config = {{set, &line}, 1000U, 0U != polarity};
        assert(0 == relayctl_open(&relay, &config, 0U));
        assert(line.b_level != config.b_active_high);
        assert(-1 == relayctl_open(&relay, &config, 0U));
        assert(0 == relayctl_step(&relay, true, true, 999U));
        assert(!relay.b_on);
        assert(0 == relayctl_step(&relay, true, true, 1000U));
        assert((relay.b_on) && (line.b_level == config.b_active_high));
        assert(0 == relayctl_step(&relay, false, true, 1001U));
        assert(relay.b_on);
        assert(0 == relayctl_step(&relay, true, false, 1002U));
        assert(!relay.b_on);
        assert(0 == relayctl_step(&relay, true, true, 2002U));
        assert(relay.b_on);
        assert(-3 == relayctl_step(&relay, true, true, 1U));
        assert(!relay.b_on);
        line.failures = 1U;
        assert(-2 == relayctl_step(&relay, true, true, 1001U));
        assert((relay.b_fault) && (!relay.b_on));
        assert(-2 == relayctl_step(&relay, true, true, 3000U));
        line.failures = 2U;
        assert(-2 == relayctl_close(&relay));
        assert(relay.b_open);
        assert(0 == relayctl_close(&relay));
        assert(!relay.b_open);
        assert(0 == relayctl_open(&relay, &config, 4000U));
        assert(!relay.b_fault);
        assert(0 == relayctl_close(&relay));
        line.failures = 2U;
        assert(-2 == relayctl_open(&relay, &config, 5000U));
        assert((relay.b_open) && (relay.b_fault));
        assert(0 == relayctl_close(&relay));
    }
}

/** @brief Verify nonblocking phase, mute, steady LED and write failure latch.
 */
static void signals (void)
{
    line_t             line   = {0};
    signalout_t        signal = {0};
    signalout_config_t config = {{set, &line}, 100U, 900U, true};
    assert(0 == signalout_open(&signal, &config, 0U));
    assert(!line.b_level);
    assert(0 == signalout_step(&signal, true, false, 0U));
    assert(line.b_level);
    assert(0 == signalout_step(&signal, true, false, 100U));
    assert(!line.b_level);
    assert(0 == signalout_step(&signal, true, false, 5000U));
    assert(line.b_level);
    assert(0 == signalout_step(&signal, true, true, 5001U));
    assert(!line.b_level);
    assert(0 == signalout_step(&signal, true, false, 5002U));
    assert(line.b_level);
    assert(-3 == signalout_step(&signal, true, false, 1U));
    assert(!line.b_level);
    line.failures = 1U;
    assert(-2 == signalout_step(&signal, true, false, 2U));
    assert((signal.b_fault) && (!line.b_level));
    assert(-2 == signalout_step(&signal, true, false, 3U));
    line.failures = 2U;
    assert(-2 == signalout_close(&signal));
    assert(signal.b_open);
    assert(0 == signalout_close(&signal));
    config.off_ms        = 0U;
    config.b_active_high = false;
    assert(0 == signalout_open(&signal, &config, 0U));
    assert(line.b_level);
    assert(0 == signalout_step(&signal, true, false, 100000U));
    assert(!line.b_level);
    assert(0 == signalout_close(&signal));
    config.on_ms = 0U;
    assert(-1 == signalout_open(&signal, &config, 0U));
}

/** @brief Exercise the real rule evaluator with the relay and copied samples.
 */
static void temperature (void)
{
    line_t                  line   = {0};
    fanctrl_t               fan    = {0};
    const relayctl_config_t output = {{set, &line}, 1000U, true};
    const rulecalc_config_t rule   = {.threshold     = 3000,
                                      .hysteresis    = 200U,
                                      .cooldown_ms   = 0U,
                                      .max_age_ms    = 500U,
                                      .metric        = RULECALC_TEMPERATURE,
                                      .source_index  = 0U,
                                      .duty_permille = 1000U,
                                      .b_above       = true};
    sampcache_entry_t       entry  = {0};
    entry.b_expected               = true;
    entry.b_seen                   = true;
    entry.source.type              = UTHOME_SAMPLE_ENVIRONMENT;
    entry.source.stale_ms          = 500U;
    entry.header.status            = UTHOME_SAMPLE_OK;
    assert(0 == fanctrl_open(&fan, &output, &rule, 0U));
    entry.header.timestamp_ms             = 1000U;
    entry.value.environment.temperature_c = 31.0F;
    assert(0 == fanctrl_step(&fan, &entry, 1000U));
    assert(fan.relay.b_on);
    puts("DEMO: 31 C -> fan commanded ON");
    entry.header.timestamp_ms             = 2000U;
    entry.value.environment.temperature_c = 29.0F;
    assert(0 == fanctrl_step(&fan, &entry, 2000U));
    assert(fan.relay.b_on);
    entry.header.timestamp_ms             = 2001U;
    entry.value.environment.temperature_c = 28.0F;
    assert(0 == fanctrl_step(&fan, &entry, 2001U));
    assert(!fan.relay.b_on);
    puts("DEMO: 28 C -> fan commanded OFF");
    entry.header.timestamp_ms             = 3001U;
    entry.value.environment.temperature_c = 31.0F;
    assert(0 == fanctrl_step(&fan, &entry, 3001U));
    assert(fan.relay.b_on);
    assert(0 == fanctrl_step(&fan, &entry, 3501U));
    assert(!fan.relay.b_on);
    puts("DEMO: stale reading -> immediate OFF despite switching holdoff");
    entry.header.timestamp_ms = 4501U;
    assert(0 == fanctrl_step(&fan, &entry, 4501U));
    assert(fan.relay.b_on);
    entry.value.environment.temperature_c = NAN;
    assert(0 == fanctrl_step(&fan, &entry, 4502U));
    assert(!fan.relay.b_on);
    assert(0 == fanctrl_step(&fan, NULL, 4503U));
    entry.value.environment.temperature_c = 31.0F;
    entry.header.timestamp_ms             = 5503U;
    assert(0 == fanctrl_step(&fan, &entry, 5503U));
    assert(fan.relay.b_on);
    entry.header.status = UTHOME_SAMPLE_IO;
    assert(0 == fanctrl_step(&fan, &entry, 5504U));
    assert(!fan.relay.b_on);
    entry.header.status       = UTHOME_SAMPLE_OK;
    entry.header.timestamp_ms = 6504U;
    entry.source.source_index = 1U;
    assert(0 == fanctrl_step(&fan, &entry, 6504U));
    assert(!fan.relay.b_on);
    entry.source.source_index = 0U;
    entry.header.timestamp_ms = 9000U;
    assert(0 == fanctrl_step(&fan, &entry, 7504U));
    assert(!fan.relay.b_on);
    entry.header.timestamp_ms = 8504U;
    assert(0 == fanctrl_step(&fan, &entry, 8504U));
    assert(fan.relay.b_on);
    assert(-3 == fanctrl_step(&fan, &entry, 8000U));
    assert(!fan.relay.b_on);
    assert(0 == fanctrl_close(&fan));
    rulecalc_config_t invalid = rule;
    invalid.duty_permille     = 500U;
    const unsigned writes     = line.writes;
    assert(-1 == fanctrl_open(&fan, &output, &invalid, 9000U));
    assert(writes == line.writes);
}

/** @brief Prove the existing pool rejects a collision without stealing pins. */
static void ownership (void)
{
    pinpool_t      pool   = {0};
    const unsigned sensor = 1U;
    const unsigned output = 2U;
    pool.allowed          = UINT32_C(0x30);
    assert(0 == pinpool_claim(&pool, UINT32_C(0x10), &sensor));
    assert(-1 == pinpool_claim(&pool, UINT32_C(0x10), &output));
    assert(pool.p_owners[4] == &sensor);
    assert(0 == pinpool_claim(&pool, UINT32_C(0x20), &output));
    assert(-1 == pinpool_release(&pool, UINT32_C(0x10), &output));
    assert(0 == pinpool_release(&pool, UINT32_C(0x20), &output));
    assert(0 == pinpool_release(&pool, UINT32_C(0x10), &sensor));
}

/* End of file. */

