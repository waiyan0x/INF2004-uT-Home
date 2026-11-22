"""Check the static-task API when no wireless task pool is selected."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NoStaticTaskTests(unittest.TestCase):
    def test_disabled_pool_reports_unsealed(self):
        source = r'''#include "c3task.h"
#include <assert.h>

int main (void)
{
    assert(0U == c3task_capacity());
    assert(0 != c3task_init());
    assert(0 != c3task_seal());
    assert(!c3task_is_sealed());
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            (path / 'main.c').write_text(source, encoding='utf-8')
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            '-DUTHOME_STATIC_TASKS=0',
                            '-I', str(ROOT / 'firmware/hal/ports/esp32c3'),
                            str(ROOT / 'firmware/hal/ports/esp32c3/c3task.c'),
                            str(path / 'main.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
