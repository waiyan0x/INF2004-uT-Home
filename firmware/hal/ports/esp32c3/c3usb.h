/**
 * @file c3usb.h
 * @brief Exclusive nonblocking native USB configuration channel.
 */
#ifndef UTHOME_C3USB_H
#define UTHOME_C3USB_H
#include "cfgpump.h"
/* Owner task only, after boot console output. No installed USB driver or other
 * readers/writers may share this endpoint. No remux, reset or power management.
 * Replies are <=48 bytes; each short write is flushed as a USB packet. */
int c3usb_open (void);
const cfgpump_ops_t * c3usb_operations (void);
#endif
/* End of file. */

