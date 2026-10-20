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

/* Call once during startup, before profile restore or any request. It creates
 * the only kernel object, one permanent mutex. Later calls create nothing;
 * after a failed creation, init/open keep failing until reboot.
 */
int c3spi_init (void);
/* The lifecycle owner opens/closes sessions; neither creates nor deletes a
 * kernel object. Close rejects attached devices and an in-flight transfer; a
 * failed pin release retains the session, rejecting open, attach and transfers
 * until close is retried successfully. The bus callbacks stay valid while
 * closed and reject use; open refuses a stray attached device. Device
 * attach/detach needs an open session but no owner check, because worker
 * cleanup detaches. host is profile number 2, not the SDK enum. MISO may be
 * UTHOME_SPI_NO_PIN. MSB first, active-low CS, modes 0..3, 100 kHz..8 MHz. RX
 * is limited to 1 MHz until board timing qualification; quantized clocks may
 * not exceed requested. No DMA, interrupts, flash SPI, automatic probing or
 * physical presence test.
 */
int c3spi_open (const c3spi_config_t * p_config, spibus_t ** pp_bus);
int c3spi_close (spibus_t * p_bus);
#endif
/* End of file. */

