"""Verify aligned usable windows in the static wireless task pool."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class StaticAlignmentTests(unittest.TestCase):
    def test_all_slots_keep_their_capacity_and_alignment(self):
        source = r'''#include "c3task.h"
#include <assert.h>
#include <stdint.h>

int main (void)
{
#if defined(TEST_BLE)
    const uint32_t sizes[4] = {4096U, 4096U, 4096U, 0U};
    const uint32_t count = 3U;
#else
    const uint32_t sizes[4] = {2816U, 3584U, 4096U, 6656U};
    const uint32_t count = 4U;
#endif
    void * stacks[4] = {0};
    uint32_t capacities[4] = {0};
    assert(0 == c3task_init());
    for (uint32_t index = 0U; index < count; ++index)
    {
        assert(0 == c3task_stack_acquire((uintptr_t)(index + 1U), sizes[index],
                                          &stacks[index], &capacities[index]));
        assert(0U == ((uintptr_t)stacks[index] & 15U));
        assert(sizes[index] == capacities[index]);
        for (uint32_t prior = 0U; prior < index; ++prior)
        {
            const uintptr_t start = (uintptr_t)stacks[index];
            const uintptr_t other = (uintptr_t)stacks[prior];
            assert((start + sizes[index] <= other) ||
                   (other + sizes[prior] <= start));
        }
        ((uint8_t *)stacks[index])[0] = (uint8_t)(index + 1U);
        ((uint8_t *)stacks[index])[sizes[index] - 1U] = (uint8_t)(index + 1U);
    }
    for (uint32_t index = 0U; index < count; ++index)
    {
        assert((uint8_t)(index + 1U) == ((uint8_t *)stacks[index])[0]);
        assert((uint8_t)(index + 1U) == ((uint8_t *)stacks[index])[sizes[index] - 1U]);
        assert(0 == c3task_stack_release((uintptr_t)(index + 1U), stacks[index], true));
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            (path / 'main.c').write_text(source, encoding='utf-8')
            for flags in ([], ['-DTEST_BLE=1', '-DUTHOME_STATIC_TASKS=1',
                                '-DUTHOME_NETWORK_BLE=1']):
                with self.subTest(profile='BLE' if flags else 'Wi-Fi'):
                    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                                    '-DUTHOME_C3TASK_HOST_TEST=1', *flags,
                                    '-I', str(ROOT / 'firmware/hal/ports/esp32c3'),
                                    str(ROOT / 'firmware/hal/ports/esp32c3/c3task.c'),
                                    str(path / 'main.c'), '-o', str(path / 'test')], check=True)
                    subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
