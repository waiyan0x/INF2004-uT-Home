/**
 * @file oledwork.h
 * @brief Fixed storage for the recurring output worker.
 */
#ifndef UTHOME_OLEDWORK_H
#define UTHOME_OLEDWORK_H

#include "oledinst.h"

typedef struct
{
    oledinst_t      instance;
    spibus_device_t device;
    uint8_t         sources;
} oledwork_t;

/* One display worker owns this zero-initialized storage. Binding opens the
 * device/control lines before use and closes them only after the worker stops.
 * The step copies one observation, renders into the existing frame and flushes
 * it with the driver's deadline. Contention renders unavailable data. */
int oledwork_step (oledwork_t * p_display);

#endif

/* End of file. */

