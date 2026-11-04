/**
 * @file cfgplan.h
 * @brief Independently admit node schema 2.0.0 into copied target settings.
 */
#ifndef UTHOME_CFGPLAN_H
#define UTHOME_CFGPLAN_H
#include <stdbool.h>
#include "rulecalc.h"
#include <stddef.h>
#include <stdint.h>
#define UTHOME_PLAN_DEVICES 8U
#define UTHOME_PLAN_BUSES   3U
#define UTHOME_PLAN_QUEUES  4U

typedef enum
{
    CFGPLAN_I2C,
    CFGPLAN_SPI,
    CFGPLAN_UART,
    CFGPLAN_GPIO
} cfgplan_bus_kind_t;

typedef enum
{
    CFGPLAN_ENV,
    CFGPLAN_OLED,
    CFGPLAN_RADAR,
    CFGPLAN_DISTANCE,
    CFGPLAN_PWM,
    CFGPLAN_LIGHT,
    CFGPLAN_DRIVER_COUNT
} cfgplan_driver_t;

typedef struct
{
    char               name[32];
    cfgplan_bus_kind_t kind;
    uint32_t           port;
    uint32_t           pin_a;
    uint32_t           pin_b;
    uint32_t           clock;
    uint32_t           buffer_bytes;
    uint32_t           data_bits;
    uint32_t           stop_bits;
    uint32_t           parity;
} cfgplan_bus_t;

typedef struct
{
    char     name[32];
    uint32_t type;
    uint32_t depth;
} cfgplan_queue_t;

typedef struct
{
    char             name[32];
    char             task_name[32];
    cfgplan_driver_t driver;
    bool             b_enabled;
    uint32_t         bus;
    uint32_t         pin_a;
    uint32_t         pin_b;
    uint32_t         pin_c;
    uint32_t         address;
    uint32_t         frequency_hz;
    uint32_t         period_ms;
    uint32_t         timeout_us;
    uint32_t         destination;
    uint32_t         consumers;
    uint32_t         priority;
    uint32_t         stack_bytes;
} cfgplan_device_t;

typedef struct
{
    rulecalc_config_t config;
    uint8_t           output_index;
} cfgplan_rule_t;

typedef struct
{
    char             name[32];
    uint32_t         health_ms;
    uint32_t         max_tasks;
    uint32_t         max_depth;
    size_t           bus_count;
    cfgplan_bus_t    buses[UTHOME_PLAN_BUSES];
    size_t           queue_count;
    cfgplan_queue_t  queues[UTHOME_PLAN_QUEUES];
    size_t           device_count;
    cfgplan_device_t devices[UTHOME_PLAN_DEVICES];
    cfgplan_rule_t   rules[4];
    size_t           rule_count;
    uint32_t         active_tasks;
    uint32_t         stack_bytes;
} cfgplan_t;

/* cfgjson_init must have run once. Single control-task use. Mask comes from
 * trusted compiled binding code, never from the profile/host. Admission
 * performs no hardware calls. Output is caller-owned candidate scratch, cleared
 * on any rejection; never pass an active plan as scratch. Unknown fields,
 * descriptors, routes, resource conflicts and unsupported target pins/settings
 * are rejected.
 */
int cfgplan_read (const uint8_t * p_data,
                  size_t          size,
                  uint32_t        compiled_mask,
                  cfgplan_t *     p_candidate);
#endif
/* End of file. */

