/**
 * @file hlthbus.h
 * @brief Report newly observed message-queue drops.
 */
#ifndef UTHOME_HLTHBUS_H
#define UTHOME_HLTHBUS_H
#include "uthome/health.h"
#include "uthome/msgbus.h"

/*
 * Observe a message queue and report newly
 * detected queue-overflow events.
 */
int hlthbus_check_queue (msgbus_queue_id_t h_queue_id,
                         uint32_t          now_ms,
                         uint32_t *        p_previous_drop_count);

#endif

/* End of file. */

