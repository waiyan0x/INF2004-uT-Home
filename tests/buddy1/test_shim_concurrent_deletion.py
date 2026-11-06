from pathlib import Path
import runpy
import subprocess

root = Path.cwd()
source = (
    Path(__file__).resolve().parent
    / "fixtures/shim_task_cleanup_prototype.txt"
).read_text()

helpers = runpy.run_path(
    str(root / "tests/buddy1/test_shim_creation_failures.py")
)
extract = helpers["extract"]
harness = helpers["HARNESS"]

harness = harness.replace(
    "#include <assert.h>",
    "#include <assert.h>\n#include <pthread.h>",
    1,
)

harness = harness.replace(
    "#define DI(saved) do { (saved) = 0; } while (0)",
    "#define DI(saved) do { (saved) = 0; "
    "pthread_mutex_lock(&g_guard); } while (0)",
)

harness = harness.replace(
    "#define EI(saved) do { (void)(saved); } while (0)",
    "#define EI(saved) do { (void)(saved); "
    "pthread_mutex_unlock(&g_guard); } while (0)",
)

harness = harness.replace(
    "static shim_task_t s_tasks[SHIM_TASK_MAX];",
    "static pthread_mutex_t g_guard = PTHREAD_MUTEX_INITIALIZER;\n"
    "static shim_task_t s_tasks[SHIM_TASK_MAX];",
    1,
)


# Reuse the actual host mocks and function extraction framework.
functions = "\n\n".join(
    extract(source, name)
    for name in (
        "find_by_tk_id",
        "find_by_handle",
        "alloc_task_entry",
        "free_task_entry",
        "__wrap_xTaskCreatePinnedToCore",
        "shim_task_reap_one",
        "__wrap_vTaskDelete",
    )
)

enum_start = source.index("enum {")
enum_end = source.index("};", enum_start) + 2
states = source[enum_start:enum_end]

def change(old, new):
    global harness
    assert old in harness, f"Mock not found: {old[:60]}"
    harness = harness.replace(old, new, 1)

change(
    "static shim_task_t s_tasks[SHIM_TASK_MAX];",
    "static shim_task_t s_tasks[SHIM_TASK_MAX];\n"
    + states
    + "\nstatic uint8_t s_cleanup_pending[SHIM_TASK_MAX];"
)

change(
    "typedef struct { int sematr, isemcnt, maxsem; } T_CSEM;",
    "typedef struct { int sematr, isemcnt, maxsem; } T_CSEM;\n"
    "typedef struct { int tskstat; } T_RTSK;\n"
    "#define TTS_DMT 1"
)

change(
    "static int g_last_stack_sz;",
    "static int g_last_stack_sz;\n"
    "static int g_state = 2, g_fail_sem_once = 0;\n"
    "static int g_fail_ter_once = 0, g_del_task_calls = 0;"
)

change(
    "static ER tk_del_sem(ID id) { assert(id == 3); assert(g_sems > 0); --g_sems; return E_OK; }",
    """static ER tk_del_sem(ID id) {
        assert(id == 3 && g_sems > 0);
        if (g_fail_sem_once) {
            g_fail_sem_once = 0;
            return -1;
        }
        --g_sems;
        return E_OK;
    }"""
)

change(
    "static ER tk_del_tsk(ID id) { assert(id == 9 && g_tasks > 0); --g_tasks; return E_OK; }",
    """static ER tk_del_tsk(ID id) {
        assert(id == 9 && g_tasks > 0);
        assert(g_state == TTS_DMT);
        ++g_del_task_calls;
        --g_tasks;
        return E_OK;
    }"""
)

change(
    "static ER tk_ter_tsk(ID id) { assert(id == 9); return E_OK; }",
    """static ER tk_ter_tsk(ID id) {
        assert(id == 9);
        if (g_fail_ter_once) {
            g_fail_ter_once = 0;
            return -1;
        }
        g_state = TTS_DMT;
        return E_OK;
    }
    static ER tk_ref_tsk(ID id, T_RTSK *state) {
        assert(id == 9 && g_tasks > 0);
        state->tskstat = g_state;
        return E_OK;
    }"""
)

change(
    "static ID tk_get_tid(void) { return 0; }",
    "static ID tk_get_tid(void) { return 9; }"
)

