"""Exercise the actual bounded C counters, including wide sizes and saturation."""
import ctypes
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from test_qualification import qualification

ROOT = Path(__file__).resolve().parents[2]
MAX = 0xffffffff


class Phase(ctypes.Structure):
    _fields_ = [("allocations", ctypes.c_uint32), ("bytes", ctypes.c_uint32),
                ("frees", ctypes.c_uint32), ("creates", ctypes.c_uint32 * 11), ("kernel_calls", ctypes.c_uint32 * 4)]


class Heap(ctypes.Structure):
    _fields_ = [("allocations", ctypes.c_uint32), ("bytes", ctypes.c_uint32),
                ("frees", ctypes.c_uint32), ("kernel_calls", ctypes.c_uint32 * 4)]


class State(ctypes.Structure):
    _fields_ = [("startup", Phase), ("normal", Phase),
                ("application", Heap), ("begun", ctypes.c_bool),
                ("sealed", ctypes.c_bool), ("saturated", ctypes.c_bool)]


class AuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("gcc")
        if compiler is None:
            raise AssertionError("GCC is required")
        cls.folder = tempfile.TemporaryDirectory(prefix="uthome-audit-")
        cls.addClassCleanup(cls.folder.cleanup)
        path = Path(cls.folder.name) / "memaudit.dll"
        subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        "-shared", "-O2", str(ROOT / "firmware/src/runtime/memaudit.c"),
                        "-o", str(path)], check=True, capture_output=True)
        cls.library = ctypes.CDLL(str(path))
        cls.seal = cls.library.memaudit_seal
        cls.seal.argtypes = [ctypes.POINTER(State)]
        cls.begin = cls.library.memaudit_begin
        cls.begin.argtypes = [ctypes.POINTER(State)]
        cls.create = cls.library.memaudit_create
        cls.create.argtypes = [ctypes.POINTER(State), ctypes.c_uint32]
        cls.heap = cls.library.memaudit_heap
        cls.heap.argtypes = [ctypes.POINTER(State), ctypes.c_size_t, ctypes.c_bool]
        cls.kernel = cls.library.memaudit_kernel
        cls.kernel.argtypes = [ctypes.POINTER(State), ctypes.c_uint32]

    @classmethod
    def tearDownClass(cls):
        if sys.platform == "win32":
            import _ctypes
            _ctypes.FreeLibrary(cls.library._handle)

    def test_boundary_is_irreversible_and_preserves_startup(self):
        state = State()
        self.assertEqual(self.heap(ctypes.byref(state), 64, False), 0)
        for kind in range(11):
            self.assertEqual(self.create(ctypes.byref(state), kind), 0)
        for kind in range(4):
            self.assertEqual(self.kernel(ctypes.byref(state), kind), 0)
        self.assertEqual(self.seal(ctypes.byref(state)), -1)
        self.assertEqual(self.begin(ctypes.byref(state)), 0)
        self.assertEqual(self.begin(ctypes.byref(state)), -1)
        self.assertEqual(self.seal(ctypes.byref(state)), 0)
        self.assertEqual(self.seal(ctypes.byref(state)), -1)
        self.assertEqual(self.heap(ctypes.byref(state), 99, True), 0)
        self.assertEqual(self.heap(ctypes.byref(state), 128, False), 0)
        self.assertEqual(self.create(ctypes.byref(state), 3), 0)
        self.assertEqual(self.kernel(ctypes.byref(state), 2), 0)
        self.assertEqual((state.startup.allocations, state.startup.bytes), (1, 64))
        self.assertEqual(list(state.startup.creates), [1] * 11)
        self.assertEqual((state.normal.allocations, state.normal.bytes, state.normal.frees), (1, 128, 1))
        self.assertEqual(list(state.normal.creates), [0, 0, 0, 1] + [0] * 7)
        self.assertEqual(list(state.startup.kernel_calls), [1] * 4)
        self.assertEqual(list(state.normal.kernel_calls), [0, 0, 1, 0])

    def test_application_startup_is_a_subset_not_a_reset(self):
        state = State()
        self.heap(ctypes.byref(state), 16, False)
        self.kernel(ctypes.byref(state), 0)
        self.assertEqual(self.begin(ctypes.byref(state)), 0)
        self.heap(ctypes.byref(state), 32, False)
        self.heap(ctypes.byref(state), 0, True)
        self.kernel(ctypes.byref(state), 0)
        self.assertEqual(self.seal(ctypes.byref(state)), 0)
        self.heap(ctypes.byref(state), 64, False)
        self.kernel(ctypes.byref(state), 0)
        self.assertEqual((state.startup.allocations, state.startup.bytes), (2, 48))
        self.assertEqual((state.application.allocations, state.application.bytes, state.application.frees), (1, 32, 1))
        self.assertEqual((state.startup.kernel_calls[0], state.application.kernel_calls[0], state.normal.kernel_calls[0]), (2, 1, 1))
        self.assertEqual((state.normal.allocations, state.normal.bytes), (1, 64))

    def test_saturating_counts_never_wrap_or_hide_overflow(self):
        state = State()
        state.startup.allocations = MAX
        state.startup.frees = MAX
        state.startup.creates[10] = MAX
        state.startup.bytes = MAX - 1
        self.heap(ctypes.byref(state), 1, False)
        self.assertTrue(state.saturated)
        self.assertEqual(state.startup.bytes, MAX)
        self.heap(ctypes.byref(state), ctypes.c_size_t(-1).value, False)
        self.heap(ctypes.byref(state), 0, True)
        self.create(ctypes.byref(state), 10)
        self.assertEqual((state.startup.allocations, state.startup.bytes,
                          state.startup.frees, state.startup.creates[10]), (MAX,) * 4)
        self.begin(ctypes.byref(state))
        self.assertEqual(self.seal(ctypes.byref(state)), 0)
        self.assertTrue(state.saturated)
        self.assertEqual(state.normal.allocations, 0)

    def test_invalid_arguments_do_not_modify_state(self):
        state = State()
        before = bytes(state)
        for kind in (11, MAX):
            self.assertEqual(self.create(ctypes.byref(state), kind), -1)
        self.assertEqual(bytes(state), before)
        self.assertEqual(self.create(None, 0), -1)
        self.assertEqual(self.heap(None, 0, False), -1)
        self.assertEqual(self.seal(None), -1)
        self.assertEqual(self.begin(None), -1)
        self.assertEqual(self.kernel(None, 0), -1)
        self.assertEqual(self.kernel(ctypes.byref(state), 4), -1)
        self.assertEqual(bytes(state), before)


