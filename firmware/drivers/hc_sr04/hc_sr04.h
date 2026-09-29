/**
 * @file hc_sr04.h
 * @brief Measure distance through a borrowed pulse-capture port.
 */
#ifndef UTHOME_HC_SR04_H
#define UTHOME_HC_SR04_H

#include "pulseio.h"

typedef struct
{
    uint32_t distance_mm;
    uint32_t echo_us;
} hc_sr04_value_t;

typedef struct
{
    pulseio_t * p_port;
    uint64_t    last_finish_ms;
    uint32_t    timeout_us;
    bool        b_started;
    bool        b_ready;
} hc_sr04_t;

/* Zero-initialize, then use one worker. Wait 62 ms after each completed
 * attempt. Configure timeout in [25000,30000] us. Failed open still requires
 * close. Measurements assume nominal sound speed; error values are always zero.
 */
int hc_sr04_open (hc_sr04_t *              p_sensor,
                  pulseio_t *              p_port,
                  const pulseio_config_t * p_config,
                  uint32_t                 timeout_us);
int hc_sr04_read (hc_sr04_t * p_sensor, hc_sr04_value_t * p_value);
int hc_sr04_close (hc_sr04_t * p_sensor);

#endif
/* End of file. */

