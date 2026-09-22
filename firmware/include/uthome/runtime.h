#ifndef UTHOME_RUNTIME_H
#define UTHOME_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UTHOME_RUNTIME_MAX_DEVICES 8
#define UTHOME_RUNTIME_MAX_DEVICE_TASKS 4
#define UTHOME_DEVICE_ID_MAX 32

typedef enum {
    UTHOME_RUNTIME_STOPPED = 0,
    UTHOME_RUNTIME_READY,
    UTHOME_RUNTIME_RUNNING,
    UTHOME_RUNTIME_ERROR
} uthome_runtime_state_t;

typedef struct {
    char id[UTHOME_DEVICE_ID_MAX];
    uint32_t period_ms;
    uint8_t priority;
    size_t stack_size_bytes;
    bool enabled;
} uthome_runtime_device_t;

typedef struct {
    uthome_runtime_state_t state;
    uint32_t run_count;
    uint32_t failure_count;
    uint32_t queue_drop_count;
} uthome_runtime_status_t;

int uthome_runtime_init(void);

int uthome_runtime_register_device(
    const uthome_runtime_device_t *device
);

int uthome_runtime_start_device(const char *id);

int uthome_runtime_stop_device(const char *id);

int uthome_runtime_update_device(
    const char *id,
    const uthome_runtime_device_t *device
);

int uthome_runtime_get_status(
    const char *id,
    uthome_runtime_status_t *status
);

size_t uthome_runtime_device_count(void);

#endif