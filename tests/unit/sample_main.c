/**
 * @file sample_main.c
 * @brief Verify sample bounds and copied queue exchange between instances.
 */
#include "sample_main.h"

#include <assert.h>
#include <stdint.h>

#include "uthome/health.h"
#include "uthome/msgbus.h"
#include "uthome/sample.h"

#ifdef NDEBUG
#error "Sample contract checks require assertions."
#endif

static void check_sizes (void);
static void check_environment (void);
static void check_targets (void);
static void check_distance (void);
static void check_health (void);

/** @brief Exercise all shared sample formats through the actual message bus. */
int main (void)
{
    assert(UTHOME_MSGBUS_OK == msgbus_init());
    check_sizes();
    check_environment();
    check_targets();
    check_distance();
    check_health();
    return 0;
}

/** @brief Reject excessive sample padding even in a compile-only ABI check. */
static void check_sizes (void)
{
    /* sizeof makes each bound a C99 integer constant expression. An oversized
     * record produces a negative array bound, including during target builds.
     */
    char environment_fit[(sizeof(sample_environment_t) <=
                          UTHOME_MSGBUS_MAX_ITEM_SIZE)
                             ? 1
                             : -1];
    char distance_fit[(sizeof(sample_distance_t) <= UTHOME_MSGBUS_MAX_ITEM_SIZE)
                          ? 1
                          : -1];
    char targets_fit[(sizeof(sample_target_report_t) <=
                      UTHOME_MSGBUS_MAX_ITEM_SIZE)
                         ? 1
                         : -1];
    char health_fit[(sizeof(sample_health_t) <= UTHOME_MSGBUS_MAX_ITEM_SIZE)
                        ? 1
                        : -1];

    assert(1U == sizeof(environment_fit));
    assert(1U == sizeof(distance_fit));
    assert(1U == sizeof(targets_fit));
    assert(1U == sizeof(health_fit));
}

/** @brief Preserve source identity and full-width time in copied samples. */
static void check_environment (void)
{
    const msgbus_queue_config_t config  = {UTHOME_SAMPLE_ENVIRONMENT,
                                           sizeof(sample_environment_t),
                                           2U,
                                           UTHOME_MSGBUS_DROP_OLDEST};
    msgbus_queue_id_t           h_queue = -1;
    sample_environment_t        input   = {0};
    sample_environment_t        output  = {0};

    assert(UTHOME_SAMPLE_UNKNOWN == input.header.status);
    assert(UTHOME_MSGBUS_OK == msgbus_create_queue(&config, &h_queue));
    input.header.timestamp_ms = UINT64_C(4294967296);
    input.header.sequence     = 7U;
    input.header.source_index = 0U;
    input.header.status       = UTHOME_SAMPLE_OK;
    input.pressure_pa         = 101325.0F;
    input.temperature_c       = -5.0F;
    assert(UTHOME_MSGBUS_OK ==
           msgbus_publish(
               h_queue, UTHOME_SAMPLE_ENVIRONMENT, &input, sizeof(input)));
    input.header.source_index = 1U;
    input.header.sequence     = 2U;
    input.temperature_c       = 25.0F;
    assert(UTHOME_MSGBUS_OK ==
           msgbus_publish(
               h_queue, UTHOME_SAMPLE_ENVIRONMENT, &input, sizeof(input)));
    input.temperature_c = 99.0F;
    assert(UTHOME_MSGBUS_OK ==
           msgbus_consume(
               h_queue, UTHOME_SAMPLE_ENVIRONMENT, &output, sizeof(output)));
    assert(0U == output.header.source_index);
    assert(7U == output.header.sequence);
    assert(UINT64_C(4294967296) == output.header.timestamp_ms);
    assert((-5.0F) == output.temperature_c);
    assert(UTHOME_MSGBUS_OK ==
           msgbus_consume(
               h_queue, UTHOME_SAMPLE_ENVIRONMENT, &output, sizeof(output)));
    assert(1U == output.header.source_index);
    assert(2U == output.header.sequence);
    assert(25.0F == output.temperature_c);
}

