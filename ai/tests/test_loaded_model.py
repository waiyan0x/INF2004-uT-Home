"""Verify the isolated target corpus against its frozen Python model."""
import ctypes as c
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import struct
import tempfile
import unittest

from ai.protocol_model import predict
from ai.timing import digest

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tests/target/runtime_port_board/loadmodel'


class LoadedModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        gcc = shutil.which('gcc')
        if gcc is None:
            raise AssertionError('GCC required')
        cls.temp = tempfile.TemporaryDirectory(prefix='uthome-model-fixture-')
        cls.addClassCleanup(cls.temp.cleanup)
        library = Path(cls.temp.name) / ('fixture.dll' if sys.platform == 'win32' else 'fixture.so')
        subprocess.run([gcc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-DUTHOME_MODEL_LOAD_PROBE=1', '-Dc3clock_now=loadfake_now', '-Dcostview_observe=loadfake_cost',
                        *[item for folder in ('firmware/include','firmware/src/runtime','firmware/src/config','firmware/src/services','firmware/hal/ports/esp32c3','tests/ai') for item in ('-I', str(ROOT/folder))],
                        '-O2', '-shared', *(['-Wl,--export-all-symbols'] if sys.platform == 'win32' else ['-fPIC']),
                        '-I', str(FIXTURE),
                        str(FIXTURE/'ailite.c'), str(FIXTURE/'aivects.c'),
                        str(FIXTURE/'aiload.c'), str(ROOT/'tests/ai/loadfake.c'), str(ROOT/'firmware/src/config/cfgbytes.c'),
                        '-o', str(library)], check=True, capture_output=True)
        cls.lib = c.CDLL(str(library))
        if sys.platform == 'win32':
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary, cls.lib._handle)
        cls.lib.aivects_count.restype = c.c_uint32
        cls.lib.aivects_get.argtypes = [c.c_uint32, c.POINTER(c.c_int32)]
        cls.lib.aivects_get.restype = c.POINTER(c.c_uint32)
        cls.lib.ailite_predict.argtypes = [c.POINTER(c.c_uint32), c.c_size_t]
        cls.lib.ailite_predict.restype = c.c_int
        cls.lib.loadfake_set.argtypes = [c.c_uint64]
        cls.lib.aiload_observe.argtypes = [c.c_void_p, c.POINTER(c.c_uint8), c.c_size_t, c.POINTER(c.c_size_t)]
        cls.manifest = json.loads((FIXTURE/'fixture.json').read_text())
        cls.model = json.loads((FIXTURE/'model.json').read_text())

    def test_frozen_identity_and_sources(self):
        self.assertEqual(digest(self.model), self.manifest['model_sha256'])
        self.assertFalse(self.manifest['deployment_allowed'])
        self.assertFalse(self.model['deployment_allowed'])
        for name, expected in self.manifest['source_sha256'].items():
            self.assertEqual(hashlib.sha256((FIXTURE/name).read_bytes()).hexdigest(), expected, name)

    def test_all_target_vectors_match_python_and_export(self):
        count = self.lib.aivects_count()
        self.assertEqual(count, self.manifest['vectors'])
        for index in range(count):
            expected = c.c_int32(-99)
            pointer = self.lib.aivects_get(index, c.byref(expected))
            self.assertTrue(pointer)
            vector = list(pointer[:18])
            with self.subTest(vector=index):
                self.assertEqual(expected.value, predict(self.model, vector))
                self.assertEqual(self.lib.ailite_predict(pointer, 18), expected.value)

    def test_invalid_corpus_access_and_feature_shape_fail_closed(self):
        expected = c.c_int32(-99)
        self.assertFalse(self.lib.aivects_get(self.lib.aivects_count(), c.byref(expected)))
        self.assertFalse(self.lib.aivects_get(0, None))
        self.assertEqual(self.lib.ailite_predict(None, 18), -1)
        vector = (c.c_uint32 * 18)()
        for size in (0, 17, 19):
            self.assertEqual(self.lib.ailite_predict(vector, size), -1)


    def test_phase_boundaries_bounded_batches_and_clock_reversal(self):
        data = (c.c_uint8 * 384)()
        size = c.c_size_t()
        call = lambda: self.lib.aiload_observe(c.c_void_p(1), data, 384, c.byref(size))
        self.assertEqual(self.lib.aiload_observe(None, data, 384, c.byref(size)), -1)
        self.lib.loadfake_set(100)
        self.assertEqual(call(), 0)
        self.assertEqual(struct.unpack_from('<4I', bytes(data), 80), (0, 0, 0, 0))
        self.assertEqual(struct.unpack_from('<I', bytes(data))[0], 0x41494c31)
        for phase, total in [(1, 4), (2, 4), (3, 8), (4, 8)]:
            self.lib.loadfake_set(100 + phase * 75000000)
            self.assertEqual(call(), 0)
            self.assertEqual(size.value, 384)
            self.assertEqual(struct.unpack_from('<4I', bytes(data), 80),
                             (phase, total, 5 if phase in (1, 3) else 0, 0))
        self.lib.loadfake_set(99)
        self.assertEqual(call(), -1)


if __name__ == '__main__':
    unittest.main()
