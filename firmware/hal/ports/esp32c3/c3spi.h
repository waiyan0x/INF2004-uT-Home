/**
 * @file c3spi.h
 * @brief Use SPI2 FIFO transfers with a retained bus mutex and configurable
 * pins.
 */
#ifndef UTHOME_C3SPI_H
#define UTHOME_C3SPI_H
#include "spibus.h"

typedef struct
{
    uint8_t host;
    uint8_t sclk_gpio;
    uint8_t mosi_gpio;
    uint8_t miso_gpio;
} c3spi_config_t;

/* Lifecycle owner opens before workers, closes after all workers/devices stop.
 * host is profile number 2, not the SDK enum. MISO may be UTHOME_SPI_NO_PIN.
 * MSB first, active-low CS, modes 0..3, 100 kHz..8 MHz. RX is limited to 1 MHz
 * until board timing qualification; quantized clocks may not exceed requested.
 * No DMA, interrupts, flash SPI, automatic probing or physical presence test.
 */
int c3spi_open (const c3spi_config_t * p_config, spibus_t ** pp_bus);
int c3spi_close (spibus_t * p_bus);
#endif
/* End of file. */

