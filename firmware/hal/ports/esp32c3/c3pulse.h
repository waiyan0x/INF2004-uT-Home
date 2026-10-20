/**
 * @file c3pulse.h
 * @brief Borrow C3 hardware pulse capture pairs.
 */
#ifndef UTHOME_C3PULSE_H
#define UTHOME_C3PULSE_H
#include "pulseio.h"
/* Startup lifecycle owner calls once after runtime initialization, before
 * restore, configuration requests or workers. Success is idempotent; a failed
 * attempt cannot allocate again and requires reboot. Never call first during
 * normal operation. Open fails before successful startup. Close retains all
 * 2 mutexes for reuse. No pin/clock changes occur during startup. */
int c3pulse_startup (void);
/* Pairs 0..1 each reserve one RMT TX/RX channel. GPIO input must idle low.
 * Factory has no I/O. Open/close require the lifecycle owner and stopped users.
 * The whole RMT peripheral is owned by this adapter. No SDK RMT driver/ISR.
 * Capture is hardware timed; waiting yields at 1 ms kernel intervals. */
pulseio_t * c3pulse_port (uint8_t pair);
#endif
/* End of file. */

