#!/usr/bin/env python3
"""Test notification semantics extracted from the actually applied shim patch."""

from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
VENDOR_SHIM = (
    ROOT
    / "firmware/third_party/esp32c3-mtk3/components/freertos_shim/shim_task.c"
)
PATCH = ROOT / "tools/target/patches/freertos-task-observer.patch"


def extract_function(source, name):
    pattern = (
        r"\b(?:static\s+)?[A-Za-z_][\w\s*]*\b"
        + re.escape(name)
        + r"\s*\([^;{}]*\)\s*\{"
    )
    found = re.search(pattern, source)
    if found is None:
        raise RuntimeError(f"Missing function in patched source: {name}")
    depth = 0
    for index in range(found.end() - 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[found.start(): index + 1]
    raise RuntimeError(f"Unterminated function in patched source: {name}")


HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int ID;
typedef int ER;
typedef int BaseType_t;
typedef int TMO;
typedef unsigned UINT;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef void * TaskHandle_t;
typedef uint32_t eNotifyAction;
typedef int shim_tag_t;

typedef struct
{
    shim_tag_t tag;
} shim_hdr_t;

typedef struct
{
    shim_hdr_t        hdr;
    ID                tsk_id;
    ID                notify_sem;
    volatile uint32_t notify_val;
    void *            stack;
    uint32_t          stack_sz;
    char              name[12];
    void *            tls[1];
} shim_task_t;

#define SHIM_TASK_MAX                 4
#define SHIM_TASK_RESERVED            ((ID)-1)
#define SHIM_FLAG_CLEANUP_BUSY        0x01U
#define SHIM_FLAG_OBSERVED            0x02U
#define SHIM_FLAG_SELF_EXIT           0x04U
#define SHIM_FLAG_NOTIFY_PENDING      0x08U
#define pdFALSE                       0
#define pdTRUE                        1
#define E_OK                          0
#define E_TMOUT                       (-50)
#define E_QOVR                        (-43)
#define portMAX_DELAY                 UINT32_MAX
#define eNoAction                     0U
#define eSetBits                      1U
#define eIncrement                    2U
#define eSetValueWithOverwrite        3U
#define eSetValueWithoutOverwrite     4U

static pthread_mutex_t g_guard = PTHREAD_MUTEX_INITIALIZER;
#define DI(saved)                                                             \
    do                                                                        \
    {                                                                         \
        (saved) = 0U;                                                         \
        assert(0 == pthread_mutex_lock(&g_guard));                             \
    } while (0)
#define EI(saved)                                                             \
    do                                                                        \
    {                                                                         \
        (void)(saved);                                                        \
        assert(0 == pthread_mutex_unlock(&g_guard));                           \
    } while (0)

@@STATE_ENUM@@

static shim_task_t       s_tasks[SHIM_TASK_MAX];
static uint8_t           s_task_states[SHIM_TASK_MAX];
static uint8_t           s_task_flags[SHIM_TASK_MAX];
static uintptr_t         s_handles[SHIM_TASK_MAX];
static uint32_t          g_tick;
static uint8_t           g_sem_count;
static ID                g_current_task = 7;

static ID tk_get_tid (void)
{
    return g_current_task;
}

static ER tk_sig_sem (ID semaphore, int count)
{
    assert((3 == semaphore) && (1 == count));
    if (0U != g_sem_count)
    {
        return E_QOVR;
    }
    g_sem_count = 1U;
    return E_OK;
}

static ER tk_wai_sem (ID semaphore, int count, TMO timeout)
{
    assert((3 == semaphore) && (1 == count));
    if (0U != g_sem_count)
    {
        g_sem_count = 0U;
        return E_OK;
    }
    if (0 < timeout)
    {
        g_tick += (uint32_t)timeout;
    }
    return E_TMOUT;
}

static TMO shim_tmo (TickType_t ticks)
{
    return (TMO)ticks;
}

static TickType_t __wrap_xTaskGetTickCount (void)
{
    return g_tick;
}

@@FUNCTIONS@@

static TaskHandle_t active_handle (void)
{
    return (TaskHandle_t)(uintptr_t)5U;
}

static void reset_notification (void)
{
    s_tasks[0].notify_val = 0U;
    s_task_flags[0] &= (uint8_t)~SHIM_FLAG_NOTIFY_PENDING;
    g_sem_count           = 0U;
    g_tick                = 0U;
}

int main (void)
{
    s_task_states[0]       = SHIM_TASK_RUNNING;
    s_tasks[0].tsk_id      = g_current_task;
    s_tasks[0].notify_sem  = 3;
    s_handles[0]           = (uintptr_t)active_handle();
    TaskHandle_t handle    = active_handle();
    uint32_t     value     = 0U;
    uint32_t     previous  = 0U;
    BaseType_t   woken     = pdTRUE;

    assert(pdTRUE == __wrap_xTaskNotify(handle, 0x03U, eSetBits));
    assert(pdTRUE == __wrap_xTaskNotify(handle, 0x0CU, eSetBits));
    assert(pdTRUE == __wrap_xTaskNotifyWait(0U, 0x05U, &value, 0U));
    assert(0x0FU == value);
    assert(0x0AU == s_tasks[0].notify_val);
    assert(pdFALSE == __wrap_xTaskNotifyWait(0U, 0U, &value, 5U));
    assert(5U == g_tick);

    reset_notification();
    s_tasks[0].notify_val = 0xFFU;
    assert(pdFALSE == __wrap_xTaskNotifyWait(0x0FU, 0U, &value, 0U));
    assert((0xF0U == value) && (0xF0U == s_tasks[0].notify_val));

    reset_notification();
    assert(pdTRUE == __wrap_xTaskNotifyGive(handle));
    assert(pdTRUE == __wrap_xTaskNotifyGive(handle));
    __wrap_xTaskNotifyGiveFromISR(handle, &woken);
    assert(pdFALSE == woken);
    assert(3U == __wrap_ulTaskNotifyTake(pdFALSE, 0U));
    assert(2U == __wrap_ulTaskGenericNotifyTake(0U, pdFALSE, 0U));
    assert(1U == __wrap_ulTaskNotifyTake(pdTRUE, 0U));
    assert(0U == __wrap_ulTaskNotifyTake(pdTRUE, 0U));

    reset_notification();
    assert(pdTRUE == __wrap_xTaskGenericNotify(handle,
                                               0U,
                                               7U,
                                               eSetValueWithOverwrite,
                                               &previous));
    assert(0U == previous);
    previous = 99U;
    assert(pdFALSE == __wrap_xTaskGenericNotify(handle,
                                                0U,
                                                9U,
                                                eSetValueWithoutOverwrite,
                                                &previous));
    assert((7U == previous) && (7U == s_tasks[0].notify_val));
    assert(pdTRUE == __wrap_xTaskNotifyWait(0U, UINT32_MAX, &value, 0U));
    assert((7U == value) && (0U == s_tasks[0].notify_val));
    assert(pdTRUE == __wrap_xTaskGenericNotify(handle,
                                               0U,
                                               9U,
                                               eSetValueWithoutOverwrite,
                                               NULL));

    assert(pdTRUE == __wrap_xTaskGenericNotifyStateClear(handle, 0U));
    assert(pdFALSE == __wrap_xTaskGenericNotifyStateClear(handle, 0U));
    assert(9U == __wrap_ulTaskGenericNotifyValueClear(handle, 0U, 1U));
    assert(8U == s_tasks[0].notify_val);
    assert(pdTRUE == __wrap_xTaskNotify(handle, 0U, eNoAction));
    assert(pdTRUE == __wrap_xTaskGenericNotifyStateClear(NULL, 0U));

    assert(pdFALSE == __wrap_xTaskGenericNotify(handle,
                                                1U,
                                                1U,
                                                eIncrement,
                                                NULL));
    assert(0U == __wrap_ulTaskGenericNotifyTake(1U, pdTRUE, 0U));
    assert(pdFALSE == __wrap_xTaskNotify((TaskHandle_t)(uintptr_t)99U,
                                        1U,
                                        eSetBits));
    assert(pdFALSE == __wrap_xTaskNotify(handle, 1U, 99U));

    s_task_states[0] = SHIM_TASK_FREE;
    assert(pdFALSE == __wrap_xTaskNotify(handle, 1U, eSetBits));
    assert(0U == __wrap_ulTaskNotifyTake(pdTRUE, 0U));

    puts("PASS: applied shim notifications: bits, increment, overwrite,");
    puts("      no-overwrite, clear, pending, timeout, ISR and stale handles");
    return 0;
}
"""


def main():
    with tempfile.TemporaryDirectory(prefix="buddy1_notify_actual_") as folder:
        directory = Path(folder)
        staged = directory / "components/freertos_shim/shim_task.c"
        staged.parent.mkdir(parents=True)
        shutil.copyfile(VENDOR_SHIM, staged)
        subprocess.run(["git", "apply", "--check", str(PATCH)],
                       cwd=directory, check=True)
        subprocess.run(["git", "apply", str(PATCH)], cwd=directory, check=True)
        source = staged.read_text(encoding="utf-8")
        enum_match = re.search(
            r"typedef\s+enum\s*\{.*?\}\s*shim_task_state_t\s*;",
            source,
            re.DOTALL,
        )
        if enum_match is None:
            raise RuntimeError("Applied shim lifecycle state is missing")

        names = (
            "find_index_by_tk_id",
            "find_index_by_handle",
            "notify_current_index",
            "notify_update",
            "notify_remaining",
            "notify_wait",
            "__wrap_xTaskNotifyGive",
            "__wrap_xTaskNotifyGiveFromISR",
            "__wrap_ulTaskNotifyTake",
            "__wrap_xTaskNotify",
            "__wrap_xTaskNotifyFromISR",
            "__wrap_xTaskNotifyWait",
            "__wrap_ulTaskGenericNotifyTake",
            "__wrap_xTaskGenericNotify",
            "__wrap_xTaskGenericNotifyFromISR",
            "__wrap_vTaskGenericNotifyGiveFromISR",
            "__wrap_xTaskGenericNotifyWait",
            "__wrap_xTaskGenericNotifyStateClear",
            "__wrap_ulTaskGenericNotifyValueClear",
        )
        functions = "\n\n".join(extract_function(source, name) for name in names)
        program = HARNESS.replace("@@STATE_ENUM@@", enum_match.group(0))
        program = program.replace("@@FUNCTIONS@@", functions)
        source_path = directory / "test_notifications.c"
        binary = directory / "test_notifications"
        source_path.write_text(program, encoding="utf-8")
        subprocess.run(
            [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fno-omit-frame-pointer",
                "-fsanitize=address,undefined",
                "-pthread",
                str(source_path),
                "-o",
                str(binary),
            ],
            check=True,
        )
        environment = os.environ.copy()
        environment["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1"
        subprocess.run([str(binary)], check=True, env=environment)


if __name__ == "__main__":
    main()
