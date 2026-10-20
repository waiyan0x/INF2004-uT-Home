/**
 * @file disci2c.h
 * @brief Collect bounded I2C register observations without identifying models.
 */
#ifndef UTHOME_DISCI2C_H
#define UTHOME_DISCI2C_H
#include "i2cbus.h"
#define UTHOME_DISC_READS 8U

typedef int disci2c_open_t (const i2cbus_config_t * p_config,
                            i2cbus_t **             pp_bus);
typedef int disci2c_close_t (i2cbus_t * p_bus);
typedef int disci2c_clock_t (uint64_t * p_now);

typedef enum
{
    DISCI2C_COMPLETE   = 0,
    DISCI2C_BUSY       = 1,
    DISCI2C_FAULT      = 2,
    DISCI2C_INVALID    = 3,
    DISCI2C_STALE      = 4,
    DISCI2C_INCOMPLETE = 5
} disci2c_result_t;

typedef enum
{
    DISCI2C_RESPONDING  = 0,
    DISCI2C_NO_RESPONSE = 1,
    DISCI2C_TIMED_OUT   = 2,
    DISCI2C_IO_ERROR    = 3,
    DISCI2C_NOT_CHECKED = 4
} disci2c_observation_t;

typedef struct
{
    uint8_t address;
    uint8_t reg;
} disci2c_read_t;

typedef struct
{
    disci2c_read_t        read;
    disci2c_observation_t status;
    uint8_t               value;
} disci2c_record_t;

typedef struct
{
    disci2c_result_t status;
    uint64_t         started_ms;
    uint32_t         elapsed_ms;
    size_t           count;
    disci2c_record_t records[UTHOME_DISC_READS];
} disci2c_result_data_t;

typedef struct
{
    disci2c_open_t *  p_open;
    disci2c_close_t * p_close;
    disci2c_clock_t * p_now;
    i2cbus_t *        p_bus;
    bool              b_busy;
} disci2c_t;

/* Serialized lifecycle owner only, with all affected resources unowned. The
 * caller supplies compiled/reviewed reads, never untrusted register commands.
 * One record per address, one register byte written and one byte read. No
 * model IDs, sensor setup, retry, heap or callbacks into application workers.
 * open must publish any retained handle, even on failure. close must retain
 * ownership on failure. The caller must block activation while p_bus is held.
 * Both callbacks must be bounded and use the same clock as p_now/the bus.
 * Budget covers open and reads; mandatory bounded cleanup follows even when
 * expired. Each transfer gets at most 100 ms within the total 200 ms budget.
 */
int disci2c_run (disci2c_t *             p_context,
                 const i2cbus_config_t * p_config,
                 const disci2c_read_t *  p_reads,
                 size_t                  count,
                 disci2c_result_data_t * p_result);
/* Explicit recovery only; release retained ownership before any activation. */
int disci2c_release (disci2c_t * p_context);
#endif
/* End of file. */

