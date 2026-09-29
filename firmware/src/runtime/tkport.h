/**
 * @file tkport.h
 * @brief Select the kernel implementation of the shared runtime-port module.
 * @details Link exactly one backend. Define UTHOME_RUNTIME_UTKERNEL only when
 * building against the pinned kernel; the native guard rejects every call.
 */
#ifndef UTHOME_TKPORT_H
#define UTHOME_TKPORT_H
#include "rt_port.h"
#include "workport.h"
#endif

/* End of file. */

