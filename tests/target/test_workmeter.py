"""Real C step assembly: measured transfer time, failure and overflow boundaries."""
import ctypes
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Stats(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in
                ('steps', 'elapsed_us', 'max_elapsed_us', 'max_lateness_us',
                 'bus_wait_us', 'bus_active_us', 'transfer_bytes', 'transfers',
                 'timeouts', 'errors', 'flags')]


class Step(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in
                ('elapsed_us', 'lateness_us', 'bus_wait_us', 'bus_active_us',
                 'transfer_bytes', 'transfers', 'timeouts', 'errors')]


class Meter(ctypes.Structure):
    _fields_ = [('stats', Stats), ('step', Step), ('started_us', ctypes.c_uint64),
                ('active', ctypes.c_bool), ('invalid', ctypes.c_bool)]


class WorkmeterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('gcc')
        if compiler is None:
            raise AssertionError('GCC is required')
        cls.folder = tempfile.TemporaryDirectory(prefix='uthome-meter-')
        cls.addClassCleanup(cls.folder.cleanup)
        path = Path(cls.folder.name) / 'workmeter.dll'
        subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-O2', '-shared', str(ROOT/'firmware/src/runtime/workmeter.c'),
                        str(ROOT/'firmware/src/runtime/workcost.c'), '-o', str(path)],
                       check=True, capture_output=True)
        cls.library = ctypes.CDLL(str(path))
        cls.begin = cls.library.workmeter_begin
        cls.begin.argtypes = [ctypes.POINTER(Meter), ctypes.c_uint64]
        cls.bus = cls.library.workmeter_bus
        cls.bus.argtypes = [ctypes.POINTER(Meter), ctypes.c_uint64, ctypes.c_uint64,
                           ctypes.c_uint32, ctypes.c_bool, ctypes.c_bool]
        cls.finish = cls.library.workmeter_finish
        cls.finish.argtypes = [ctypes.POINTER(Meter), ctypes.c_uint64, ctypes.c_uint64, ctypes.c_bool]

    @classmethod
    def tearDownClass(cls):
        if sys.platform == 'win32':
            import _ctypes
            _ctypes.FreeLibrary(cls.library._handle)

    def test_bus_cost_and_step_cost_are_distinct_and_cumulative(self):
        state = Meter()
        p = ctypes.byref(state)
        self.assertEqual(self.begin(p, 1000), 0)
        self.assertEqual(self.begin(p, 1001), -1)
        self.assertEqual(self.bus(p, 5, 20, 30, False, False), 0)
        self.assertEqual(self.bus(p, 7, 11, 0, True, True), 0)
        self.assertEqual(state.stats.steps, 0)  # Nothing published mid-step.
        self.assertEqual(self.finish(p, 1100, 2000, False), 0)
        self.assertEqual((state.stats.elapsed_us, state.stats.bus_wait_us, state.stats.bus_active_us), (100, 12, 31))
        self.assertEqual((state.stats.transfers, state.stats.transfer_bytes, state.stats.timeouts, state.stats.errors), (2, 30, 1, 1))
        self.assertEqual(self.begin(p, 2000), 0)
        self.assertEqual(self.finish(p, 2050, 0, True), 0)
        self.assertEqual((state.stats.steps, state.stats.elapsed_us, state.stats.max_elapsed_us,
                          state.stats.max_lateness_us, state.stats.errors), (2, 150, 100, 2000, 2))

    def test_clock_reversal_overflow_and_overlapping_timings_poison_the_binding(self):
        cases = [(lambda p: None, 999, 0), (lambda p: None, 1000 + 2**32, 0),
                 (lambda p: None, 1010, 2**32),
                 (lambda p: self.bus(p, 10, 20, 0, False, False), 1020, 0),
                 (lambda p: self.bus(p, 2**32, 0, 0, False, False), 1100, 0),
                 (lambda p: self.bus(p, 0, 0, 0, True, False), 1100, 0)]
        for setup, end, late in cases:
            with self.subTest(end=end, lateness=late):
                state = Meter()
                p = ctypes.byref(state)
                self.assertEqual(self.begin(p, 1000), 0)
                setup(p)
                self.assertEqual(self.finish(p, end, late, False), -1)
                self.assertTrue(state.invalid)
                self.assertEqual(state.stats.steps, 0)
                self.assertEqual(self.begin(p, 3000), -1)

    def test_aggregate_saturation_remains_visible(self):
        state = Meter()
        state.stats.elapsed_us = 0xffffffff
        self.begin(ctypes.byref(state), 0)
        self.assertEqual(self.finish(ctypes.byref(state), 1, 0, False), 0)
        self.assertEqual(state.stats.elapsed_us, 0xffffffff)
        self.assertEqual(state.stats.flags, 1)

    def test_no_active_step_and_null_state_never_publish(self):
        state = Meter()
        self.assertEqual(self.bus(ctypes.byref(state), 0, 0, 0, False, False), -1)
        self.assertEqual(self.finish(ctypes.byref(state), 1, 0, False), -1)
        self.assertEqual(self.begin(None, 0), -1)
        self.assertEqual(self.bus(None, 0, 0, 0, False, False), -1)
        self.assertEqual(self.finish(None, 1, 0, False), -1)
        self.assertEqual(state.stats.steps, 0)
