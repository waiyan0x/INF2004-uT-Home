"""Execute the real event adapter with controlled owned native-call responses."""
import ctypes as c
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / 'firmware/hal/ports/esp32c3/network'


class Config(c.Structure):
    _fields_ = [(name, c.c_int) for name in
                ('create_fail_at', 'signal_result', 'wait_result')] + [
                    (name, c.c_uint32) for name in
                    ('clock_fail_at', 'clock_reverse_at', 'stale_wakes')] + [
                        (name, c.c_bool) for name in
                        ('cache', 'context', 'publish', 'delete')]


class Metrics(c.Structure):
    _fields_ = [(name, c.c_uint32) for name in
                ('creates', 'signals', 'waits', 'enters', 'leaves', 'depth',
                 'violations', 'clocks')] + [('budgets', c.c_uint32 * 32)]


class EventAdapterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler, objcopy = shutil.which('gcc'), shutil.which('objcopy')
        if not compiler or not objcopy:
            raise AssertionError('GCC and objcopy are required')
        subprocess.run([sys.executable, str(ROOT / 'tools/c_standard/check.py')], check=True)
        folder = tempfile.TemporaryDirectory(prefix='uthome-event-adapter-')
        cls.addClassCleanup(folder.cleanup)
        cls.folder = Path(folder.name)
        flags = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2']
        if sys.platform != 'win32':
            flags.append('-fPIC')
        for directory in (PORT, ROOT / 'firmware/src/network', ROOT / 'tests/target'):
            flags += ['-I', str(directory)]
        obj = cls.folder / 'event.o'
        subprocess.run([compiler, *flags, '-c', str(PORT / 'c3event.c'), '-o', str(obj)], check=True)
        aliases = []
        for name in ('create', 'signal', 'wait', 'enter', 'leave', 'available',
                     'context', 'now', 'tick_hz'):
            aliases += ['--redefine-sym', f'c3ewait_{name}=evfake_{name}']
        subprocess.run([objcopy, *aliases, str(obj)], check=True)
        cls.library = cls.folder / 'adapter.dll'
        sources = [ROOT / 'firmware/src/network' / f'{name}.c'
                   for name in ('netevent', 'netwait')]
        subprocess.run([compiler, *flags, '-shared', str(obj), *map(str, sources),
                        str(ROOT / 'tests/target/evfake.c'), '-o', str(cls.library)], check=True)

    def setUp(self):
        library = self.folder / f'{self._testMethodName}.dll'
        shutil.copyfile(self.library, library)
        self.lib = c.CDLL(str(library))
        if sys.platform == 'win32':
            import _ctypes
            self.addCleanup(_ctypes.FreeLibrary, self.lib._handle)
        signatures = {
            'evfake_configure': ([c.POINTER(Config), c.c_void_p], None),
            'evfake_metrics': ([], c.POINTER(Metrics)),
            'c3event_init': ([], c.c_int),
            'c3event_create': ([], c.c_void_p),
            'c3event_delete': ([c.c_void_p], None),
            'c3event_set': ([c.c_void_p, c.c_uint32], c.c_uint32),
            'c3event_get': ([c.c_void_p], c.c_uint32),
            'c3event_wait': ([c.c_void_p, c.c_uint32, c.c_int, c.c_int, c.c_uint32], c.c_uint32),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.lib, name)
            function.argtypes, function.restype = arguments, result
        self.handle = None
        self.configure()

    def configure(self, **changes):
        config = Config(**(dict(cache=True, context=True, publish=True) | changes))
        self.lib.evfake_configure(c.byref(config), self.handle)

    @property
    def metrics(self):
        return self.lib.evfake_metrics().contents

    def initialize(self):
        self.assertEqual(self.lib.c3event_init(), 0)
        self.assertEqual(self.lib.c3event_init(), 0)
        self.assertEqual(self.metrics.creates, 16)
        self.handle = self.lib.c3event_create()
        self.assertTrue(self.handle)
        self.configure()

    def wait(self, ticks=10):
        return self.lib.c3event_wait(self.handle, 1, 1, 1, ticks)

    def tearDown(self):
        self.assertEqual(self.metrics.violations, 0)
        self.assertEqual(self.metrics.depth, 0)
        self.assertEqual(self.metrics.enters, self.metrics.leaves)

    def test_partial_initialization_is_terminal_without_recreation(self):
        self.configure(create_fail_at=3)
        self.assertEqual(self.lib.c3event_init(), -1)
        self.configure()
        self.assertEqual(self.lib.c3event_init(), -1)
        self.assertEqual(self.metrics.creates, 3)
        self.assertIsNone(self.lib.c3event_create())

    def test_full_hint_remains_ready_and_returns_the_event_snapshot(self):
        self.initialize()
        self.configure(signal_result=1)
        self.assertEqual(self.wait(), 1)
        self.assertEqual(self.metrics.signals, 1)
        self.assertEqual(self.lib.c3event_get(self.handle), 0)
        self.assertTrue(self.lib.c3event_create())

    def test_broken_notification_stops_new_operations(self):
        self.initialize()
        self.configure(signal_result=-1)
        self.assertEqual(self.wait(), 1)
        self.assertIsNone(self.lib.c3event_create())
        self.assertEqual(self.lib.c3event_init(), -1)
        self.assertEqual(self.lib.c3event_set(self.handle, 2), 0)
        self.assertEqual(self.metrics.creates, 16)

    def test_stale_wakes_keep_one_absolute_budget(self):
        self.initialize()
        self.configure(stale_wakes=2)
        self.assertEqual(self.wait(), 1)
        self.assertEqual(list(self.metrics.budgets)[:3], [10, 9, 8])

    def test_early_native_timeout_does_not_end_a_later_ready_wait(self):
        self.initialize()
        self.configure(stale_wakes=1, wait_result=-2)
        self.assertEqual(self.wait(), 1)
        self.assertEqual(list(self.metrics.budgets)[:2], [10, 9])

    def test_native_wait_failure_releases_registration_for_reuse(self):
        self.initialize()
        for _ in range(20):
            self.configure(wait_result=-1, publish=False)
            self.assertEqual(self.wait(), 0)
        self.configure()
        self.assertEqual(self.wait(), 1)
        self.assertEqual(self.metrics.waits, 21)
        self.assertEqual(self.metrics.creates, 16)

    def test_clock_failures_before_and_after_registration_release_slots(self):
        self.initialize()
        for position in (1, 2):
            for _ in range(20):
                self.configure(clock_fail_at=position)
                self.assertEqual(self.wait(), 0)
        self.configure(clock_reverse_at=2)
        self.assertEqual(self.wait(), 0)
        self.assertEqual(self.metrics.waits, 0)
        self.configure()
        self.assertEqual(self.wait(), 1)

    def test_deadline_expiry_releases_waiter_without_clearing_other_bits(self):
        self.initialize()
        self.assertEqual(self.lib.c3event_set(self.handle, 2), 2)
        self.configure(publish=False, wait_result=-2)
        self.assertEqual(self.wait(3), 2)
        self.assertEqual(list(self.metrics.budgets)[:3], [3, 2, 1])
        self.assertEqual(self.lib.c3event_get(self.handle), 2)

    def test_delete_during_wait_returns_zero_and_releases_group(self):
        self.initialize()
        others = [self.lib.c3event_create() for _ in range(7)]
        self.assertTrue(all(others))
        self.assertIsNone(self.lib.c3event_create())
        self.configure(delete=True)
        self.assertEqual(self.wait(), 0)
        self.assertTrue(self.lib.c3event_create())

    def test_infinite_wait_keeps_native_infinite_budget(self):
        self.initialize()
        self.assertEqual(self.wait(0xffffffff), 1)
        self.assertEqual(self.metrics.budgets[0], 0xffffffff)

    def test_poll_and_invalid_context_never_wait_or_read_clock(self):
        self.initialize()
        self.assertEqual(self.wait(0), 0)
        self.configure(context=False)
        self.assertEqual(self.wait(), 0)
        self.configure(cache=False)
        self.assertEqual(self.wait(), 0)
        self.assertIsNone(self.lib.c3event_create())
        self.assertEqual(self.metrics.waits, 0)
        self.assertEqual(self.metrics.clocks, 0)


if __name__ == '__main__':
    unittest.main()
