"""Exercise the fixed controller engine with explicitly synthetic predictors."""
import ctypes as c
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PREDICT = c.CFUNCTYPE(c.c_int, c.POINTER(c.c_uint32), c.c_size_t)
HASH = c.c_uint8 * 32


class Identity(c.Structure):
    _fields_ = [('build', HASH), ('context', HASH), ('revision', c.c_uint32),
                ('crc', c.c_uint32), ('session', c.c_uint32)]


class Model(c.Structure):
    _fields_ = [('hash', HASH), ('build', HASH), ('contexts', c.POINTER(c.c_uint8)),
                ('context_count', c.c_size_t), ('predict', PREDICT), ('qualified', c.c_bool)]


class Request(c.Structure):
    _fields_ = [('identity', Identity), ('request_id', c.c_uint32),
                ('intervals', c.c_uint32 * 4), ('features', (c.c_uint32 * 18) * 4),
                ('count', c.c_uint32), ('admitted', c.c_bool)]


class Reply(c.Structure):
    _fields_ = [('identity', Identity), ('model_hash', HASH), ('request_id', c.c_uint32),
                ('interval', c.c_uint32), ('status', c.c_int)]


class State(c.Structure):
    _fields_ = [('request', Request), ('model', c.POINTER(Model)),
                ('started', c.c_uint64), ('last', c.c_uint64), ('cursor', c.c_uint32),
                ('supported', c.c_uint32), ('best', c.c_uint32), ('status', c.c_int)]


class AdviserTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        gcc = shutil.which('gcc')
        if gcc is None:
            raise AssertionError('GCC required')
        cls.temp = tempfile.TemporaryDirectory(prefix='uthome-adviser-')
        cls.addClassCleanup(cls.temp.cleanup)
        dll = Path(cls.temp.name) / 'adviser.dll'
        subprocess.run([gcc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2', '-shared',
                        str(ROOT / 'firmware/src/services/adviser.c'), '-o', str(dll)],
                       check=True, capture_output=True)
        cls.lib = c.CDLL(str(dll))
        cls.start = cls.lib.adviser_start
        cls.start.argtypes = [c.POINTER(State), c.POINTER(Model), c.POINTER(Request), c.c_uint64]
        cls.start.restype = c.c_int
        cls.step = cls.lib.adviser_step
        cls.step.argtypes = [c.POINTER(State), c.POINTER(Identity), c.c_uint64]
        cls.step.restype = c.c_int
        cls.reply = cls.lib.adviser_reply
        cls.reply.argtypes = [c.POINTER(State), c.POINTER(Identity), c.c_uint64, c.POINTER(Reply)]
        cls.reply.restype = c.c_int
        cls.cancel = cls.lib.adviser_cancel
        cls.cancel.argtypes = [c.POINTER(State)]
        cls.cancel.restype = None

    @classmethod
    def tearDownClass(cls):
        if sys.platform == 'win32':
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)

    def setUp(self):
        self.state = State()
        self.current = Identity(HASH(*range(32)), HASH(*range(32, 64)), 12, 456, 78)
        self.request = Request(identity=self.current, request_id=9, count=4, admitted=True)
        for i, interval in enumerate([1000, 200, 2000, 500]):
            self.request.intervals[i] = interval
            self.request.features[i][1] = interval
        self.calls = []
        self.return_value = None

        def predict(features, count):
            self.calls.append((list(features[:count]), count))
            return self.return_value if self.return_value is not None else int(features[1] <= 500)

        self.callback = PREDICT(predict)
        self.contexts = HASH(*range(32, 64))
        self.model = Model(HASH(*range(64, 96)), self.current.build, self.contexts, 1, self.callback, True)
        self.ptr = c.byref(self.state)
        self.identity = c.byref(self.current)

    def begin(self, now=1000):
        return self.start(self.ptr, c.byref(self.model), c.byref(self.request), now)

    def get(self, now=2000):
        reply = Reply()
        self.assertEqual(self.reply(self.ptr, self.identity, now, c.byref(reply)), 0)
        return reply

    def test_one_candidate_per_step_and_longest_passing_result(self):
        self.assertLessEqual(c.sizeof(State), 512)
        self.assertEqual(self.begin(), 1)
        for index in range(4):
            self.assertEqual(self.step(self.ptr, self.identity, 1100 + index * 100), 2 if index == 3 else 1)
            self.assertEqual(len(self.calls), index + 1)
            if index < 3:
                self.assertEqual(self.get(now=1100 + index * 100).interval, 0)
        reply = self.get()
        self.assertEqual((reply.status, reply.interval, reply.request_id), (2, 500, 9))
        self.assertEqual(bytes(reply.identity), bytes(self.current))
        self.assertEqual(bytes(reply.model_hash), bytes(self.model.hash))
        self.assertEqual(self.step(self.ptr, self.identity, 3000), 2)
        self.assertEqual(len(self.calls), 4)

    def test_busy_request_is_unchanged_and_input_is_copied(self):
        self.assertEqual(self.begin(), 1)
        before = bytes(self.state)
        self.request.request_id = 123
        self.request.features[0][1] = 42
        self.assertEqual(self.begin(), 1)
        self.assertEqual(bytes(self.state), before)
        self.assertEqual(self.step(self.ptr, self.identity, 1001), 1)
        self.assertEqual(self.calls[0][0][1], 1000)
        self.assertEqual(self.get().request_id, 9)

    def test_unqualified_missing_and_unknown_context_models_never_execute(self):
        self.model.qualified = False
        self.assertEqual(self.begin(), 4)
        self.assertEqual(self.step(self.ptr, self.identity, 1001), 4)
        self.model.qualified = True
        self.model.build[0] ^= 1
        self.assertEqual(self.begin(), 4)
        self.model.build[0] ^= 1
        self.contexts[0] ^= 1
        self.assertEqual(self.begin(), 4)
        self.assertEqual(self.start(self.ptr, None, c.byref(self.request), 1000), 4)
        self.assertEqual(self.get().interval, 0)
        self.assertEqual(self.calls, [])

    def test_every_identity_field_invalidates_pending_and_ready_advice(self):
        for field in ('build', 'context', 'revision', 'crc', 'session'):
            for completed in (False, True):
                with self.subTest(field=field, completed=completed):
                    self.setUp()
                    self.assertEqual(self.begin(), 1)
                    if completed:
                        for _ in range(4):
                            self.step(self.ptr, self.identity, 1100)
                    if field in ('build', 'context'):
                        getattr(self.current, field)[0] ^= 1
                    else:
                        setattr(self.current, field, getattr(self.current, field) + 1)
                    reply = self.get()
                    self.assertEqual((reply.status, reply.interval), (5, 0))
                    count = len(self.calls)
                    self.assertEqual(self.step(self.ptr, self.identity, 3000), 5)
                    self.assertEqual(len(self.calls), count)

    def test_deadline_expiry_and_clock_reversal_discard_partial_result(self):
        self.assertEqual(self.begin(), 1)
        self.assertEqual(self.step(self.ptr, self.identity, 100999), 1)
        self.assertEqual(self.step(self.ptr, self.identity, 101000), 6)
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(self.get(now=101001).interval, 0)
        self.assertEqual(self.begin(now=200000), 1)
        self.assertEqual(self.step(self.ptr, self.identity, 199999), 7)
        self.assertEqual(len(self.calls), 1)

    def test_ready_result_has_bounded_lifetime_and_disconnect_invalidates(self):
        self.begin()
        for _ in range(4):
            self.step(self.ptr, self.identity, 2000)
        self.assertEqual(self.get(now=30000999).status, 2)
        self.assertEqual(self.get(now=30001000).status, 6)
        self.assertEqual(self.begin(now=40000000), 1)
        self.cancel(self.ptr)
        self.assertEqual(self.get(now=40000001).status, 5)
        self.assertEqual(self.get(now=40000002).interval, 0)

    def test_all_fail_all_unknown_and_bad_predictor_return(self):
        for score, expected in ((0, 3), (-1, 4), (2, 7), (-2, 7)):
            with self.subTest(score=score):
                self.setUp()
                self.return_value = score
                self.assertEqual(self.begin(), 1)
                for _ in range(4):
                    self.step(self.ptr, self.identity, 2000)
                self.assertEqual((self.get().status, self.get().interval), (expected, 0))
                self.assertEqual(len(self.calls), 4 if score in (0, -1) else 1)

    def test_invalid_capacity_duplicate_and_unadmitted_requests_do_not_start(self):
        for field, value in (('count', 0), ('count', 5), ('admitted', False), ('request_id', 0)):
            with self.subTest(field=field, value=value):
                self.setUp()
                setattr(self.request, field, value)
                self.assertEqual(self.begin(), 8)
                self.assertEqual(bytes(self.state), bytes(State()))
        self.setUp()
        self.request.intervals[1] = self.request.features[1][1] = 1000
        self.assertEqual(self.begin(), 8)
        self.request.intervals[1] = 123
        self.assertEqual(self.begin(), 8)
        self.assertEqual(self.calls, [])

    def test_null_arguments_fail_without_callback(self):
        self.assertEqual(self.start(None, None, None, 0), 8)
        self.assertEqual(self.start(self.ptr, None, None, 0), 8)
        self.assertEqual(self.step(self.ptr, None, 0), 8)
        self.assertEqual(self.reply(self.ptr, None, 0, None), -1)
        self.cancel(None)
        self.assertEqual(self.calls, [])


if __name__ == '__main__':
    unittest.main()
