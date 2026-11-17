"""Actual fixed-output adapter with SDK call/allocator doubles, not TLS crypto."""
import ctypes as c
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
OUTPUT_BYTES = 4429
INPUT_BYTES = 16717


class Context(c.Structure):
    _fields_ = [('input', c.c_void_p), ('output', c.c_void_p)]


class Config(c.Structure):
    _fields_ = [('failure', c.c_uint32)]


class TlsOutputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('gcc')
        objcopy = shutil.which('objcopy') or shutil.which('llvm-objcopy')
        if not compiler or not objcopy:
            raise AssertionError('GCC and objcopy are required')
        cls.temp = tempfile.TemporaryDirectory(prefix='uthome-tls-output-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        (cls.directory / 'mbedtls').mkdir()
        # Foreign header spellings map to maintained, conventionally named test
        # types. The target build uses the full, unmodified SDK declarations.
        (cls.directory / 'mbedtls/ssl.h').write_text(
            '#include "touttest.h"\n#define MBEDTLS_PRIVATE(field) field\n')
        for header in ('mbedtls/platform.h', 'mbedtls/platform_util.h'):
            (cls.directory / header).write_text('#include "touttest.h"\n')
        (cls.directory / 'ssl_misc.h').write_text(
            '#define MBEDTLS_SSL_IN_BUFFER_LEN 16717\n'
            '#define MBEDTLS_SSL_OUT_BUFFER_LEN 4429\n')
        port = ROOT / 'firmware/hal/ports/esp32c3/network'
        flags = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
                 '-DUTHOME_NETWORK_WIFI=1', '-DUTHOME_STATIC_NET_QUEUES=1',
                 '-Dmbedtls_ssl_context=touttest_context_t',
                 '-Dmbedtls_ssl_config=touttest_config_t',
                 '-Din_buf=p_in_buf', '-Dout_buf=p_out_buf',
                 '-Dc3tout_native_setup=touttest_setup',
                 '-Dmbedtls_calloc=touttest_alloc',
                 '-Dmbedtls_free=touttest_free',
                 '-Dmbedtls_platform_zeroize=touttest_zeroize',
                 '-Dmbedtls_zeroize_and_free=touttest_wipe',
                 '-I' + str(cls.directory), '-I' + str(ROOT / 'tests/target'), '-I' + str(port)]
        if sys.platform != 'win32':
            flags += ['-fPIC']
        cls.command = [compiler, *flags, '-c', str(port / 'c3tout.c')]
        obj = cls.directory / 'output.o'
        subprocess.run([*cls.command, '-o', str(obj)], check=True, capture_output=True)
        prefix = '_' if sys.platform == 'darwin' else ''
        aliases = []
        for original, replacement in (('malloc', 'allocate'), ('calloc', 'zero'),
                                      ('realloc', 'resize'), ('free', 'release')):
            aliases += ['--redefine-sym', f'{prefix}{original}={prefix}heaptrap_{replacement}']
        subprocess.run([objcopy, *aliases, str(obj)], check=True, capture_output=True)
        cls.binary = cls.directory / 'output.dll'
        subprocess.run([compiler, *flags, '-shared', str(obj),
                        str(ROOT / 'tests/target/touttest.c'),
                        str(ROOT / 'tests/target/heaptrap.c'), '-o', str(cls.binary)],
                       check=True, capture_output=True)

    def setUp(self):
        path = self.directory / (self._testMethodName + '.dll')
        shutil.copyfile(self.binary, path)
        self.lib = c.CDLL(str(path))
        if sys.platform == 'win32':
            import _ctypes
            self.addCleanup(_ctypes.FreeLibrary, self.lib._handle)
        for name, args, result in (
                ('c3tout_setup', [c.POINTER(Context), c.POINTER(Config)], c.c_int),
                ('c3tout_alloc', [c.c_size_t, c.c_size_t], c.c_void_p),
                ('c3tout_free', [c.c_void_p], None),
                ('c3tout_wipe', [c.c_void_p, c.c_size_t], None),
                ('touttest_count', [c.c_uint32], c.c_uint32),
                ('heaptrap_count', [], c.c_size_t)):
            function = getattr(self.lib, name)
            function.argtypes, function.restype = args, result
        self.contexts = []

    def tearDown(self):
        for context in self.contexts:
            self.close(context)
        self.assertEqual(self.lib.heaptrap_count(), 0)

    def open(self, failure=0):
        context = Context()
        self.contexts.append(context)
        result = self.lib.c3tout_setup(c.byref(context), c.byref(Config(failure)))
        return result, context

    def close(self, context):
        for name, size in (('input', INPUT_BYTES), ('output', OUTPUT_BYTES)):
            pointer = getattr(context, name)
            if pointer:
                self.lib.c3tout_wipe(pointer, size)
                setattr(context, name, None)

    def test_one_session_exhaust_without_output_heap_fallback(self):
        contexts = [self.open()[1] for _ in range(1)]
        outputs = [context.output for context in contexts]
        self.assertTrue(all(outputs))
        self.assertEqual(len(set(outputs)), 1)
        self.assertEqual(self.lib.touttest_count(0), 1)  # inputs only
        for index, pointer in enumerate(outputs):
            self.assertEqual(pointer % c.alignment(c.c_uint64), 0)
            self.assertEqual(c.string_at(pointer, OUTPUT_BYTES), bytes(OUTPUT_BYTES))
            c.memset(pointer, index + 1, OUTPUT_BYTES)
        result, rejected = self.open()
        self.assertNotEqual(result, 0)
        self.assertFalse(rejected.input or rejected.output)
        self.assertEqual(self.lib.touttest_count(0), 2)  # rejected input freed
        self.close(contexts[0])
        self.assertEqual(c.string_at(outputs[0], OUTPUT_BYTES), bytes(OUTPUT_BYTES))
        result, replacement = self.open()
        self.assertEqual(result, 0)
        self.assertEqual(replacement.output, outputs[0])
        self.assertEqual(c.string_at(replacement.output, OUTPUT_BYTES), bytes(OUTPUT_BYTES))

    def test_setup_failures_release_slots_and_clear_selection_scope(self):
        for failure in (1, 2):
            for _ in range(20):
                result, context = self.open(failure)
                self.assertNotEqual(result, 0)
                self.assertFalse(context.input or context.output)
        before = self.lib.touttest_count(0)
        pointer = self.lib.c3tout_alloc(1, OUTPUT_BYTES)
        self.assertTrue(pointer)
        self.assertEqual(self.lib.touttest_count(0), before + 1)
        self.lib.c3tout_free(pointer)
        self.assertTrue(all(self.open()[0] == 0 for _ in range(1)))

    def test_same_size_later_in_setup_uses_sdk_not_another_output_slot(self):
        contexts = [self.open(3)[1] for _ in range(1)]
        self.assertTrue(all(context.output for context in contexts))
        self.assertEqual(self.lib.touttest_count(0), 2)  # inputs and later objects
        self.assertEqual(self.lib.touttest_count(3), 1)
        self.assertNotEqual(self.open()[0], 0)

    def test_both_cleanup_paths_erase_entire_buffer_without_sdk_free(self):
        for wipe in (False, True):
            result, context = self.open()
            self.assertEqual(result, 0)
            pointer = context.output
            c.memset(pointer, 0xA5, OUTPUT_BYTES)
            before = self.lib.touttest_count(1)
            if wipe:
                self.lib.c3tout_wipe(pointer, 1)  # owned capacity governs erasure
            else:
                self.lib.c3tout_free(pointer)
            context.output = None
            self.assertEqual(self.lib.touttest_count(1), before)
            self.assertEqual(c.string_at(pointer, OUTPUT_BYTES), bytes(OUTPUT_BYTES))
            self.close(context)

    def test_reconnect_cycles_reuse_all_storage(self):
        expected = None
        for _ in range(1000):
            contexts = [self.open()[1] for _ in range(1)]
            outputs = [context.output for context in contexts]
            self.assertTrue(all(outputs))
            if expected is None:
                expected = outputs
            self.assertEqual(outputs, expected)
            for context in reversed(contexts):
                self.close(context)

    def test_variable_buffers_are_rejected_before_build(self):
        result = subprocess.run([*self.command, '-DMBEDTLS_SSL_VARIABLE_BUFFER_LENGTH',
                                 '-o', str(self.directory / 'rejected.o')], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('without buffer resizing', result.stderr)


if __name__ == '__main__':
    unittest.main()
