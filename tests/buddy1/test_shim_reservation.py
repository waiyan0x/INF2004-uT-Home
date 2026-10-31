#!/usr/bin/env python3
"""Host concurrency smoke test using reservation helpers from the actual staged shim.

Run from the repository root. Applies the observer patch in a temporary source
copy, extracts the real helper functions, and exercises them with pthreads.
The test does not boot ESP32-C3 or validate all xTaskCreate failure paths.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import textwrap

ROOT = Path.cwd()
SHIM = ROOT / 'firmware/third_party/esp32c3-mtk3/components/freertos_shim/shim_task.c'
PATCH = ROOT / 'tools/target/patches/freertos-task-observer.patch'

def extract_function(source, name):
    match = re.search(r'\bstatic\s+[^;{}\n]+\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', source)
    if not match:
        raise RuntimeError(f'Could not find helper {name} in patched shim')
    start = match.start()
    depth = 0
    for index in range(match.end() - 1, len(source)):
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise RuntimeError(f'Incomplete helper {name}')

def run_command(args, cwd=None):
    subprocess.run(args, cwd=cwd, check=True)

if not SHIM.is_file() or not PATCH.is_file():
    raise SystemExit('Run from repo root; vendor shim or observer patch missing')

with tempfile.TemporaryDirectory(prefix='buddy1_reservation_') as folder:
    workspace = Path(folder)
    stage = workspace / 'components/freertos_shim'
    stage.mkdir(parents=True)
    staged_file = stage / 'shim_task.c'
    staged_file.write_bytes(SHIM.read_bytes())
    # Only apply to the temporary copy. Never change the pinned vendor tree.
    run_command(['git', '-c', 'core.autocrlf=true', 'apply', '--check', str(PATCH)], cwd=workspace)
    run_command(['git', '-c', 'core.autocrlf=true', 'apply', str(PATCH)], cwd=workspace)
    source = staged_file.read_text()
    if not re.search(r'#define\s+SHIM_TASK_RESERVED\s+\(\(ID\)-1\)', source):
        raise SystemExit('Missing explicit reserved-slot sentinel')
    state = re.search(
        r'typedef\s+enum\s*\{.*?\}\s*shim_task_state_t\s*;',
        source,
        re.DOTALL,
    )
    if state is None:
        raise RuntimeError('Missing lifecycle state in patched shim')
    helpers = '\n\n'.join(
        extract_function(source, name)
        for name in (
            'make_handle',
            'clear_task_entry',
            'alloc_task_entry',
            'release_unstarted_entry',
        )
    )
    program = r'''
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int ID;
typedef unsigned int UINT;
typedef struct { ID tsk_id; } shim_task_t;
#define SHIM_TASK_MAX 4
#define SHIM_TASK_RESERVED ((ID)-1)
#define SHIM_FLAG_CLEANUP_BUSY 0x01U
#define SHIM_FLAG_OBSERVED 0x02U
#define SHIM_FLAG_SELF_EXIT 0x04U
#define SHIM_FLAG_NOTIFY_PENDING 0x08U
@@STATE@@
static shim_task_t s_tasks[SHIM_TASK_MAX];
static uint8_t s_task_states[SHIM_TASK_MAX];
static uint8_t s_task_flags[SHIM_TASK_MAX];
static uintptr_t s_handles[SHIM_TASK_MAX];
static uint32_t s_generations[SHIM_TASK_MAX];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
#define DI(saved) do { (saved) = 0; assert(pthread_mutex_lock(&g_lock) == 0); } while (0)
#define EI(saved) do { (void)(saved); assert(pthread_mutex_unlock(&g_lock) == 0); } while (0)

/* Exact helper implementations extracted from the patched vendor shim. */
@@HELPERS@@

static shim_task_t *g_allocations[SHIM_TASK_MAX];
static int g_indices[SHIM_TASK_MAX];
static uintptr_t g_handles[SHIM_TASK_MAX];
static void *worker(void *arg)
{
    uintptr_t index = (uintptr_t)arg;
    g_indices[index] = -1;
    g_handles[index] = 0U;
    g_allocations[index] = alloc_task_entry(&g_indices[index],
                                             &g_handles[index]);
    return NULL;
}

int main(void)
{
    for (int round = 0; round < 100; ++round) {
        pthread_t threads[SHIM_TASK_MAX];
        memset(g_allocations, 0, sizeof(g_allocations));
        for (uintptr_t i = 0; i < SHIM_TASK_MAX; ++i)
            assert(pthread_create(&threads[i], NULL, worker, (void *)i) == 0);
        for (int i = 0; i < SHIM_TASK_MAX; ++i)
            assert(pthread_join(threads[i], NULL) == 0);
        for (int i = 0; i < SHIM_TASK_MAX; ++i) {
            assert(g_allocations[i] != NULL);
            assert(g_allocations[i]->tsk_id == SHIM_TASK_RESERVED);
            assert(g_indices[i] >= 0 && g_indices[i] < SHIM_TASK_MAX);
            assert(g_handles[i] != 0U);
            for (int j = i + 1; j < SHIM_TASK_MAX; ++j)
            {
                assert(g_allocations[i] != g_allocations[j]);
                assert(g_handles[i] != g_handles[j]);
            }
        }
        int rejected_index = -1;
        uintptr_t rejected_handle = 0U;
        assert(alloc_task_entry(&rejected_index, &rejected_handle) == NULL);
        for (int i = 0; i < SHIM_TASK_MAX; ++i)
            release_unstarted_entry(g_indices[i]);
        for (int i = 0; i < SHIM_TASK_MAX; ++i)
        {
            assert(s_tasks[i].tsk_id == 0);
            assert(s_task_states[i] == SHIM_TASK_FREE);
        }
    }
    puts("PASS: 4 distinct generation reservations, exhaustion, cleanup, 100 rounds");
    return 0;
}
'''
    code = program.replace('@@STATE@@', state.group(0))
    code = code.replace('@@HELPERS@@', helpers)
    test_file = workspace / 'reservation_test.c'
    exe = workspace / 'reservation_test'
    test_file.write_text(code)
    run_command(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread', str(test_file), '-o', str(exe)])
    run_command([str(exe)])
print('NOTE: Host helper test only. ESP32-C3 runtime and xTaskCreate failure injection remain unverified.')