class AuditBuildTests(unittest.TestCase):
    def test_flash_hooks_and_bypassed_native_symbols_fail(self):
        symbols = {name: 0x40381000 for name in ("c3audit_alloc", "c3audit_free", "memaudit_heap",
                   "memaudit_create", "memaudit_kernel", "disint", "enaint")}
        symbols.update(c3audit_create_task=0x40382000, __wrap_tk_cre_tsk=0x40382000,
                       c3audit_native_task=0x42010000)
        self.assertTrue(qualification.check_audit_symbols(symbols)["tk_cre_tsk"])
        for name, address in (("disint", 0x42020000), ("__wrap_tk_cre_tsk", 0),
                              ("tk_cre_tsk", 0x42010000), ("c3audit_native_task", 0)):
            with self.subTest(name=name), self.assertRaises(qualification.QualificationError):
                qualification.check_audit_symbols(dict(symbols, **{name: address}))

    def test_hook_renames_cannot_be_missing_or_escape_their_translation_unit(self):
        commands = {Path("/test" + suffix).resolve(): " ".join(
            f"-D{symbol}={value}" for symbol, value in renames.items())
            for suffix, renames in qualification.AUDIT_RENAMES.items()}
        self.assertEqual(len(qualification.verify_audit_scope(commands)), 12)
        missing = dict(commands)
        del missing[next(iter(missing))]
        with self.assertRaises(qualification.QualificationError):
            qualification.verify_audit_scope(missing)
        leaked = dict(commands)
        leaked[Path("/unrelated.c").resolve()] = "-Desp_heap_trace_alloc_hook=c3audit_alloc"
        with self.assertRaises(qualification.QualificationError):
            qualification.verify_audit_scope(leaked)
        changed = dict(commands)
        first = next(iter(changed))
        changed[first] += " -Desp_heap_trace_alloc_hook=unobserved"
        with self.assertRaises(qualification.QualificationError):
            qualification.verify_audit_scope(changed)