test_main = r'''
static TaskHandle_t make_task(void) {
    TaskHandle_t h = NULL;
    g_state = 2;
    assert(__wrap_xTaskCreatePinnedToCore(
        dummy, "WiFi", 4096, NULL, 3, &h, 0) == pdTRUE);
    assert(h != NULL);
    return h;
}

static void check_clean(void) {
    assert(g_sems == 0);
    assert(g_stacks == 0);
    assert(g_tasks == 0);
    for (int i = 0; i < SHIM_TASK_MAX; ++i) {
        assert(s_tasks[i].tsk_id == 0);
        assert(s_cleanup_pending[i] == SHIM_CLEANUP_NONE);
    }
}


struct delete_args {
    TaskHandle_t handle;
};

static void *delete_worker(void *arg) {
    struct delete_args *a = (struct delete_args *)arg;
    __wrap_vTaskDelete(a->handle);
    return NULL;
}

int main(void) {
    /* Test 1: Running self-deleted task must not be reclaimed. */
    make_task();
    __wrap_vTaskDelete(NULL);
    assert(shim_task_reap_one() == 0);
    assert(g_tasks == 1 && g_stacks == 1);
    g_state = TTS_DMT;
    assert(shim_task_reap_one() == 1);
    check_clean();
    puts("PASS: Running task protected; dormant task reclaimed");

    /* Test 2: Semaphore failure must not delete task twice. */
    make_task();
    __wrap_vTaskDelete(NULL);
    g_state = TTS_DMT;
    g_fail_sem_once = 1;
    int before = g_del_task_calls;
    assert(shim_task_reap_one() == -1);
    assert(g_del_task_calls == before + 1);
    assert(g_stacks == 1 && g_sems == 1);
    assert(shim_task_reap_one() == 1);
    assert(g_del_task_calls == before + 1);
    check_clean();
    puts("PASS: Semaphore failure retried without double task deletion");

    /* Test 3: Explicit deletion terminates before reclamation. */
    TaskHandle_t h = make_task();
    __wrap_vTaskDelete(h);
    assert(g_state == TTS_DMT);
    assert(shim_task_reap_one() == 1);
    check_clean();
    puts("PASS: Explicit termination and cleanup");

    /* Test 4: Failed termination must not free running stack. */
    h = make_task();
    g_fail_ter_once = 1;
    __wrap_vTaskDelete(h);
    assert(g_state != TTS_DMT);
    assert(shim_task_reap_one() == 0);
    assert(g_stacks == 1 && g_tasks == 1);
    g_state = TTS_DMT;
    assert(shim_task_reap_one() == 1);
    check_clean();
    puts("PASS: Failed termination retains resources");


    /* Test 5: A second deletion must not reclaim an owned slot. */
    h = make_task();
    __wrap_vTaskDelete(h);
    __wrap_vTaskDelete(h);
    assert(g_tasks == 1 && g_stacks == 1);
    assert(shim_task_reap_one() == 1);
    check_clean();
    puts("PASS: Duplicate deletion did not double-free");

    /* Test 6: The slot must be reusable after complete cleanup. */
    h = make_task();
    assert(h == (TaskHandle_t)&s_tasks[0]);
    __wrap_vTaskDelete(h);
    assert(shim_task_reap_one() == 1);
    check_clean();
    puts("PASS: Cleaned task slot reused");


    h = make_task();
    struct delete_args args = {h};

    pthread_t first, second;
    assert(pthread_create(&first, NULL, delete_worker, &args) == 0);
    assert(pthread_create(&second, NULL, delete_worker, &args) == 0);

    assert(pthread_join(first, NULL) == 0);
    assert(pthread_join(second, NULL) == 0);

    assert(g_tasks == 1 && g_stacks == 1 && g_sems == 1);
    assert(shim_task_reap_one() == 1);
    check_clean();

    puts("PASS: Two concurrent deletion callers; one cleanup");
    puts("PASS: Six lifecycle scenarios");
    return 0;
}
'''

harness = harness[:harness.index("static void check_clean(void)")]
harness = harness.replace("@@FUNCTIONS@@", functions)
harness += "\n" + test_main

directory = Path("/tmp/buddy1-cleanup-work")
test_c = directory / "test_reaper.c"
binary = directory / "test_reaper"

test_c.write_text(harness)

subprocess.run(
    ["cc", "-pthread", "-std=c11", "-Wall", "-Wextra", "-Werror",
     "-Wno-unused-function", str(test_c), "-o", str(binary)],
    check=True
)
subprocess.run([str(binary)], check=True)
