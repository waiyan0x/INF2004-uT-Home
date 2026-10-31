"""Compile fixed event state and compare broadcast/barrier behavior to a model."""
import ctypes as c
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Group(c.Structure):
    _fields_ = [('key', c.c_void_p), ('bits', c.c_uint32), ('users', c.c_uint32), ('live', c.c_bool)]


class Waiter(c.Structure):
    _fields_ = [('group', c.c_uint32), ('wanted', c.c_uint32), ('result', c.c_uint32),
                ('all', c.c_bool), ('clear', c.c_bool), ('used', c.c_bool), ('ready', c.c_bool)]


class Pool(c.Structure):
    _fields_ = [('groups', Group * 8), ('waiters', Waiter * 16)]


class NetworkEventTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        gcc = shutil.which('gcc')
        if gcc is None:
            raise AssertionError('GCC is required')
        cls.temp = tempfile.TemporaryDirectory(prefix='uthome-netevent-')
        cls.addClassCleanup(cls.temp.cleanup)
        dll = Path(cls.temp.name) / 'events.dll'
        subprocess.run([gcc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2', '-shared',
                        str(ROOT / 'firmware/src/network/netevent.c'), '-o', str(dll)],
                       check=True, capture_output=True)
        cls.lib = c.CDLL(str(dll))
        ptr = c.POINTER(c.c_uint32)
        signatures = {
            'create': [c.POINTER(Pool), c.c_void_p],
            'release': [c.POINTER(Pool), c.c_void_p, ptr],
            'set': [c.POINTER(Pool), c.c_void_p, c.c_uint32, ptr, ptr],
            'clear': [c.POINTER(Pool), c.c_void_p, c.c_uint32, ptr],
            'get': [c.POINTER(Pool), c.c_void_p, ptr],
            'begin': [c.POINTER(Pool), c.c_void_p, c.c_uint32, c.c_bool, c.c_bool,
                      c.c_uint32, c.c_bool, ptr, ptr, ptr],
            'finish': [c.POINTER(Pool), c.c_uint32, c.c_bool, ptr],
        }
        for name, args in signatures.items():
            fn = getattr(cls.lib, 'netevent_' + name)
            fn.argtypes = args
            fn.restype = c.c_void_p if name == 'create' else c.c_int
            setattr(cls, name, fn)

    @classmethod
    def tearDownClass(cls):
        if sys.platform == 'win32':
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)

    def setUp(self):
        self.pool = Pool()
        self.ptr = c.byref(self.pool)
        self.handle = self.create(self.ptr, None)
        self.assertTrue(self.handle)

    def start(self, wanted, clear=False, all_bits=False, sync=0, block=True, handle=None):
        token, result, wake = c.c_uint32(), c.c_uint32(), c.c_uint32()
        status = self.begin(self.ptr, self.handle if handle is None else handle,
                            wanted, clear, all_bits, sync, block,
                            c.byref(token), c.byref(result), c.byref(wake))
        return status, token.value, result.value, wake.value

    def publish(self, bits):
        result, wake = c.c_uint32(), c.c_uint32()
        self.assertEqual(self.set(self.ptr, self.handle, bits, c.byref(result), c.byref(wake)), 0)
        return result.value, wake.value

    def complete(self, token, timeout=False):
        result = c.c_uint32(0xdeadbeef)
        return self.finish(self.ptr, token, timeout, c.byref(result)), result.value

    def test_exact_capacity_static_identity_and_reuse(self):
        handles = [self.handle] + [self.create(self.ptr, 100 + i) for i in range(7)]
        self.assertEqual(handles[1:], list(range(100, 107)))
        self.assertIsNone(self.create(self.ptr, None))
        self.assertIsNone(self.create(self.ptr, 100))
        wake = c.c_uint32(99)
        self.assertEqual(self.release(self.ptr, handles[3], c.byref(wake)), 0)
        self.assertEqual(wake.value, 0)
        self.assertEqual(self.release(self.ptr, handles[3], c.byref(wake)), -1)
        self.assertEqual(self.create(self.ptr, 333), 333)

    def test_broadcast_snapshot_and_unrelated_bits_survive(self):
        first = self.start(3, clear=True, all_bits=True)
        second = self.start(1, clear=True)
        third = self.start(2)
        self.assertEqual([first[0], second[0], third[0]], [0, 0, 0])
        self.assertEqual(self.publish(0x83), (0x80, 7))
        for item in (first, second, third):
            self.assertEqual(self.complete(item[1]), (1, 0x83))
            self.assertEqual(self.complete(item[1])[0], -1)

    def test_static_key_cannot_alias_a_later_dynamic_handle(self):
        future_key = c.addressof(self.pool.groups[2])
        self.assertEqual(self.create(self.ptr, future_key), future_key)
        dynamic = self.create(self.ptr, None)
        self.assertTrue(dynamic)
        self.assertNotEqual(dynamic, future_key)

    def test_partial_all_wait_and_timeout_do_not_clear(self):
        wait = self.start(3, clear=True, all_bits=True)
        self.assertEqual(self.publish(5), (5, 0))
        self.assertEqual(self.complete(wait[1])[0], 0)
        self.assertEqual(self.complete(wait[1], True), (1, 5))
        self.assertEqual(self.start(3, clear=True, all_bits=True, block=False), (1, 16, 5, 0))
        self.assertEqual(self.start(1, clear=True, block=False), (1, 16, 5, 0))
        self.assertEqual(self.publish(0), (4, 0))

    def test_barrier_last_arrival_uses_preclear_snapshot(self):
        first = self.start(3, clear=True, all_bits=True, sync=1)
        self.assertEqual(first[0], 0)
        second = self.start(3, clear=True, all_bits=True, sync=2)
        self.assertEqual(second, (1, 16, 3, 1))
        self.assertEqual(self.complete(first[1]), (1, 3))
        self.assertEqual(self.publish(0), (0, 0))

    def test_delete_pins_identity_and_wakes_zero(self):
        others = [self.create(self.ptr, 100 + i) for i in range(7)]
        self.assertTrue(all(others))
        first, second = self.start(1), self.start(2)
        self.publish(1)
        wake = c.c_uint32()
        self.assertEqual(self.release(self.ptr, self.handle, c.byref(wake)), 0)
        self.assertEqual(wake.value, 2)
        self.assertIsNone(self.create(self.ptr, self.handle))
        self.assertIsNone(self.create(self.ptr, None))
        self.assertEqual(self.complete(second[1]), (1, 0))
        self.assertIsNone(self.create(self.ptr, None))
        self.assertEqual(self.complete(first[1]), (1, 1))
        self.assertTrue(self.create(self.ptr, None))

    def test_wait_capacity_failure_has_no_barrier_side_effect(self):
        pending = [self.start(2) for _ in range(16)]
        self.assertTrue(all(item[0] == 0 for item in pending))
        before = bytes(self.pool)
        self.assertEqual(self.start(2, sync=1)[0], -1)
        self.assertEqual(bytes(self.pool), before)
        self.assertEqual(self.start(1, sync=1, block=False)[0], 1)
        self.assertEqual(self.complete(pending[0][1], True), (1, 1))
        self.assertEqual(self.start(2)[0], 0)

    def test_rejects_reserved_bits_unknown_keys_and_null_outputs(self):
        before = bytes(self.pool)
        output = c.c_uint32()
        for bits in (0xff000000, 0x01000001):
            self.assertEqual(self.set(self.ptr, self.handle, bits, c.byref(output), c.byref(output)), -1)
            self.assertEqual(self.clear(self.ptr, self.handle, bits, c.byref(output)), -1)
            self.assertEqual(self.start(bits)[0], -1)
        self.assertEqual(self.start(0)[0], -1)
        self.assertEqual(self.start(1, handle=123)[0], -1)
        self.assertEqual(self.get(self.ptr, None, c.byref(output)), -1)
        self.assertEqual(self.get(self.ptr, self.handle, None), -1)
        self.assertEqual(bytes(self.pool), before)

    def test_seeded_interleavings_match_independent_model(self):
        rng = random.Random(23019)
        bits, waits = 0, {}
        for _ in range(6000):
            action = rng.choice(('start', 'set', 'finish', 'clear'))
            if action == 'start':
                wanted = rng.randint(1, 255)
                clear, all_bits = bool(rng.randrange(2)), bool(rng.randrange(2))
                matched = (bits & wanted) == wanted if all_bits else bool(bits & wanted)
                result = self.start(wanted, clear, all_bits)
                if matched:
                    self.assertEqual(result, (1, 16, bits, 0))
                    if clear:
                        bits &= ~wanted
                elif len(waits) == 16:
                    self.assertEqual(result[0], -1)
                else:
                    self.assertEqual(result[0], 0)
                    waits[result[1]] = [wanted, clear, all_bits, None]
            elif action == 'set':
                setting = rng.randrange(256)
                bits |= setting
                clearing, wake = 0, 0
                for token, item in waits.items():
                    wanted, clear, all_bits, ready = item
                    matched = (bits & wanted) == wanted if all_bits else bool(bits & wanted)
                    if ready is None and matched:
                        item[3] = bits
                        wake |= 1 << token
                        if clear:
                            clearing |= wanted
                bits &= ~clearing
                self.assertEqual(self.publish(setting), (bits, wake))
            elif action == 'finish' and waits:
                token = rng.choice(list(waits))
                timeout = bool(rng.randrange(2))
                ready = waits[token][3]
                actual = self.complete(token, timeout)
                if ready is not None or timeout:
                    self.assertEqual(actual, (1, bits if ready is None else ready))
                    del waits[token]
                else:
                    self.assertEqual(actual[0], 0)
            elif action == 'clear':
                clearing = rng.randrange(256)
                result = c.c_uint32()
                self.assertEqual(self.clear(self.ptr, self.handle, clearing, c.byref(result)), 0)
                self.assertEqual(result.value, bits)
                bits &= ~clearing
            result = c.c_uint32()
            self.assertEqual(self.get(self.ptr, self.handle, c.byref(result)), 0)
            self.assertEqual(result.value, bits)


if __name__ == '__main__':
    unittest.main()
