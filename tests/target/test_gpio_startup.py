"""Execute the boot-once policy shared by both GPIO adapters."""
import ctypes as ct
import _ctypes
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / 'firmware/hal/ports/esp32c3'
Create = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.c_size_t)


class State(ct.Structure):
    _fields_ = [('attempted', ct.c_bool), ('ready', ct.c_bool)]


class StartupContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('gcc')
        if compiler is None:
            raise AssertionError('GCC is required')
        subprocess.run([sys.executable, str(ROOT/'tools/c_standard/check.py')], check=True)
        cls.folder = tempfile.TemporaryDirectory(prefix='uthome-bootonce-')
        cls.addClassCleanup(cls.folder.cleanup)
        target = Path(cls.folder.name)/'bootonce.dll'
        flags = [] if sys.platform == 'win32' else ['-fPIC']
        subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        *flags, '-shared', '-O2', '-I', str(ROOT/'firmware/hal/include'),
                        str(ROOT/'firmware/hal/bootonce.c'), '-o', str(target)], check=True)
        cls.lib = ct.CDLL(str(target))
        cls.addClassCleanup(_ctypes.FreeLibrary if sys.platform == 'win32' else _ctypes.dlclose,
                            cls.lib._handle)
        cls.lib.bootonce_run.argtypes = [ct.POINTER(State), ct.c_size_t, Create, ct.c_void_p]
        cls.lib.bootonce_ready.argtypes = [ct.POINTER(State)]
        cls.lib.bootonce_ready.restype = ct.c_bool

    def test_all_resources_created_once_and_published_together(self):
        for capacity in (2, 4):
            with self.subTest(capacity=capacity):
                state, calls, early_ready = State(), [], []
                def create(context, index):
                    calls.append((context, index))
                    early_ready.append(self.lib.bootonce_ready(ct.byref(state)))
                    return 0
                callback = Create(create)
                self.assertEqual(0, self.lib.bootonce_run(ct.byref(state), capacity, callback, 17))
                self.assertEqual([(17, index) for index in range(capacity)], calls)
                self.assertEqual([False] * capacity, early_ready)
                self.assertTrue(self.lib.bootonce_ready(ct.byref(state)))
                for _ in range(20):
                    self.assertEqual(0, self.lib.bootonce_run(ct.byref(state), capacity, callback, 17))
                self.assertEqual(capacity, len(calls))

    def test_each_partial_failure_is_terminal_and_retains_created_resources(self):
        for capacity in (2, 4):
            for failure in range(capacity):
                with self.subTest(capacity=capacity, failure=failure):
                    state, calls, retained = State(), [], []
                    def create(_, index):
                        calls.append(index)
                        if index == failure:
                            return -91
                        retained.append(index)
                        return 0
                    callback = Create(create)
                    self.assertEqual(-1, self.lib.bootonce_run(ct.byref(state), capacity, callback, None))
                    self.assertFalse(self.lib.bootonce_ready(ct.byref(state)))
                    self.assertTrue(state.attempted)
                    for _ in range(20):
                        self.assertEqual(-1, self.lib.bootonce_run(ct.byref(state), capacity, callback, None))
                    self.assertEqual(list(range(failure + 1)), calls)
                    self.assertEqual(list(range(failure)), retained)

    def test_invalid_requests_do_not_mark_startup_attempted(self):
        state, calls = State(), []
        callback = Create(lambda _, index: calls.append(index) or 0)
        for pointer, capacity, create in ((None, 2, callback),
                                         (ct.byref(state), 0, callback),
                                         (ct.byref(state), 2, Create())):
            self.assertEqual(-1, self.lib.bootonce_run(pointer, capacity, create, None))
        self.assertFalse(state.attempted)
        self.assertFalse(self.lib.bootonce_ready(None))
        self.assertEqual([], calls)

    def test_native_resources_are_permanent_and_boot_order_precedes_use(self):
        application = (ROOT/'firmware/app/c3app.c').read_text()
        for module in ('c3pulse', 'c3pwm'):
            source = (PORT/f'{module}.c').read_text()
            self.assertEqual(1, source.count('tk_cre_mtx('))
            for operation in ('tk_del_mtx(', 'malloc('):
                self.assertNotIn(operation, source)
            position = application.index(f'{module}_startup()')
            self.assertLess(application.index('activate_init()'), position)
            self.assertLess(position, application.index('cfgctrl_restore('))
            self.assertLess(position, application.index('cfgpump_step('))


if __name__ == '__main__':
    unittest.main()
