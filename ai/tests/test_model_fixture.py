"""Verify the isolated target corpus against its frozen Python model."""
import ctypes as c
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from ai.protocol_model import predict
from ai.timing import digest

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tests/target/runtime_port_board/model'


class ModelFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        gcc = shutil.which('gcc')
        if gcc is None:
            raise AssertionError('GCC required')
        cls.temp = tempfile.TemporaryDirectory(prefix='uthome-model-fixture-')
        cls.addClassCleanup(cls.temp.cleanup)
        library = Path(cls.temp.name) / ('fixture.dll' if sys.platform == 'win32' else 'fixture.so')
        subprocess.run([gcc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-O2', '-shared', *(['-Wl,--export-all-symbols'] if sys.platform == 'win32' else ['-fPIC']),
                        '-I', str(FIXTURE),
                        str(FIXTURE/'aifrozen.c'), str(FIXTURE/'aicases.c'),
                        '-o', str(library)], check=True, capture_output=True)
        cls.lib = c.CDLL(str(library))
        if sys.platform == 'win32':
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary, cls.lib._handle)
        cls.lib.aicases_count.restype = c.c_uint32
        cls.lib.aicases_get.argtypes = [c.c_uint32, c.POINTER(c.c_int32)]
        cls.lib.aicases_get.restype = c.POINTER(c.c_uint32)
        cls.lib.aifrozen_predict.argtypes = [c.POINTER(c.c_uint32), c.c_size_t]
        cls.lib.aifrozen_predict.restype = c.c_int
        cls.manifest = json.loads((FIXTURE/'fixture.json').read_text())
        cls.model = json.loads((FIXTURE/'model.json').read_text())

    def test_frozen_identity_and_sources(self):
        self.assertEqual(digest(self.model), self.manifest['model_sha256'])
        self.assertFalse(self.manifest['deployment_allowed'])
        self.assertFalse(self.manifest['statistical_gate_passed'])
        for name, expected in self.manifest['source_sha256'].items():
            self.assertEqual(hashlib.sha256((FIXTURE/name).read_bytes()).hexdigest(), expected, name)

    def test_all_target_vectors_match_python_and_export(self):
        count = self.lib.aicases_count()
        self.assertEqual(count, self.manifest['vectors'])
        for index in range(count):
            expected = c.c_int32(-99)
            pointer = self.lib.aicases_get(index, c.byref(expected))
            self.assertTrue(pointer)
            vector = list(pointer[:18])
            with self.subTest(vector=index):
                self.assertEqual(expected.value, predict(self.model, vector))
                self.assertEqual(self.lib.aifrozen_predict(pointer, 18), expected.value)

    def test_invalid_corpus_access_and_feature_shape_fail_closed(self):
        expected = c.c_int32(-99)
        self.assertFalse(self.lib.aicases_get(self.lib.aicases_count(), c.byref(expected)))
        self.assertFalse(self.lib.aicases_get(0, None))
        self.assertEqual(self.lib.aifrozen_predict(None, 18), -1)
        vector = (c.c_uint32 * 18)()
        for size in (0, 17, 19):
            self.assertEqual(self.lib.aifrozen_predict(vector, size), -1)


if __name__ == '__main__':
    unittest.main()
