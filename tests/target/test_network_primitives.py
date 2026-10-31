"""Real fixed ring and deadline behavior, including randomized queue workloads."""
import ctypes as c
from collections import deque
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Ring(c.Structure):
    _fields_ = [('storage', c.POINTER(c.c_uint8)), ('bytes', c.c_size_t),
                ('capacity', c.c_uint32), ('item_bytes', c.c_uint32),
                ('head', c.c_uint32), ('count', c.c_uint32)]


class Wait(c.Structure):
    _fields_ = [('started', c.c_uint64), ('deadline', c.c_uint64), ('infinite', c.c_bool)]


class NetworkPrimitiveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('gcc')
        if compiler is None:
            raise AssertionError('GCC is required')
        cls.folder = tempfile.TemporaryDirectory(prefix='uthome-netcore-')
        cls.addClassCleanup(cls.folder.cleanup)
        dll = Path(cls.folder.name) / 'netcore.dll'
        subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-O2', '-shared', str(ROOT/'firmware/src/network/netring.c'),
                        str(ROOT/'firmware/src/network/netwait.c'), '-o', str(dll)],
                       check=True, capture_output=True)
        cls.lib = c.CDLL(str(dll))
        cls.init = cls.lib.netring_init
        cls.init.argtypes = [c.POINTER(Ring), c.POINTER(c.c_uint8), c.c_size_t, c.c_uint32, c.c_uint32]
        cls.send = cls.lib.netring_send
        cls.send.argtypes = [c.POINTER(Ring), c.c_void_p, c.c_int]
        cls.receive = cls.lib.netring_receive
        cls.receive.argtypes = [c.POINTER(Ring), c.c_void_p]
        cls.peek = cls.lib.netring_peek
        cls.peek.argtypes = [c.POINTER(Ring), c.c_void_p]
        cls.reset = cls.lib.netring_reset
        cls.reset.argtypes = [c.POINTER(Ring)]
        cls.wait_init = cls.lib.netwait_init
        cls.wait_init.argtypes = [c.POINTER(Wait), c.c_uint64, c.c_uint32, c.c_uint32]
        cls.remaining = cls.lib.netwait_remaining
        cls.remaining.argtypes = [c.POINTER(Wait), c.c_uint64, c.POINTER(c.c_uint32)]

    @classmethod
    def tearDownClass(cls):
        if sys.platform == 'win32':
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)

    def ring(self, count):
        storage = (c.c_uint8 * (count * 4 + 16))(*([0xDA] * (count * 4 + 16)))
        ring = Ring()
        self.assertEqual(self.init(c.byref(ring), c.cast(c.byref(storage, 8), c.POINTER(c.c_uint8)),
                                   count * 4, count, 4), 0)
        return ring, storage

    def test_one_slot_is_usable_full_then_replace_and_reusable(self):
        ring, storage = self.ring(1)
        one, two, result = c.c_uint32(1), c.c_uint32(2), c.c_uint32(99)
        self.assertEqual(self.send(c.byref(ring), c.byref(one), 0), 0)
        self.assertEqual(ring.count, 1)
        self.assertEqual(self.send(c.byref(ring), c.byref(two), 1), -1)
        self.assertEqual(self.peek(c.byref(ring), c.byref(result)), 0)
        self.assertEqual(result.value, 1)
        self.assertEqual(self.send(c.byref(ring), c.byref(two), 2), 0)
        self.assertEqual(self.receive(c.byref(ring), c.byref(result)), 0)
        self.assertEqual(result.value, 2)
        self.assertEqual(self.receive(c.byref(ring), c.byref(result)), -1)
        self.assertEqual(result.value, 2)
        self.assertEqual(self.send(c.byref(ring), c.byref(one), 0), 0)
        self.assertEqual(bytes(storage)[:8], b'\xda' * 8)
        self.assertEqual(bytes(storage)[-8:], b'\xda' * 8)

    def test_randomized_front_back_peek_full_reset_and_wrap_match_deque(self):
        rng = random.Random(971)
        for capacity in (1, 2, 3, 16, 32):
            ring, storage = self.ring(capacity)
            expected = deque()
            for _ in range(5000):
                op = rng.randrange(6)
                value = c.c_uint32(rng.randrange(2**32))
                if op < 2:
                    result = self.send(c.byref(ring), c.byref(value), op)
                    if len(expected) == capacity:
                        self.assertEqual(result, -1)
                    else:
                        self.assertEqual(result, 0)
                        (expected.appendleft if op else expected.append)(value.value)
                elif op in (2, 3):
                    result = (self.peek if op == 2 else self.receive)(c.byref(ring), c.byref(value))
                    if expected:
                        self.assertEqual(result, 0)
                        self.assertEqual(value.value, expected[0] if op == 2 else expected.popleft())
                    else:
                        self.assertEqual(result, -1)
                elif op == 4:
                    self.assertEqual(self.reset(c.byref(ring)), 0)
                    expected.clear()
                else:
                    self.assertEqual(self.receive(c.byref(ring), None), 0 if expected else -1)
                    if expected:
                        expected.popleft()
                self.assertEqual(ring.count, len(expected))
                self.assertEqual(bytes(storage)[:8] + bytes(storage)[-8:], b'\xda' * 16)

    def test_rejected_metadata_and_arguments_do_not_modify_state_or_bytes(self):
        ring, storage = self.ring(2)
        value = c.c_uint32(42)
        before = bytes(ring), bytes(storage)
        for capacity, size, available in ((0, 4, 8), (2, 0, 8), (3, 4, 8), (2**32-1, 2**32-1, 8)):
            self.assertEqual(self.init(c.byref(ring), ring.storage, available, capacity, size), -1)
            self.assertEqual((bytes(ring), bytes(storage)), before)
        for position in (-1, 2, 3):
            self.assertEqual(self.send(c.byref(ring), c.byref(value), position), -1)
            self.assertEqual((bytes(ring), bytes(storage)), before)
        self.assertEqual(self.send(c.byref(ring), None, 0), -1)
        self.assertEqual(self.init(None, ring.storage, 8, 2, 4), -1)
        ring.head = 2
        self.assertEqual(self.receive(c.byref(ring), c.byref(value)), -1)
        self.assertEqual(value.value, 42)
        self.assertEqual(bytes(storage), before[1])

    def test_ticks_use_actual_frequency_and_stale_wakes_cannot_extend_timeout(self):
        wait, result = Wait(), c.c_uint32()
        self.assertEqual(self.wait_init(c.byref(wait), 1000000, 10, 100), 0)
        for elapsed, expected in ((0, 100), (1, 100), (25000, 75), (99999, 1), (100000, 0), (200000, 0)):
            self.assertEqual(self.remaining(c.byref(wait), 1000000 + elapsed, c.byref(result)), 0)
            self.assertEqual(result.value, expected)
        self.assertEqual(self.wait_init(c.byref(wait), 0, 1, 128), 0)
        self.assertEqual(wait.deadline, 7813)
        self.assertEqual(self.remaining(c.byref(wait), 0, c.byref(result)), 0)
        self.assertEqual(result.value, 8)

    def test_infinite_poll_long_wait_overflow_and_backwards_clock(self):
        wait, result = Wait(), c.c_uint32(77)
        self.assertEqual(self.wait_init(c.byref(wait), 100, 0, 100), 0)
        self.assertEqual(self.remaining(c.byref(wait), 100, c.byref(result)), 0)
        self.assertEqual(result.value, 0)
        self.assertEqual(self.wait_init(c.byref(wait), 100, 2**32-1, 100), 0)
        self.assertEqual(self.remaining(c.byref(wait), 2**64-1, c.byref(result)), 0)
        self.assertEqual(result.value, 2**32-1)
        self.assertEqual(self.wait_init(c.byref(wait), 0, 2**32-2, 1), 0)
        self.assertEqual(self.remaining(c.byref(wait), 0, c.byref(result)), 0)
        self.assertEqual(result.value, 2**31-1)
        self.assertEqual(self.wait_init(c.byref(wait), 100, 1, 1000), 0)
        before = bytes(wait)
        for now, hz in ((2**64-1, 100), (0, 0), (0, 1000001)):
            self.assertEqual(self.wait_init(c.byref(wait), now, 1, hz), -1)
            self.assertEqual(bytes(wait), before)
        result.value = 77
        self.assertEqual(self.remaining(c.byref(wait), 99, c.byref(result)), -1)
        self.assertEqual(result.value, 77)


if __name__ == '__main__':
    unittest.main()
