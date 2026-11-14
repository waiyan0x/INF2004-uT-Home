"""Fixed ESP-TLS records; SDK declarations are doubled, TLS itself is not simulated."""
import ctypes as c
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADER = r"""/**
 * @file esp_tls.h
 * @brief Minimal foreign SDK types; target qualification uses the real headers.
 */
#ifndef UTHOME_TEST_ESP_TLS_H
#define UTHOME_TEST_ESP_TLS_H
#include <stddef.h>
#include <stdint.h>
#define ESP_OK              0
#define ESP_ERR_INVALID_ARG 0x102
typedef int esp_err_t;

typedef struct
{
    uint64_t words[16];
} esp_tls_t;

typedef enum
{
    ESP_TLS_ERR_TYPE_UNKNOWN = 0,
    ESP_TLS_ERR_TYPE_SYSTEM,
    ESP_TLS_ERR_TYPE_MBEDTLS,
    ESP_TLS_ERR_TYPE_MBEDTLS_CERT_FLAGS,
    ESP_TLS_ERR_TYPE_ESP,
    ESP_TLS_ERR_TYPE_CUSTOM_STACK,
    ESP_TLS_ERR_TYPE_CUSTOM_STACK_CERT_FLAGS,
    ESP_TLS_ERR_TYPE_MAX
} esp_tls_error_type_t;

typedef struct esp_tls_last_error
{
    esp_err_t last_error;
    int       esp_tls_error_code;
    int       esp_tls_flags;
} esp_tls_last_error_t;

typedef esp_tls_last_error_t * esp_tls_error_handle_t;
#endif

/* End of file. */

"""
PRIVATE = r"""/**
 * @file esp_tls_private.h
 * @brief Forward the opaque host type; no SDK connection logic is doubled.
 */
#ifndef UTHOME_TEST_ESP_TLS_PRIVATE_H
#define UTHOME_TEST_ESP_TLS_PRIVATE_H
#include "esp_tls.h"
#endif

/* End of file. */

"""


class TlsStorageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("gcc")
        objcopy = shutil.which("objcopy") or shutil.which("llvm-objcopy")
        if not compiler or not objcopy:
            raise AssertionError("GCC and objcopy are required")
        cls.temp = tempfile.TemporaryDirectory(prefix="uthome-tls-records-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        (cls.directory / "esp_tls.h").write_text(HEADER)
        (cls.directory / "esp_tls_private.h").write_text(PRIVATE)
        port = ROOT / "firmware/hal/ports/esp32c3/network"
        flags = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        if sys.platform != "win32":
            flags += ["-fPIC"]
        obj = cls.directory / "c3tls.o"
        subprocess.run([compiler, *flags, "-DUTHOME_NETWORK_WIFI=1",
                        "-DUTHOME_STATIC_NET_QUEUES=1", "-I" + str(cls.directory),
                        "-I" + str(port), "-c", str(port / "c3tls.c"), "-o", str(obj)],
                       check=True, capture_output=True)
        prefix = "_" if sys.platform == "darwin" else ""
        aliases = []
        for original, replacement in (("malloc", "allocate"), ("calloc", "zero"),
                                      ("realloc", "resize"), ("free", "release")):
            aliases += ["--redefine-sym", f"{prefix}{original}={prefix}heaptrap_{replacement}"]
        subprocess.run([objcopy, *aliases, str(obj)], check=True, capture_output=True)
        cls.binary = cls.directory / "records.dll"
        subprocess.run([compiler, *flags, "-shared", str(obj),
                        str(ROOT / "tests/target/heaptrap.c"), "-o", str(cls.binary)],
                       check=True, capture_output=True)

    def setUp(self):
        path = self.directory / (self._testMethodName + ".dll")
        shutil.copyfile(self.binary, path)
        self.lib = c.CDLL(str(path))
        if sys.platform == "win32":
            import _ctypes
            self.addCleanup(_ctypes.FreeLibrary, self.lib._handle)
        signatures = {
            "c3tls_alloc": ([c.c_size_t, c.c_size_t], c.c_void_p),
            "c3tls_free": ([c.c_void_p], None),
            "c3tls_dup": ([c.c_void_p, c.c_size_t], c.c_void_p),
            "c3tls_error_create": ([], c.c_void_p),
            "c3tls_error_destroy": ([c.c_void_p], None),
            "c3tls_error_capture": ([c.c_void_p, c.c_uint32, c.c_int], None),
            "c3tls_error_get": ([c.c_void_p, c.c_int, c.POINTER(c.c_int)], c.c_int),
            "heaptrap_count": ([], c.c_size_t),
        }
        for name, (args, result) in signatures.items():
            function = getattr(self.lib, name)
            function.argtypes, function.restype = args, result

    def tearDown(self):
        self.assertEqual(self.lib.heaptrap_count(), 0)

    def test_three_contexts_exhaust_reuse_and_zero_storage(self):
        contexts = [self.lib.c3tls_alloc(1, 128) for _ in range(3)]
        self.assertEqual(len(set(contexts)), 3)
        for context in contexts:
            self.assertTrue(context)
            self.assertEqual(context % c.alignment(c.c_uint64), 0)
            self.assertEqual(c.string_at(context, 128), bytes(128))
            c.memset(context, 0xA5, 128)
        self.assertFalse(self.lib.c3tls_alloc(1, 128))
        self.lib.c3tls_free(contexts[1])
        self.assertEqual(c.string_at(contexts[1], 128), bytes(128))
        self.assertEqual(self.lib.c3tls_alloc(1, 128), contexts[1])
        self.assertEqual(c.string_at(contexts[0], 128), bytes([0xA5]) * 128)
        self.assertFalse(self.lib.c3tls_alloc(1, 128))

    def test_bad_sizes_and_foreign_release_preserve_full_pool(self):
        for count, size in ((0, 128), (2, 64), (1, 127), (1, 129),
                            (c.c_size_t(-1).value, 128), (1, c.c_size_t(-1).value)):
            self.assertFalse(self.lib.c3tls_alloc(count, size))
        contexts = [self.lib.c3tls_alloc(1, 128) for _ in range(3)]
        stranger = c.create_string_buffer(16)
        for address in (None, c.addressof(stranger), contexts[0] + 8):
            self.lib.c3tls_free(address)
        self.assertFalse(self.lib.c3tls_alloc(1, 128))
        self.lib.c3tls_free(contexts[0])
        self.lib.c3tls_free(contexts[0])
        self.assertEqual(self.lib.c3tls_alloc(1, 128), contexts[0])
        self.assertFalse(self.lib.c3tls_alloc(1, 128))

    def test_hostname_capacity_truncation_and_exclusive_lifetime(self):
        self.assertFalse(self.lib.c3tls_dup(None, 0))
        self.assertFalse(self.lib.c3tls_dup(b"x", 254))
        self.assertFalse(self.lib.c3tls_dup(b"x", c.c_size_t(-1).value))
        for raw, limit, expected in ((b"", 0, b""), (b"abc", 2, b"ab"),
                                      (b"a\0bc", 4, b"a"), (b"x" * 253, 253, b"x" * 253)):
            pointer = self.lib.c3tls_dup(raw, limit)
            self.assertTrue(pointer)
            self.assertEqual(c.string_at(pointer), expected)
            self.assertFalse(self.lib.c3tls_dup(b"next", 4))
            self.assertEqual(c.string_at(pointer), expected)
            self.lib.c3tls_free(pointer)
            self.assertEqual(c.string_at(pointer, 254), bytes(254))

    def test_error_categories_preserve_public_fields_and_clear_independently(self):
        error = self.lib.c3tls_error_create()
        for category, value in ((1, -10), (2, -20), (3, 30), (4, -40)):
            self.lib.c3tls_error_capture(error, category, value)
        public = (c.c_int * 3).from_address(error)
        self.assertEqual(list(public), [-40, -20, 30])
        value = c.c_int(99)
        for category, expected in ((1, -10), (5, -20), (6, 30), (4, -40)):
            self.assertEqual(self.lib.c3tls_error_get(error, category, c.byref(value)), 0)
            self.assertEqual(value.value, expected)
            self.assertEqual(self.lib.c3tls_error_get(error, category, c.byref(value)), 0)
            self.assertEqual(value.value, 0)
        self.lib.c3tls_error_capture(error, 2, 0)
        self.assertEqual(list(public), [0, 0, 0])

    def test_error_exhaustion_invalid_calls_and_failure_cleanup(self):
        errors = [self.lib.c3tls_error_create() for _ in range(3)]
        self.assertEqual(len(set(errors)), 3)
        self.assertFalse(self.lib.c3tls_error_create())
        context = self.lib.c3tls_alloc(1, 128)
        # SDK init frees its context when no error tracker is available.
        self.lib.c3tls_free(context)
        self.assertEqual(self.lib.c3tls_alloc(1, 128), context)
        value = c.c_int(99)
        foreign = c.create_string_buffer(16)
        for error, kind in ((None, 1), (c.addressof(foreign), 1),
                            (errors[0], -1), (errors[0], 0), (errors[0], 7)):
            self.lib.c3tls_error_capture(error, kind, -5)
            self.assertEqual(self.lib.c3tls_error_get(error, kind, c.byref(value)), 0x102)
            self.assertEqual(value.value, 99)
        self.lib.c3tls_error_capture(errors[0], 4, -123)
        self.assertEqual(self.lib.c3tls_error_get(errors[0], 4, None), 0x102)
        self.assertEqual(self.lib.c3tls_error_get(errors[0], 4, c.byref(value)), 0)
        self.assertEqual(value.value, -123)
        self.lib.c3tls_error_destroy(errors[1])
        self.assertEqual(self.lib.c3tls_error_get(errors[1], 1, c.byref(value)), 0x102)
        self.assertEqual(self.lib.c3tls_error_create(), errors[1])

    def test_repeated_full_lifecycle_reuses_all_records(self):
        expected = None
        for iteration in range(1000):
            contexts = [self.lib.c3tls_alloc(1, 128) for _ in range(3)]
            errors = [self.lib.c3tls_error_create() for _ in range(3)]
            self.assertTrue(all(contexts) and all(errors))
            self.assertEqual(len(set(contexts + errors)), 6)
            if expected is None:
                expected = contexts + errors
            self.assertEqual(contexts + errors, expected)
            self.assertFalse(self.lib.c3tls_alloc(1, 128))
            self.assertFalse(self.lib.c3tls_error_create())
            for context, error in zip(contexts, errors):
                name = self.lib.c3tls_dup(b"broker.example", 14)
                self.assertTrue(name)
                self.lib.c3tls_free(name)
                self.lib.c3tls_error_capture(error, 4, iteration)
                self.lib.c3tls_error_destroy(error)
                self.lib.c3tls_free(context)


if __name__ == "__main__":
    unittest.main()
