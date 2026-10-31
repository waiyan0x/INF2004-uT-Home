"""Execute the fixed networking pool against adversarial scheduling callbacks."""
import ctypes as c
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
ENTER = c.CFUNCTYPE(c.c_uint32, c.c_void_p)
LEAVE = c.CFUNCTYPE(None, c.c_void_p, c.c_uint32)
CLOCK = c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(c.c_uint64))
BLOCK = c.CFUNCTYPE(c.c_bool, c.c_void_p)
OWNER = c.CFUNCTYPE(c.c_uint32, c.c_void_p)
WAIT = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint32, c.c_bool, c.c_uint32)
SIGNAL = c.CFUNCTYPE(None, c.c_void_p, c.c_uint32, c.c_bool)
LOCK = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint32, c.c_uint32)
UNLOCK = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint32)


class Ops(c.Structure):
    _fields_ = [('context', c.c_void_p), ('enter', ENTER), ('leave', LEAVE),
                ('clock', CLOCK), ('block', BLOCK), ('owner', OWNER),
                ('wait', WAIT), ('signal', SIGNAL), ('lock', LOCK), ('unlock', UNLOCK)]


class Ring(c.Structure):
    _fields_ = [('storage', c.c_void_p), ('bytes', c.c_size_t), ('capacity', c.c_uint32),
                ('item_bytes', c.c_uint32), ('head', c.c_uint32), ('count', c.c_uint32)]


class Slot(c.Structure):
    _fields_ = [('ring', Ring), ('key', c.c_void_p),
                ('kind', c.c_int), ('count', c.c_uint32), ('capacity', c.c_uint32),
                ('users', c.c_uint32), ('owner', c.c_uint32), ('depth', c.c_uint32), ('live', c.c_bool)]


class Pool(c.Structure):
    _fields_ = [('ops', Ops), ('slots', Slot * 56), ('storage', c.c_uint8 * 8192), ('hz', c.c_uint32), ('ready', c.c_bool)]


class NetworkPoolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        gcc = shutil.which('gcc')
        if gcc is None:
            raise AssertionError('GCC is required')
        cls.temp = tempfile.TemporaryDirectory(prefix='uthome-netpool-')
        cls.addClassCleanup(cls.temp.cleanup)
        dll = Path(cls.temp.name) / 'netpool.dll'
        subprocess.run([gcc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2', '-shared',
                        *[str(ROOT/f'firmware/src/network/{name}.c') for name in ('netring', 'netwait', 'netpool')],
                        '-o', str(dll)], check=True, capture_output=True)
        cls.lib = c.CDLL(str(dll))
        declarations = {
            'init': [c.POINTER(Pool), c.POINTER(Ops), c.c_uint32],
            'create': [c.POINTER(Pool), c.c_int, c.c_uint32, c.c_uint32, c.c_uint32,
                       c.c_void_p, c.c_size_t, c.c_void_p],
            'release': [c.POINTER(Pool), c.c_void_p],
            'send': [c.POINTER(Pool), c.c_void_p, c.c_void_p, c.c_uint32, c.c_int, c.c_bool],
            'receive': [c.POINTER(Pool), c.c_void_p, c.c_void_p, c.c_uint32, c.c_bool, c.c_bool],
            'mutex': [c.POINTER(Pool), c.c_void_p, c.c_uint32, c.c_bool, c.c_bool],
            'query': [c.POINTER(Pool), c.c_void_p, c.POINTER(c.c_uint32), c.POINTER(c.c_uint32), c.POINTER(c.c_int)],
            'reset': [c.POINTER(Pool), c.c_void_p],
        }
        for name, args in declarations.items():
            fn = getattr(cls.lib, 'netpool_' + name)
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
        self.now = 1000000
        self.owner = 1
        self.allowed = True
        self.clock_ok = True
        self.critical = 0
        self.errors = []
        self.waits = []
        self.locks = [0] * 56
        self.lock_calls = []
        self.signals = []
        self.on_wait = None
        self.on_lock = None

        def enter(_):
            saved = self.critical
            self.critical += 1
            return saved

        def leave(_, saved):
            if self.critical != saved + 1:
                self.errors.append('unbalanced critical section')
            self.critical = saved

        def clock(_, output):
            output[0] = self.now
            return 0 if self.clock_ok else -1

        def wait(_, slot, sender, milliseconds):
            if self.critical:
                self.errors.append('blocking while interrupts excluded')
            self.waits.append((slot, sender, milliseconds))
            if self.on_wait:
                return self.on_wait(slot, sender, milliseconds)
            self.now += min(milliseconds, 20) * 1000
            return 0  # Stale notification, including the exact deadline.

        def signal(_, slot, sender):
            if not self.critical:
                self.errors.append('signal outside protected operation')
            self.signals.append((slot, sender))

        def lock(_, slot, milliseconds):
            if self.critical:
                self.errors.append('native lock while interrupts excluded')
            self.lock_calls.append((slot, milliseconds))
            if self.on_lock:
                return self.on_lock(slot, milliseconds)
            if self.locks[slot]:
                return -1
            self.locks[slot] = self.owner
            return 0

        def unlock(_, slot):
            if not self.critical:
                self.errors.append('unlock metadata can race with handoff')
            if self.locks[slot] != self.owner:
                return -1
            self.locks[slot] = 0
            return 0

        self.callbacks = [ENTER(enter), LEAVE(leave), CLOCK(clock),
                          BLOCK(lambda _: self.allowed and self.critical == 0),
                          OWNER(lambda _: self.owner), WAIT(wait), SIGNAL(signal), LOCK(lock), UNLOCK(unlock)]
        self.ops = Ops(None, *self.callbacks)
        self.assertEqual(self.init(self.ptr, c.byref(self.ops), 100), 0)

    def tearDown(self):
        self.assertEqual(self.critical, 0)
        self.assertEqual(self.errors, [])

    def new(self, kind=0, capacity=1, size=4, initial=0, storage=None, storage_bytes=0, key=None):
        return self.create(self.ptr, kind, capacity, size, initial, storage, storage_bytes, key)

    def status(self, handle):
        count, space, kind = c.c_uint32(), c.c_uint32(), c.c_int()
        self.assertEqual(self.query(self.ptr, handle, c.byref(count), c.byref(space), c.byref(kind)), 0)
        return count.value, space.value, kind.value

    def test_exact_capacity_all_slots_and_external_static_storage(self):
        self.assertIsNone(self.new(capacity=513))
        self.assertIsNone(self.new(kind=1, capacity=0, size=0))
        self.assertIsNone(self.new(kind=1, capacity=2, size=0, initial=3))
        external = (c.c_uint8 * 4096)()
        key = c.c_uint32(91)
        handle = self.new(capacity=1024, storage=c.byref(external), storage_bytes=4096, key=c.byref(key))
        self.assertEqual(handle, c.addressof(key))
        self.assertIsNone(self.new(key=c.byref(key)))
        value = c.c_uint32(0x12345678)
        self.assertEqual(self.send(self.ptr, handle, c.byref(value), 0, 0, False), 0)
        self.assertEqual(bytes(external)[:4], bytes(value))
        self.assertEqual(key.value, 91)
        handles = [self.new() for _ in range(7)]
        self.assertTrue(all(handles))
        self.assertEqual(len(set(handles + [handle])), 8)
        self.assertIsNone(self.new())
        self.assertEqual(self.release(self.ptr, handles[4]), 0)
        self.assertTrue(self.new())
        self.assertIsNone(self.new())
        self.assertTrue(all(self.new(kind=1, capacity=1, size=0) for _ in range(16)))
        self.assertIsNone(self.new(kind=1, capacity=1, size=0))
        self.assertTrue(all(self.new(kind=2, size=0) for _ in range(32)))
        self.assertIsNone(self.new(kind=2, size=0))

    def test_wifi_sized_queue_and_partition_limits_preserve_all_payloads(self):
        large = self.new(capacity=200, size=8)
        self.assertTrue(large)
        self.assertIsNone(self.new(capacity=129, size=8))
        medium = [self.new(capacity=128, size=8) for _ in range(5)]
        self.assertTrue(all(medium))
        self.assertIsNone(self.new(capacity=65, size=8))
        small = [self.new(capacity=64, size=8) for _ in range(2)]
        self.assertTrue(all(small))
        self.assertIsNone(self.new())
        queues = [(large, 200)] + [(handle, 128) for handle in medium] + [(handle, 64) for handle in small]
        for tag, (handle, count) in enumerate(queues):
            for sequence in range(count):
                value = (c.c_uint32 * 2)(tag, sequence)
                self.assertEqual(self.send(self.ptr, handle, value, 0, 0, False), 0)
            self.assertEqual(self.status(handle)[:2], (count, 0))
            self.assertEqual(self.send(self.ptr, handle, value, 0, 0, False), -1)
        for tag, (handle, count) in enumerate(queues):
            for sequence in range(count):
                value = (c.c_uint32 * 2)(0xffffffff, 0xffffffff)
                self.assertEqual(self.receive(self.ptr, handle, value, 0, False, False), 0)
                self.assertEqual(list(value), [tag, sequence])
            self.assertEqual(self.release(self.ptr, handle), 0)
        exact = self.new(capacity=256, size=8)
        self.assertTrue(exact)
        self.assertIsNone(self.new(capacity=257, size=8))
        self.assertEqual(self.release(self.ptr, exact), 0)
        self.assertTrue(self.new(capacity=256, size=8))

    def test_stale_wakes_use_remaining_budget_and_preserve_live_lease(self):
        handle = self.new()
        result = c.c_uint32(88)
        self.assertEqual(self.receive(self.ptr, handle, c.byref(result), 10, False, False), -1)
        self.assertEqual([x[2] for x in self.waits], [100, 80, 60, 40, 20])
        self.assertEqual(result.value, 88)
        self.assertEqual(self.pool.slots[0].users, 0)
        self.assertEqual(self.release(self.ptr, handle), 0)

    def test_early_native_timeouts_do_not_truncate_finite_or_infinite_waits(self):
        handle = self.new()
        result = c.c_uint32(89)

        def early(slot, sender, milliseconds):
            self.now += min(milliseconds, 7) * 1000
            return 1

        self.on_wait = early
        self.assertEqual(self.receive(self.ptr, handle, c.byref(result), 2, False, False), -1)
        self.assertEqual([x[2] for x in self.waits], [20, 13, 6])
        self.assertEqual(result.value, 89)
        self.waits.clear()
        value = c.c_uint32(37)

        def eventual(slot, sender, milliseconds):
            self.assertEqual(milliseconds, 2**32 - 1)
            self.now += 1000000
            if len(self.waits) == 3:
                self.assertEqual(self.send(self.ptr, handle, c.byref(value), 0, 0, True), 0)
            return 1

        self.on_wait = eventual
        self.assertEqual(self.receive(self.ptr, handle, c.byref(result), 2**32 - 1, False, False), 0)
        self.assertEqual(result.value, 37)
        self.assertEqual(len(self.waits), 3)

    def test_mutex_clamped_native_timeouts_preserve_total_deadline(self):
        handle = self.new(kind=2, size=0)
        failed_release = []

        def timeout(slot, milliseconds):
            failed_release.append(self.release(self.ptr, handle))
            self.now += milliseconds * 1000
            return 1

        self.on_lock = timeout
        self.assertEqual(self.mutex(self.ptr, handle, 2**32 - 2, True, False), -1)
        waits = [duration for _, duration in self.lock_calls]
        self.assertEqual(sum(waits), (2**32 - 2) * 10)
        self.assertTrue(all(0 <= duration <= 2**31 - 1 for duration in waits))
        self.assertEqual(waits[-1], 0)
        self.assertTrue(all(value == -1 for value in failed_release))
        self.assertEqual(self.pool.slots[24].users, 0)
        self.assertEqual(self.release(self.ptr, handle), 0)

    def test_waiting_receiver_blocks_release_and_accepts_isr_send(self):
        handle = self.new()
        value, result = c.c_uint32(42), c.c_uint32()
        attempts = []

        def wake(slot, sender, milliseconds):
            attempts.append(self.release(self.ptr, handle))
            self.now += 25000
            if len(attempts) == 3:
                attempts.append(self.send(self.ptr, handle, c.byref(value), 999, 0, True))
            return 0

        self.on_wait = wake
        self.assertEqual(self.receive(self.ptr, handle, c.byref(result), 10, False, False), 0)
        self.assertEqual(result.value, 42)
        self.assertEqual(attempts, [-1, -1, -1, 0])
        self.assertEqual([x[2] for x in self.waits], [100, 75, 50])
        self.assertEqual(self.release(self.ptr, handle), 0)

    def test_full_sender_can_resume_after_reset_without_new_objects(self):
        handle = self.new()
        value, result = c.c_uint32(1), c.c_uint32()
        self.assertEqual(self.send(self.ptr, handle, c.byref(value), 0, 0, False), 0)
        actions = []

        def wake(slot, sender, milliseconds):
            actions.append((sender, self.reset(self.ptr, handle)))
            return 0

        self.on_wait = wake
        value.value = 2
        self.assertEqual(self.send(self.ptr, handle, c.byref(value), 10, 1, False), 0)
        self.assertEqual(actions, [(True, 0)])
        self.assertEqual(self.receive(self.ptr, handle, c.byref(result), 0, False, False), 0)
        self.assertEqual(result.value, 2)

    def test_isr_forbidden_context_invalid_handles_and_peek_never_block(self):
        handle = self.new()
        value, result = c.c_uint32(7), c.c_uint32()
        for key in (None, 1, 0xDEADBEEF):
            self.assertEqual(self.receive(self.ptr, key, c.byref(result), 100, False, False), -1)
            self.assertEqual(self.release(self.ptr, key), -1)
        self.assertEqual(self.receive(self.ptr, handle, c.byref(result), 100, False, True), -1)
        self.allowed = False
        self.assertEqual(self.receive(self.ptr, handle, c.byref(result), 100, False, False), -1)
        self.assertEqual(self.send(self.ptr, handle, c.byref(value), 100, 0, True), 0)
        self.assertEqual(self.receive(self.ptr, handle, c.byref(result), 100, True, True), 0)
        self.assertEqual(result.value, 7)
        self.assertEqual(self.status(handle), (1, 0, 0))
        self.assertEqual(self.send(self.ptr, handle, c.byref(value), 100, 0, True), -1)
        self.assertEqual(self.waits, [])

    def test_semaphore_limits_counts_and_wrong_kind_operations(self):
        handle = self.new(kind=1, capacity=2, size=0, initial=1)
        self.assertEqual(self.status(handle), (1, 1, 1))
        self.assertEqual(self.send(self.ptr, handle, None, 0, 0, True), 0)
        self.assertEqual(self.send(self.ptr, handle, None, 100, 0, False), -1)
        self.assertEqual(self.status(handle), (2, 0, 1))
        self.assertEqual(self.receive(self.ptr, handle, None, 0, True, False), -1)
        self.assertEqual(self.receive(self.ptr, handle, None, 0, False, True), 0)
        self.assertEqual(self.receive(self.ptr, handle, None, 0, False, False), 0)
        self.assertEqual(self.receive(self.ptr, handle, None, 0, False, True), -1)
        self.assertEqual(self.reset(self.ptr, handle), -1)
        self.assertEqual(self.mutex(self.ptr, handle, 0, True, False), -1)
        self.assertEqual(self.waits, [])

    def test_mutex_ownership_recursion_overflow_and_release_guards(self):
        normal = self.new(kind=2, size=0)
        recursive = self.new(kind=3, size=0)
        self.assertEqual(self.receive(self.ptr, normal, None, 5, False, False), 0)
        self.assertEqual(self.lock_calls, [(24, 50)])
        self.assertEqual(self.receive(self.ptr, normal, None, 0, False, False), -1)
        self.assertEqual(self.release(self.ptr, normal), -1)
        self.owner = 2
        self.assertEqual(self.send(self.ptr, normal, None, 0, 0, False), -1)
        self.owner = 1
        self.assertEqual(self.send(self.ptr, normal, None, 0, 0, True), -1)
        self.assertEqual(self.send(self.ptr, normal, None, 0, 0, False), 0)
        self.assertEqual(self.release(self.ptr, normal), 0)
        self.assertEqual(self.mutex(self.ptr, recursive, 0, True, False), -1)
        self.assertEqual(self.mutex(self.ptr, recursive, 0, True, True), 0)
        self.assertEqual(self.mutex(self.ptr, recursive, 0, True, True), 0)
        self.assertEqual(self.pool.slots[25].depth, 2)
        self.assertEqual(self.mutex(self.ptr, recursive, 0, False, True), 0)
        self.assertEqual(self.release(self.ptr, recursive), -1)
        self.pool.slots[25].depth = 2**32-1
        self.assertEqual(self.mutex(self.ptr, recursive, 0, True, True), -1)
        self.pool.slots[25].depth = 1
        self.assertEqual(self.mutex(self.ptr, recursive, 0, False, True), 0)
        self.assertEqual(self.release(self.ptr, recursive), 0)
        self.assertEqual(self.lock_calls, [(24, 50), (25, 0)])

    def test_boot_mutexes_do_not_require_a_task_clock_or_native_lock(self):
        self.owner = 2**32 - 1
        self.allowed = False
        self.clock_ok = False
        normal = self.new(kind=2, size=0)
        recursive = self.new(kind=3, size=0)
        self.assertEqual(self.receive(self.ptr, normal, None, 2**32-1, False, False), 0)
        self.assertEqual(self.receive(self.ptr, normal, None, 0, False, False), -1)
        self.assertEqual(self.release(self.ptr, normal), -1)
        self.assertEqual(self.send(self.ptr, normal, None, 0, 0, True), -1)
        self.assertEqual(self.send(self.ptr, normal, None, 0, 0, False), 0)
        self.assertEqual(self.send(self.ptr, normal, None, 0, 0, False), -1)
        for _ in range(3):
            self.assertEqual(self.mutex(self.ptr, recursive, 2**32-1, True, True), 0)
        self.assertEqual(self.pool.slots[25].depth, 3)
        for _ in range(3):
            self.assertEqual(self.mutex(self.ptr, recursive, 0, False, True), 0)
        self.assertEqual(self.pool.slots[25].owner, 0)
        self.assertEqual(self.lock_calls, [])
        self.assertEqual(self.waits, [])
        self.owner, self.allowed, self.clock_ok = 1, True, True
        self.assertEqual(self.receive(self.ptr, normal, None, 0, False, False), 0)
        self.assertEqual(self.send(self.ptr, normal, None, 0, 0, False), 0)
        self.assertEqual(self.lock_calls, [(24, 0)])

    def test_retained_boot_lock_cannot_be_stolen_when_scheduler_starts(self):
        handle = self.new(kind=3, size=0)
        self.owner, self.allowed = 2**32-1, False
        self.assertEqual(self.mutex(self.ptr, handle, 0, True, True), 0)
        self.pool.slots[24].depth = 2**32-1
        self.assertEqual(self.mutex(self.ptr, handle, 0, True, True), -1)
        self.pool.slots[24].depth = 1
        self.owner, self.allowed = 1, True
        self.assertEqual(self.mutex(self.ptr, handle, 100, True, True), -1)
        self.assertEqual(self.mutex(self.ptr, handle, 0, False, True), -1)
        self.assertEqual(self.release(self.ptr, handle), -1)
        self.assertEqual(self.pool.slots[24].owner, 2**32-1)
        self.assertEqual(self.pool.slots[24].depth, 1)
        self.assertEqual(self.lock_calls, [])
        self.owner = 0
        self.assertEqual(self.mutex(self.ptr, handle, 0, False, True), -1)

    def test_port_failure_and_reinitialization_leave_existing_state_intact(self):
        handle = self.new()
        self.assertEqual(self.init(self.ptr, c.byref(self.ops), 1000), -1)
        self.assertEqual(self.pool.hz, 100)
        self.clock_ok = False
        result = c.c_uint32(13)
        self.assertEqual(self.receive(self.ptr, handle, c.byref(result), 10, False, False), -1)
        self.assertEqual(result.value, 13)
        self.assertEqual(self.waits, [])
        self.assertEqual(self.release(self.ptr, handle), 0)


if __name__ == '__main__':
    unittest.main()
