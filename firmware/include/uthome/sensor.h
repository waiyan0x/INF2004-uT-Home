#ifndef UTHOME_SENSOR_H
#define UTHOME_SENSOR_H

#include <stdint.h>

#include "hal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sensor sample types (the planned sensor.h in include/uthome/README.md).
 *
 * environment_sample is the hardware-independent environmental reading. A
 * sensor driver fills pressure, temperature and status. The runtime task
 * stamps timestamp_ms from the monotonic clock when it publishes the sample,
 * so the driver leaves that field zero.
 *
 * This is a shared contract used by drivers, services and the display. The
 * field set is the team's to agree; kept minimal here to what the BMP388 path
 * needs. target_report and distance_sample (UART/GPIO sensors) belong here too.
 */
typedef struct {
    float        pressure_pa;    /* Compensated pressure in pascals */
    float        temperature_c;  /* Compensated temperature in degrees Celsius */
    uint32_t     timestamp_ms;   /* Monotonic time of the reading, set by the runtime */
    hal_status_t status;         /* HAL_OK when the reading is valid */
} environment_sample_t;

#ifdef __cplusplus
}
#endif

#endif /* UTHOME_SENSOR_H */
