#!/usr/bin/env python3
"""Compile and stress the project-owned static task-stack implementation."""

from pathlib import Path
import os
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware/hal/ports/esp32c3/c3task.c"
INCLUDE = ROOT / "firmware/hal/ports/esp32c3"

PROGRAM = r"""
#include "c3task.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static int reap_one (void)
{
    return 0;
}

static int other_reaper (void)
{
    return 0;
}

static void acquire (uintptr_t owner,
                     uint32_t  requested,
                     void **   pp_stack,
                     uint32_t *p_capacity)
{
    assert(0 == c3task_stack_acquire(owner,
                                     requested,
                                     pp_stack,
                                     p_capacity));
    assert(NULL != *pp_stack);
    assert(0U == ((uintptr_t)*pp_stack & 15U));
    assert(requested <= *p_capacity);
}

int main (void)
{
    assert(4U == c3task_capacity());
    assert(0U == c3task_used());
    assert(0 == c3task_init());
    assert(0 == c3task_init());
    assert(0 == c3task_reaper_bind(reap_one));
    assert(0 == c3task_reaper_bind(reap_one));
    assert(0 != c3task_reaper_bind(other_reaper));

    void *   stacks[4]     = {NULL};
    uint32_t capacities[4] = {0U};
    for (uint32_t cycle = 0U; cycle < 10000U; ++cycle)
    {
        const uintptr_t owner = (uintptr_t)cycle + 1000U;
        acquire(owner, 4096U, &stacks[0], &capacities[0]);
        assert(1U == c3task_used());
        assert(0 == c3task_stack_release(owner, stacks[0], true));
        assert(0U == c3task_used());
    }

    uintptr_t stale_owner    = 100U;
    void *    stale_stack    = NULL;
    uint32_t  stale_capacity = 0U;
    acquire(stale_owner, 2816U, &stale_stack, &stale_capacity);
    assert(0 == c3task_stack_release(stale_owner, stale_stack, true));
    acquire(stale_owner + 1U, 2816U, &stacks[0], &capacities[0]);
    assert(stale_stack == stacks[0]);
    assert(0 != c3task_stack_release(stale_owner, stale_stack, true));
    assert(0 == c3task_stack_release(stale_owner + 1U, stacks[0], true));

    acquire(1U, 2800U, &stacks[0], &capacities[0]);
    acquire(2U, 3500U, &stacks[1], &capacities[1]);
    acquire(3U, 4096U, &stacks[2], &capacities[2]);
    acquire(4U, 5000U, &stacks[3], &capacities[3]);
    assert(2816U == capacities[0]);
    assert(3584U == capacities[1]);
    assert(4096U == capacities[2]);
    assert(6656U == capacities[3]);
    assert(4U == c3task_used());
    assert(0 != c3task_seal());

    for (uint32_t index = 0U; index < 4U; ++index)
    {
        assert(0 == c3task_stack_commit((uintptr_t)index + 1U,
                                        stacks[index]));
        for (uint32_t other = index + 1U; other < 4U; ++other)
        {
            assert(stacks[index] != stacks[other]);
        }
    }
    assert(0 == c3task_seal());
    assert(0 == c3task_seal());
    assert(c3task_is_sealed());

    void *   rejected_stack    = NULL;
    uint32_t rejected_capacity = 0U;
    assert(0 != c3task_stack_acquire(5U,
                                     1U,
                                     &rejected_stack,
                                     &rejected_capacity));
    assert(0 != c3task_stack_release(1U, stacks[0], false));
    assert(0 != c3task_stack_release(9U, stacks[0], true));

    c3task_cleanup_pending(1U, 100U);
    c3task_cleanup_pending(1U, 101U);
    c3task_cleanup_complete(1U, 112U);
    c3task_stack_record_t   records[4] = {0};
    c3task_resource_stats_t stats      = {0};
    assert(0 == c3task_resource_snapshot(records, 4U, &stats));
    assert(stats.b_sealed);
    assert(1U == stats.post_seal_rejections);
    assert(1U == stats.cleanup_requests);
    assert(1U == stats.cleanup_completions);
    assert(12U == stats.maximum_cleanup_latency_ms);
    for (uint32_t index = 0U; index < 4U; ++index)
    {
        assert(records[index].b_used);
        assert(records[index].b_committed);
        assert(capacities[index] == records[index].capacity_bytes);
        assert(0 == c3task_stack_release((uintptr_t)index + 1U,
                                         stacks[index],
                                         true));
    }
    assert(0U == c3task_used());
    assert(0 != c3task_stack_acquire(20U,
                                     4096U,
                                     &rejected_stack,
                                     &rejected_capacity));

    puts("PASS: actual c3task pool, startup seal, diagnostics and 10000 reuse cycles");
    return 0;
}
"""


def main():
    with tempfile.TemporaryDirectory(prefix="buddy1_c3task_") as folder:
        directory = Path(folder)
        test_source = directory / "test_c3task.c"
        binary = directory / "test_c3task"
        test_source.write_text(PROGRAM, encoding="utf-8")
        command = [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fno-omit-frame-pointer",
            "-fsanitize=address,undefined",
            "-DUTHOME_C3TASK_HOST_TEST=1",
            f"-I{INCLUDE}",
            str(SOURCE),
            str(test_source),
            "-o",
            str(binary),
        ]
        subprocess.run(command, check=True)
        environment = os.environ.copy()
        environment["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1"
        subprocess.run([str(binary)], check=True, env=environment)


if __name__ == "__main__":
    main()
