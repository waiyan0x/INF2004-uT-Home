#!/usr/bin/env python3
"""Compile actual patched shim creation/deletion functions against fault-injected host mocks.

Run from repository root. Does not modify pinned vendor source or flash hardware.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2] if (Path(__file__).resolve().parents[2] / 'firmware').exists() else Path.cwd()
SHIM = ROOT / 'firmware/third_party/esp32c3-mtk3/components/freertos_shim/shim_task.c'
PATCH = ROOT / 'tools/target/patches/freertos-task-observer.patch'


def extract(source, name):
    found = re.search(r'\b(?:static\s+)?[A-Za-z_][\w\s*]*\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', source)
    if found is None:
        raise RuntimeError(f'Missing function in patched source: {name}')
    depth = 0
    for i in range(found.end() - 1, len(source)):
        if source[i] == '{':
            depth += 1
        elif source[i] == '}':
            depth -= 1
            if depth == 0:
                return source[found.start():i + 1]
    raise RuntimeError(f'Unterminated function: {name}')


HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
typedef int ID;
typedef int ER;
typedef int INT;
typedef int SZ;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef unsigned UINT;
typedef unsigned char UB;
typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;
typedef void (*FP)(void *);
typedef int shim_tag_t;
typedef struct { shim_tag_t tag; } shim_hdr_t;
typedef struct {
    shim_hdr_t hdr;
    ID tsk_id;
    ID notify_sem;
    volatile uint32_t notify_val;
    void *stack;
    uint32_t stack_sz;
    char name[12];
    void *tls[1];
} shim_task_t;
typedef struct { int sematr, isemcnt, maxsem; } T_CSEM;
typedef struct { void *exinf; int tskatr; FP task; int itskpri; SZ stksz; void *bufptr; } T_CTSK;
#define SHIM_TASK_MAX 48
#define SHIM_TASK_RESERVED ((ID)-1)
#define pdFALSE 0
#define pdTRUE 1
#define E_OK 0
#define TA_TFIFO 1
#define TA_HLNG 2
#define TA_RNG0 4
#define TA_USERBUF 8
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_INTERNAL 2
#define DI(saved) do { (saved) = 0; } while (0)
#define EI(saved) do { (void)(saved); } while (0)
static shim_task_t s_tasks[SHIM_TASK_MAX];
static int g_fail, g_sems, g_stacks, g_tasks, g_abort, g_cancel, g_commit, g_release;
static int g_last_stack_sz;
static void tm_printf(const UB *fmt, ...) { (void)fmt; }
static void esp_rom_printf(const char *fmt, ...) { (void)fmt; }
static unsigned heap_caps_get_free_size(int flags) { (void)flags; return 100000U; }
static unsigned heap_caps_get_largest_free_block(int flags) { (void)flags; return 100000U; }
static int shim_prio_to_tk(uint32_t p) { return (int)p; }
static uint32_t c3task_observer_begin(const char *name, uint32_t size) { (void)name; (void)size; return 1; }
static int c3task_observer_abort(uint32_t token) { (void)token; ++g_abort; return 0; }
static int c3task_observer_commit(uint32_t token, void *h) { (void)token; assert(h); ++g_commit; return 0; }
static int c3task_observer_cancel(void *h) { assert(h); ++g_cancel; return 0; }
static int c3task_observer_release(void *h) { assert(h); ++g_release; return 0; }
static ID tk_cre_sem(T_CSEM *s) { (void)s; if (g_fail == 1) return -1; ++g_sems; return 3; }
static ER tk_del_sem(ID id) { assert(id == 3); assert(g_sems > 0); --g_sems; return E_OK; }
static void *heap_caps_malloc(SZ sz, int flags) {
    (void)flags;
    if (g_fail == 2) return NULL;
    void *p = malloc((size_t)sz);
    assert(p); ++g_stacks; g_last_stack_sz = sz;
    return p;
}
static void heap_caps_free(void *p) { assert(p && g_stacks > 0); free(p); --g_stacks; }
static ID tk_cre_tsk(T_CTSK *t) {
    assert(t->stksz == g_last_stack_sz);
    if (g_fail == 3) return -1;
    ++g_tasks; return 9;
}
static ER tk_sta_tsk(ID id, INT param) { (void)param; assert(id == 9); return g_fail == 4 ? -1 : E_OK; }
static ER tk_del_tsk(ID id) { assert(id == 9 && g_tasks > 0); --g_tasks; return E_OK; }
static ER tk_ter_tsk(ID id) { assert(id == 9); return E_OK; }
static ID tk_get_tid(void) { return 0; }
static ER tk_ext_tsk(void) { return E_OK; }
static void dummy(void *param) { (void)param; }
@@FUNCTIONS@@
static void check_clean(void) {
    assert(g_sems == 0 && g_stacks == 0 && g_tasks == 0);
    for (int i = 0; i < SHIM_TASK_MAX; ++i) assert(s_tasks[i].tsk_id == 0);
}
int main(void) {
    for (int round = 0; round < 30; ++round) {
        for (int failure = 1; failure <= 4; ++failure) {
            g_fail = failure;
            int abort_before = g_abort, cancel_before = g_cancel;
            int commit_before = g_commit;
            TaskHandle_t h = (void *)(uintptr_t)0x1234;
            assert(__wrap_xTaskCreatePinnedToCore(dummy, "WiFi", 4096, NULL, 3, &h, 0) == pdFALSE);
            assert(h == (void *)(uintptr_t)0x1234);
            assert(g_abort == abort_before + (failure <= 3));
            assert(g_commit == commit_before + (failure == 4));
            assert(g_cancel == cancel_before + (failure == 4));
            check_clean();
        }
        g_fail = 0;
        TaskHandle_t h = NULL;
        assert(__wrap_xTaskCreatePinnedToCore(dummy, "WiFi", 4096, NULL, 3, &h, 0) == pdTRUE);
        assert(h != NULL && ((shim_task_t *)h)->tsk_id == 9);
        assert(g_last_stack_sz == 6144);
        __wrap_vTaskDelete(h);
        check_clean();
    }
    assert(g_release == 30);
    puts("PASS: 4 injected creation failures + successful create/delete, 30 rounds");
    puts("NOTE: Host mocks only; self-deletion, concurrent full wrapper, board and static stacks unverified.");
}
'''


def main():
    if not SHIM.is_file() or not PATCH.is_file():
        raise SystemExit('Run from the repository root; shim or patch missing')
    with tempfile.TemporaryDirectory(prefix='buddy1_failures_') as tmp:
        directory = Path(tmp)
        staged = directory / 'components/freertos_shim/shim_task.c'
        staged.parent.mkdir(parents=True)
        staged.write_bytes(SHIM.read_bytes())
        for suffix in ('--check', None):
            args = ['git', '-c', 'core.autocrlf=true', 'apply']
            if suffix:
                args.append(suffix)
            subprocess.run(args + [str(PATCH)], cwd=directory, check=True)
        source = staged.read_text()
        functions = '\n\n'.join(extract(source, name) for name in (
            'alloc_task_entry', 'free_task_entry',
            '__wrap_xTaskCreatePinnedToCore', '__wrap_vTaskDelete'))
        # Deletion requires the helper that maps a FreeRTOS handle to the shim entry.
        functions = extract(source, 'find_by_tk_id') + '\n\n' + extract(source, 'find_by_handle') + '\n\n' + functions
        c_path = directory / 'test.c'
        c_path.write_text(HARNESS.replace('@@FUNCTIONS@@', functions))
        binary = directory / 'test'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', str(c_path), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
