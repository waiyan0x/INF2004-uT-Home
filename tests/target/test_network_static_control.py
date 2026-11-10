"""Pinned lwIP validity observes the real static queue adapter's control byte."""
import ctypes as c
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class StaticControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='uthome-static-control-')
        cls.addClassCleanup(cls.temp.cleanup)
        path = Path(cls.temp.name)
        source = (ROOT/'firmware/hal/ports/esp32c3/network/c3nq.c').read_text()
        start = source.index('void * c3nq_static (')
        end = source.index('/** @brief Counting semaphore', start)
        # Compile the production ABI boundary unchanged; only pool admission is
        # replaced, so this checks storage visibility rather than pool internals.
        harness = r"""
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
static bool gb_accept = true;
void control_accept (bool b_accept) { gb_accept = b_accept; }
static void * create (uint32_t length, uint32_t bytes, uint8_t kind,
                      uint8_t * p_storage, void * p_key, bool b_static)
{
    (void)length; (void)bytes; (void)kind; (void)p_storage; (void)b_static;
    return gb_accept ? p_key : NULL;
}
"""
        (path/'control.c').write_text(harness + source[start:end])
        subprocess.run([shutil.which('cc'), '-std=c99', '-Wall', '-Wextra',
                        '-Werror', '-pedantic', '-shared', str(path/'control.c'),
                        '-o', str(path/'control.dylib')], check=True, capture_output=True)
        cls.lib = c.CDLL(str(path/'control.dylib'))
        cls.lib.c3nq_static.argtypes = [c.c_uint32, c.c_uint32, c.c_void_p, c.c_void_p, c.c_uint8]
        cls.lib.c3nq_static.restype = c.c_void_p
        cls.lib.control_accept.argtypes = [c.c_bool]

    def test_success_is_visible_to_pinned_lwip_zero_storage_validity_check(self):
        self.lib.control_accept(True)
        control = (c.c_uint8 * 80)()
        self.assertEqual(self.lib.c3nq_static(1, 0, None, control, 3), c.addressof(control))
        self.assertNotEqual(bytes(control), bytes(80))
        self.assertEqual(bytes(control)[1:], bytes(79))

    def test_failed_or_duplicate_lease_does_not_change_caller_storage(self):
        self.lib.control_accept(False)
        for original in [bytes(80), bytes([17])*80]:
            control = (c.c_uint8 * 80).from_buffer_copy(original)
            self.assertIsNone(self.lib.c3nq_static(1, 0, None, control, 3))
            self.assertEqual(bytes(control), original)

    def test_null_control_is_not_dereferenced(self):
        self.lib.control_accept(True)
        self.assertIsNone(self.lib.c3nq_static(1, 0, None, None, 3))


if __name__ == '__main__':
    unittest.main()