/** @brief Copy the full three-target report and reject a mismatched type. */
static void check_targets (void)
{
    const msgbus_queue_config_t config  = {UTHOME_SAMPLE_TARGETS,
                                           sizeof(sample_target_report_t),
                                           1U,
                                           UTHOME_MSGBUS_DROP_OLDEST};
    msgbus_queue_id_t           h_queue = -1;
    sample_target_report_t      input   = {0};
    sample_target_report_t      output  = {0};

    assert(UTHOME_MSGBUS_OK == msgbus_create_queue(&config, &h_queue));
    input.header.status            = UTHOME_SAMPLE_OK;
    input.header.source_index      = 2U;
    input.targets[2].b_present     = true;
    input.targets[2].x_mm          = -120;
    input.targets[2].y_mm          = 340;
    input.targets[2].speed_cm_s    = -8;
    input.targets[2].resolution_mm = 10U;
    assert(
        UTHOME_MSGBUS_ERR_TYPE ==
        msgbus_publish(h_queue, UTHOME_SAMPLE_DISTANCE, &input, sizeof(input)));
    assert(
        UTHOME_MSGBUS_OK ==
        msgbus_publish(h_queue, UTHOME_SAMPLE_TARGETS, &input, sizeof(input)));
    assert(UTHOME_MSGBUS_OK ==
           msgbus_consume(
               h_queue, UTHOME_SAMPLE_TARGETS, &output, sizeof(output)));
    assert(output.targets[2].b_present);
    assert(!output.targets[0].b_present);
    assert((-120) == output.targets[2].x_mm);
    assert(340 == output.targets[2].y_mm);
    assert((-8) == output.targets[2].speed_cm_s);
    assert(10U == output.targets[2].resolution_mm);
}

/** @brief Distinguish timeout from a successful zero-distance read. */
static void check_distance (void)
{
    const msgbus_queue_config_t config  = {UTHOME_SAMPLE_DISTANCE,
                                           sizeof(sample_distance_t),
                                           1U,
                                           UTHOME_MSGBUS_DROP_OLDEST};
    msgbus_queue_id_t           h_queue = -1;
    sample_distance_t           input   = {0};
    sample_distance_t           output  = {0};

    assert(UTHOME_MSGBUS_OK == msgbus_create_queue(&config, &h_queue));
    input.header.status       = UTHOME_SAMPLE_TIMEOUT;
    input.header.source_index = 3U;
    assert(
        UTHOME_MSGBUS_OK ==
        msgbus_publish(h_queue, UTHOME_SAMPLE_DISTANCE, &input, sizeof(input)));
    assert(UTHOME_MSGBUS_OK ==
           msgbus_consume(
               h_queue, UTHOME_SAMPLE_DISTANCE, &output, sizeof(output)));
    assert(UTHOME_SAMPLE_TIMEOUT == output.header.status);
    assert(0U == output.distance_mm);
    input.header.status = UTHOME_SAMPLE_OK;
    input.distance_mm   = 123U;
    input.echo_us       = 720U;
    assert(
        UTHOME_MSGBUS_OK ==
        msgbus_publish(h_queue, UTHOME_SAMPLE_DISTANCE, &input, sizeof(input)));
    assert(UTHOME_MSGBUS_OK ==
           msgbus_consume(
               h_queue, UTHOME_SAMPLE_DISTANCE, &output, sizeof(output)));
    assert(UTHOME_SAMPLE_OK == output.header.status);
    assert(123U == output.distance_mm);
    assert(720U == output.echo_us);
}

/** @brief Represent a node-level health event without a device identity. */
static void check_health (void)
{
    const msgbus_queue_config_t config  = {UTHOME_SAMPLE_HEALTH,
                                           sizeof(sample_health_t),
                                           1U,
                                           UTHOME_MSGBUS_DROP_OLDEST};
    msgbus_queue_id_t           h_queue = -1;
    sample_health_t             input   = {0};
    sample_health_t             output  = {0};

    assert(UTHOME_MSGBUS_OK == msgbus_create_queue(&config, &h_queue));
    input.header.source_index = UTHOME_SAMPLE_NO_SOURCE;
    input.header.status       = UTHOME_SAMPLE_OK;
    input.event               = UTHOME_HEALTH_QUEUE_OVERFLOW;
    input.occurrences         = 12U;
    assert(
        UTHOME_MSGBUS_OK ==
        msgbus_publish(h_queue, UTHOME_SAMPLE_HEALTH, &input, sizeof(input)));
    assert(
        UTHOME_MSGBUS_OK ==
        msgbus_consume(h_queue, UTHOME_SAMPLE_HEALTH, &output, sizeof(output)));
    assert(UTHOME_SAMPLE_NO_SOURCE == output.header.source_index);
    assert(UTHOME_HEALTH_QUEUE_OVERFLOW == output.event);
    assert(12U == output.occurrences);
}

/* End of file. */

