#include "uthome/runtime.h"

#include <string.h>

static uthome_runtime_device_t g_devices[UTHOME_RUNTIME_MAX_DEVICES];
static uthome_runtime_status_t g_status[UTHOME_RUNTIME_MAX_DEVICES];
static size_t g_device_count;

static int find_device(const char *id)
{
    if (id == NULL) {
        return -1;
    }

    for (size_t i = 0; i < g_device_count; ++i) {
        if (strcmp(g_devices[i].id, id) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static int valid_device(const uthome_runtime_device_t *device)
{
    return device != NULL
        && device->id[0] != '\0'
        && memchr(device->id, '\0', sizeof(device->id)) != NULL;
}

static size_t running_device_count(void)
{
    size_t running = 0;

    for (size_t i = 0; i < g_device_count; ++i) {
        if (g_status[i].state == UTHOME_RUNTIME_RUNNING) {
            ++running;
        }
    }

    return running;
}

int uthome_runtime_init(void)
{
    memset(g_devices, 0, sizeof(g_devices));
    memset(g_status, 0, sizeof(g_status));
    g_device_count = 0;
    return 0;
}

int uthome_runtime_register_device(
    const uthome_runtime_device_t *device)
{
    if (!valid_device(device)) {
        return -1;
    }

    if (g_device_count >= UTHOME_RUNTIME_MAX_DEVICES) {
        return -2;
    }

    if (find_device(device->id) >= 0) {
        return -3;
    }

    g_devices[g_device_count] = *device;
    g_status[g_device_count].state = device->enabled
        ? UTHOME_RUNTIME_READY
        : UTHOME_RUNTIME_STOPPED;
    ++g_device_count;

    return 0;
}

int uthome_runtime_start_device(const char *id)
{
    int index = find_device(id);

    if (index < 0) {
        return -1;
    }

    if (!g_devices[index].enabled) {
        return -2;
    }

    if (g_status[index].state == UTHOME_RUNTIME_RUNNING) {
        return 0;
    }

    if (running_device_count() >= UTHOME_RUNTIME_MAX_DEVICE_TASKS) {
        return -3;
    }

    g_status[index].state = UTHOME_RUNTIME_RUNNING;
    return 0;
}

int uthome_runtime_stop_device(const char *id)
{
    int index = find_device(id);

    if (index < 0) {
        return -1;
    }

    g_status[index].state = UTHOME_RUNTIME_STOPPED;
    return 0;
}

int uthome_runtime_update_device(
    const char *id,
    const uthome_runtime_device_t *device)
{
    int index = find_device(id);

    if (index < 0 || !valid_device(device) || strcmp(device->id, id) != 0) {
        return -1;
    }

    g_devices[index] = *device;

    if (!device->enabled) {
        g_status[index].state = UTHOME_RUNTIME_STOPPED;
    } else if (g_status[index].state == UTHOME_RUNTIME_STOPPED) {
        g_status[index].state = UTHOME_RUNTIME_READY;
    }

    return 0;
}

int uthome_runtime_get_status(
    const char *id,
    uthome_runtime_status_t *status)
{
    int index = find_device(id);

    if (index < 0 || status == NULL) {
        return -1;
    }

    *status = g_status[index];
    return 0;
}

size_t uthome_runtime_device_count(void)
{
    return g_device_count;
}