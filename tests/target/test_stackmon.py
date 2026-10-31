"""Sentinel scanner bounds, lowest touched byte and invalid output preservation."""
import ctypes
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class StackmonTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('gcc')
        if compiler is None:
            raise AssertionError('GCC required')
        cls.folder = tempfile.TemporaryDirectory(prefix='uthome-stack-')
        cls.addClassCleanup(cls.folder.cleanup)
        path = Path(cls.folder.name)/'stackmon.dll'
        subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-O2', '-shared', str(ROOT/'firmware/src/runtime/stackmon.c'),
                        '-o', str(path)], check=True, capture_output=True)
        cls.lib = ctypes.CDLL(str(path))
        cls.paint = cls.lib.stackmon_paint
        cls.paint.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        cls.scan = cls.lib.stackmon_unused
        cls.scan.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]

    @classmethod
    def tearDownClass(cls):
        if sys.platform == 'win32':
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)

    def test_actual_reservations_and_lowest_touched_byte(self):
        for size in (1024, 8192, 8448):
            with self.subTest(size=size):
                data = (ctypes.c_ubyte*(size+2))()
                data[0] = data[-1] = 0x36
                start = ctypes.byref(data, 1)
                self.assertEqual(self.paint(start, size), 0)
                value = ctypes.c_size_t()
                self.assertEqual(self.scan(start, size, ctypes.byref(value)), 0)
                self.assertEqual(value.value, size)
                data[size] = 0
                self.scan(start, size, ctypes.byref(value))
                self.assertEqual(value.value, size-1)
                data[201] = 0
                self.scan(start, size, ctypes.byref(value))
                self.assertEqual(value.value, 200)
                data[1] = 0
                self.scan(start, size, ctypes.byref(value))
                self.assertEqual(value.value, 0)
                self.assertEqual((data[0], data[-1]), (0x36, 0x36))

    def test_invalid_inputs_do_not_touch_storage_or_output(self):
        data = (ctypes.c_ubyte*16)(*([0x36]*16))
        value = ctypes.c_size_t(123)
        for pointer, size in ((None, 16), (data, 0), (data, 8449)):
            self.assertEqual(self.paint(pointer, size), -1)
            self.assertEqual(self.scan(pointer, size, ctypes.byref(value)), -1)
        self.assertEqual(self.scan(data, 16, None), -1)
        self.assertEqual(value.value, 123)
        self.assertEqual(bytes(data), bytes([0x36]*16))
