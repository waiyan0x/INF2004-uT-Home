/* Shared host-sim helpers. */

#include "host_sim.h"

/* Defined in the per-protocol fakes. */
void host_sim_reset_i2c(void);
void host_sim_reset_spi(void);

void host_sim_reset(void)
{
    host_sim_reset_i2c();
    host_sim_reset_spi();
}
