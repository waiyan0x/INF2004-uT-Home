"""Fault-inject the C slot core used by c3store, without SDK or flash access."""
import ctypes as ct
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from test_storage_layout import canonical_table

ROOT = Path(__file__).resolve().parents[2]
SLOT = 12288
BytePointer = ct.POINTER(ct.c_uint8)
Owner = ct.CFUNCTYPE(ct.c_bool, ct.c_void_p)
Size = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.POINTER(ct.c_uint32))
Read = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.c_size_t, BytePointer, ct.c_size_t)
Write = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.c_size_t, BytePointer, ct.c_size_t)
Erase = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.c_size_t, ct.c_size_t)


class Operations(ct.Structure):
    _fields_ = [('owner', Owner), ('size', Size), ('table', Read),
                ('read', Read), ('write', Write), ('erase', Erase)]


class State(ct.Structure):
    _fields_ = [('operations', ct.POINTER(Operations)), ('context', ct.c_void_p),
                ('ready', ct.c_bool), ('scratch', ct.c_uint8 * 256)]


class Flash:
    """Record bounded physical/partition accesses; reject a faulty core safely."""

    def __init__(self):
        self.owner = True
        self.flash_size = 0x400000
        self.size_error = False
        self.table = bytearray(canonical_table())
        self.partition = bytearray(range(256)) * 96
        self.calls = []
        self.fail_call = None
        self.unexpected = []
        # Keep all callback objects alive for every synchronous native call.
        self.operations = Operations(Owner(lambda _: self.owner), Size(self.get_size),
                                     Read(self.read_table), Read(self.read),
                                     Write(self.write), Erase(self.erase))
        self.state = State(ct.pointer(self.operations), None, False)

    def called(self, name, offset, size):
        self.calls.append((name, offset, size))
        return len(self.calls) == self.fail_call

    def get_size(self, _, output):
        self.called('size', 0, 0)
        output[0] = self.flash_size
        return -91 if self.size_error else 0

    def read_table(self, _, offset, output, size):
        failed = self.called('table', offset, size)
        if (self.state.ready or not 0x8000 <= offset <= 0x8b00 or size != 256 or
                ct.addressof(output.contents) != ct.addressof(self.state.scratch)):
            self.unexpected.append(('table', offset, size))
            return -92
        if failed:
            return -91
        ct.memmove(output, bytes(self.table[offset - 0x8000:offset - 0x8000 + size]), size)
        return 0

    def read(self, _, offset, output, size):
        failed = self.called('read', offset, size)
        if (offset + size > len(self.partition) or not 0 < size <= 256 or
                ct.addressof(output.contents) != ct.addressof(self.state.scratch)):
            self.unexpected.append(('read', offset, size))
            return -92
        if failed:
            return -91
        ct.memmove(output, bytes(self.partition[offset:offset + size]), size)
        return 0

    def write(self, _, offset, data, size):
        failed = self.called('write', offset, size)
        if offset + size > len(self.partition):
            self.unexpected.append(('write', offset, size))
            return -92
        if failed:
            return -91
        self.partition[offset:offset + size] = ct.string_at(data, size)
        return 0

    def erase(self, _, offset, size):
        failed = self.called('erase', offset, size)
        if offset not in (0, SLOT) or size != SLOT:
            self.unexpected.append(('erase', offset, size))
            return -92
        if failed:
            return -91
        self.partition[offset:offset + size] = b'\xff' * size
        return 0


def setUpModule():
    subprocess.run([sys.executable, str(ROOT / 'tools/c_standard/check.py'), '--root', str(ROOT)], check=True)


class StorageAdapterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('gcc')
        if compiler is None:
            raise AssertionError('GCC is required')
        cls.folder = tempfile.TemporaryDirectory(prefix='uthome-slot-core-')
        cls.addClassCleanup(cls.folder.cleanup)
        target = Path(cls.folder.name) / 'cfgslot.dll'
        subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-shared', '-O2', str(ROOT / 'firmware/src/config/cfgslot.c'),
                        str(ROOT / 'firmware/src/config/cfgpart.c'), '-o', str(target)],
                       check=True, capture_output=True)
        cls.lib = ct.CDLL(str(target))
        signatures = {
            'open': [ct.POINTER(State)], 'sync': [ct.POINTER(State)],
            'read': [ct.POINTER(State), ct.c_uint, ct.c_size_t, BytePointer, ct.c_size_t],
            'write': [ct.POINTER(State), ct.c_uint, ct.c_size_t, BytePointer, ct.c_size_t],
            'erase': [ct.POINTER(State), ct.c_uint],
            'region_allowed': [ct.POINTER(State), ct.c_size_t, ct.c_size_t],
        }
        for name, arguments in signatures.items():
            function = getattr(cls.lib, 'cfgslot_' + name)
            function.argtypes = arguments
            function.restype = ct.c_bool if name == 'region_allowed' else ct.c_int

    @classmethod
    def tearDownClass(cls):
        if sys.platform == 'win32':
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)

    def open(self, flash):
        self.assertEqual(self.lib.cfgslot_open(ct.byref(flash.state)), 0)
        self.assertTrue(flash.state.ready)
        self.assertEqual(flash.calls, [('size', 0, 0)] +
                         [('table', 0x8000 + offset, 256) for offset in range(0, 3072, 256)])
        self.assertEqual(flash.unexpected, [])
        flash.calls.clear()

    def test_unavailable_or_nonowner_access_never_reaches_flash(self):
        flash = Flash()
        state = ct.byref(flash.state)
        data = (ct.c_uint8 * 1)(42)
        self.assertEqual(self.lib.cfgslot_open(None), -1)
        self.assertEqual(self.lib.cfgslot_open(ct.byref(State())), -1)
        for ready in (False, True):
            if ready:
                self.open(flash)
                flash.owner = False
            self.assertEqual(self.lib.cfgslot_read(state, 0, 0, data, 1), -1)
            self.assertEqual(self.lib.cfgslot_write(state, 0, 0, data, 1), -1)
            self.assertEqual(self.lib.cfgslot_erase(state, 0), -1)
            self.assertEqual(self.lib.cfgslot_sync(state), -1)
            self.assertEqual(flash.calls, [])
        flash = Flash()
        flash.owner = False
        self.assertEqual(self.lib.cfgslot_open(ct.byref(flash.state)), -1)
        self.assertEqual(flash.calls, [])

    def test_size_failure_and_wrong_chip_size_do_not_read_the_table(self):
        for size_error, size in ((True, 0x400000), (False, 0), (False, 0x800000)):
            flash = Flash()
            flash.size_error, flash.flash_size = size_error, size
            self.assertEqual(self.lib.cfgslot_open(ct.byref(flash.state)), -1)
            self.assertFalse(flash.state.ready)
            self.assertEqual(flash.calls, [('size', 0, 0)])

    def test_every_table_chunk_failure_or_corruption_keeps_writes_disabled(self):
        for chunk in range(12):
            for corrupt in (False, True):
                with self.subTest(chunk=chunk, corrupt=corrupt):
                    flash = Flash()
                    if corrupt:
                        flash.table[chunk * 256] ^= 1
                    else:
                        flash.fail_call = chunk + 2
                    self.assertEqual(self.lib.cfgslot_open(ct.byref(flash.state)), -1)
                    self.assertFalse(flash.state.ready)
                    self.assertEqual(len(flash.calls), chunk + 2)
                    self.assertEqual(flash.unexpected, [])
                    self.assertFalse(self.lib.cfgslot_region_allowed(ct.byref(flash.state), 0x110000, 1))
                    flash.table = bytearray(canonical_table())
                    flash.fail_call = None
                    flash.calls.clear()
                    self.open(flash)
                    self.assertEqual(self.lib.cfgslot_open(ct.byref(flash.state)), -1)
                    self.assertEqual(flash.calls, [])

    def test_subtraction_bounds_and_null_buffers_fail_before_io(self):
        flash = Flash()
        self.open(flash)
        state = ct.byref(flash.state)
        data = (ct.c_uint8 * 1)(42)
        maximum = ct.c_size_t(-1).value
        for slot, offset, size in ((2, 0, 1), (0xffffffff, 0, 1), (0, SLOT + 1, 0),
                                   (0, SLOT, 1), (1, SLOT - 1, 2), (0, maximum, 1), (1, 1, maximum)):
            with self.subTest(slot=slot, offset=offset, size=size):
                self.assertEqual(self.lib.cfgslot_read(state, slot, offset, data, size), -1)
                self.assertEqual(self.lib.cfgslot_write(state, slot, offset, data, size), -1)
        self.assertEqual(self.lib.cfgslot_read(state, 0, 0, None, 0), -1)
        self.assertEqual(self.lib.cfgslot_write(state, 0, 0, None, 0), -1)
        self.assertEqual(self.lib.cfgslot_erase(state, 2), -1)
        self.assertEqual(self.lib.cfgslot_read(state, 1, SLOT, data, 0), 0)
        self.assertEqual(self.lib.cfgslot_sync(state), 0)
        self.assertEqual(flash.calls, [])
        self.assertEqual(self.lib.cfgslot_write(state, 1, SLOT, data, 0), 0)
        self.assertEqual(flash.calls, [('write', SLOT * 2, 0)])

    def test_read_chunking_and_each_failure_preserve_the_uncopied_tail(self):
        for failed in (None, 1, 2, 3):
            flash = Flash()
            self.open(flash)
            flash.fail_call = failed
            output = (ct.c_uint8 * 600)(*([0xa5] * 600))
            offset = 2 * SLOT - 600
            result = self.lib.cfgslot_read(ct.byref(flash.state), 1, SLOT - 600, output, 600)
            expected = [('read', offset, 256), ('read', offset + 256, 256), ('read', offset + 512, 88)]
            copied = 600 if failed is None else (failed - 1) * 256
            self.assertEqual(result, 0 if failed is None else -1)
            self.assertEqual(flash.calls, expected[:failed])
            self.assertEqual(bytes(output[:copied]), flash.partition[offset:offset + copied])
            self.assertEqual(bytes(output[copied:]), b'\xa5' * (600 - copied))
            self.assertEqual(flash.unexpected, [])

    def test_write_and_erase_are_exact_and_fail_without_retry(self):
        for slot in (0, 1):
            for fail in (False, True):
                flash = Flash()
                self.open(flash)
                before = bytes(flash.partition)
                flash.fail_call = 1 if fail else None
                data = (ct.c_uint8 * 3)(9, 8, 7)
                offset = slot * SLOT + SLOT - 3
                self.assertEqual(self.lib.cfgslot_write(ct.byref(flash.state), slot, SLOT - 3, data, 3), -1 if fail else 0)
                expected = bytearray(before)
                if not fail:
                    expected[offset:offset + 3] = bytes(data)
                self.assertEqual(flash.partition, expected)
                self.assertEqual(flash.calls, [('write', offset, 3)])
                flash.calls.clear()
                self.assertEqual(self.lib.cfgslot_erase(ct.byref(flash.state), slot), -1 if fail else 0)
                if not fail:
                    expected[slot * SLOT:(slot + 1) * SLOT] = b'\xff' * SLOT
                self.assertEqual(flash.partition, expected)
                self.assertEqual(flash.calls, [('erase', slot * SLOT, SLOT)])
                self.assertEqual(flash.unexpected, [])

    def test_flash_guard_is_owner_independent_and_keeps_exact_window(self):
        flash = Flash()
        self.open(flash)
        flash.owner = False
        state = ct.byref(flash.state)
        for address, size, allowed in ((0x110000, 0x6000, True), (0x116000, 0, True),
                                       (0x10ffff, 1, False), (0x116000, 1, False),
                                       (0x110000, ct.c_size_t(-1).value, False)):
            self.assertEqual(self.lib.cfgslot_region_allowed(state, address, size), allowed)
        self.assertEqual(flash.calls, [])
