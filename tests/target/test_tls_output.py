"""Actual fixed-record adapter with SDK call/allocator doubles, not TLS crypto."""
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

    def test_one_session_exhaust_without_record_heap_fallback(self):
        result, context = self.open()
        self.assertEqual(result, 0)
        pointers = (context.input, context.output)
        self.assertTrue(all(pointers))
        self.assertEqual(len(set(pointers)), 2)
        self.assertEqual(self.lib.touttest_count(0), 0)
        for pointer, size in zip(pointers, (INPUT_BYTES, OUTPUT_BYTES)):
            self.assertEqual(pointer % c.alignment(c.c_uint64), 0)
            self.assertEqual(c.string_at(pointer, size), bytes(size))
            c.memset(pointer, 0xA5, size)
        result, rejected = self.open()
        self.assertNotEqual(result, 0)
        self.assertFalse(rejected.input or rejected.output)
        self.assertEqual(self.lib.touttest_count(0), 0)
        for pointer, size in zip(pointers, (INPUT_BYTES, OUTPUT_BYTES)):
            self.assertEqual(c.string_at(pointer, size), b'\xa5' * size)
        self.close(context)
        for pointer, size in zip(pointers, (INPUT_BYTES, OUTPUT_BYTES)):
            self.assertEqual(c.string_at(pointer, size), bytes(size))
        result, replacement = self.open()
        self.assertEqual(result, 0)
        self.assertEqual((replacement.input, replacement.output), pointers)

    def test_setup_failures_release_slots_and_clear_selection_scope(self):
        for failure in (1, 2, 4):
            for _ in range(20):
                result, context = self.open(failure)
                self.assertNotEqual(result, 0)
                self.assertFalse(context.input or context.output)
        before = self.lib.touttest_count(0)
        for size in (INPUT_BYTES, OUTPUT_BYTES):
            pointer = self.lib.c3tout_alloc(1, size)
            self.assertTrue(pointer)
            self.lib.c3tout_free(pointer)
        self.assertEqual(self.lib.touttest_count(0), before + 2)
        self.assertEqual(self.open()[0], 0)

    def test_same_size_later_in_setup_uses_sdk_not_record_slots(self):
        result, context = self.open(3)
        self.assertEqual(result, 0)
        self.assertTrue(context.input and context.output)
        self.assertEqual(self.lib.touttest_count(0), 2)  # later objects only
        self.assertEqual(self.lib.touttest_count(3), 2)
        self.assertNotEqual(self.open()[0], 0)

    def test_both_cleanup_paths_erase_entire_buffer_without_sdk_free(self):
        for wipe in (False, True):
            result, context = self.open()
            self.assertEqual(result, 0)
            for name, size in (('input', INPUT_BYTES), ('output', OUTPUT_BYTES)):
                pointer = getattr(context, name)
                c.memset(pointer, 0xA5, size)
                before = self.lib.touttest_count(1)
                if wipe:
                    self.lib.c3tout_wipe(pointer, 1)  # owned capacity governs erasure
                else:
                    self.lib.c3tout_free(pointer)
                setattr(context, name, None)
                self.assertEqual(self.lib.touttest_count(1), before)
                self.assertEqual(c.string_at(pointer, size), bytes(size))

    def test_output_exhaustion_returns_new_input_lease(self):
        result, owner = self.open()
        self.assertEqual(result, 0)
        pointer = owner.input
        self.lib.c3tout_free(pointer)
        owner.input = None
        c.memset(owner.output, 0x5A, OUTPUT_BYTES)
        result, rejected = self.open()
        self.assertNotEqual(result, 0)
        self.assertFalse(rejected.input or rejected.output)
        self.assertEqual(self.lib.touttest_count(0), 0)
        self.assertEqual(c.string_at(owner.output, OUTPUT_BYTES), b'\x5a' * OUTPUT_BYTES)
        self.close(owner)
        result, replacement = self.open()
        self.assertEqual(result, 0)
        self.assertEqual(replacement.input, pointer)

    def test_unrelated_allocation_shapes_keep_sdk_path(self):
        result, context = self.open(5)
        self.assertEqual(result, 0)
        self.assertTrue(context.input and context.output)
        self.assertEqual(self.lib.touttest_count(0), 3)
        for count, size in ((2, INPUT_BYTES), (2, OUTPUT_BYTES), (1, 17)):
            pointer = self.lib.c3tout_alloc(count, size)
            self.assertTrue(pointer)
            self.lib.c3tout_wipe(pointer, count * size)
        self.assertEqual(self.lib.touttest_count(0), 6)
        self.assertEqual(self.lib.touttest_count(3), 3)

    def test_reconnect_cycles_reuse_all_storage(self):
        expected = None
        for _ in range(1000):
            result, context = self.open()
            self.assertEqual(result, 0)
            pointers = (context.input, context.output)
            self.assertTrue(all(pointers))
            if expected is None:
                expected = pointers
            self.assertEqual(pointers, expected)
            self.close(context)
        self.assertEqual(self.lib.touttest_count(0), 0)

    def test_variable_buffers_are_rejected_before_build(self):
        result = subprocess.run([*self.command, '-DMBEDTLS_SSL_VARIABLE_BUFFER_LENGTH',
                                 '-o', str(self.directory / 'rejected.o')], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('without buffer resizing', result.stderr)


if __name__ == '__main__':
    unittest.main()
