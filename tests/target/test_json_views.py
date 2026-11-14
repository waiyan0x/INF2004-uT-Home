"""Exercise compact JSON views against independent values and JSON grammar."""
import ctypes as c
import json
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class View(c.Structure):
    _fields_ = [(name, c.c_uint16) for name in ("start", "end", "next", "key")]


VIEW = c.POINTER(View)


class JsonViewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("gcc")
        objcopy = shutil.which("objcopy") or shutil.which("llvm-objcopy")
        if not compiler or not objcopy:
            raise AssertionError("GCC and objcopy are required")
        folder = tempfile.TemporaryDirectory(prefix="uthome-json-views-")
        cls.addClassCleanup(folder.cleanup)
        directory = Path(folder.name)
        cls.binary = directory / "views.dll"
        flags = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        if sys.platform != "win32":
            flags += ["-fPIC"]
        prefix = "_" if sys.platform == "darwin" else ""
        aliases = []
        for original, replacement in (("malloc", "allocate"), ("calloc", "zero"),
                                      ("realloc", "resize"), ("free", "release")):
            aliases += ["--redefine-sym", f"{prefix}{original}={prefix}heaptrap_{replacement}"]
        objects = []
        for name in ("cfgjson", "jsonscan"):
            obj = directory / (name + ".o")
            subprocess.run([compiler, *flags, "-c",
                            str(ROOT / f"firmware/src/config/{name}.c"),
                            "-o", str(obj)], check=True, capture_output=True)
            subprocess.run([objcopy, *aliases, str(obj)], check=True, capture_output=True)
            objects.append(str(obj))
        subprocess.run([compiler, *flags, "-shared", *objects,
                        str(ROOT / "tests/target/heaptrap.c"), "-o", str(cls.binary)],
                       check=True, capture_output=True)
        cls.lib = c.CDLL(str(cls.binary))
        if sys.platform == "win32":
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary, cls.lib._handle)
        signatures = {
            "cfgjson_init": ([], c.c_int),
            "cfgjson_open": ([c.c_void_p, c.c_size_t], VIEW),
            "cfgjson_close": ([], None),
            "cfgjson_peak": ([], c.c_size_t),
            "cfgjson_get": ([VIEW, c.c_char_p], VIEW),
            "cfgjson_at": ([VIEW, c.c_size_t], VIEW),
            "cfgjson_array": ([VIEW, c.c_size_t, c.POINTER(c.c_size_t)], c.c_bool),
            "cfgjson_object": ([VIEW, c.POINTER(c.c_char_p), c.c_size_t, c.c_uint32], c.c_bool),
            "cfgjson_is": ([VIEW, c.c_char_p], c.c_bool),
            "cfgjson_text": ([VIEW, c.c_void_p, c.c_size_t], c.c_bool),
            "cfgjson_name": ([VIEW, c.c_void_p], c.c_bool),
            "cfgjson_uint": ([VIEW, c.c_uint32, c.c_uint32, c.POINTER(c.c_uint32)], c.c_bool),
            "cfgjson_int": ([VIEW, c.c_int32, c.c_int32, c.POINTER(c.c_int32)], c.c_bool),
            "cfgjson_bool": ([VIEW, c.POINTER(c.c_bool)], c.c_bool),
            "heaptrap_count": ([], c.c_size_t),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(cls.lib, name)
            function.argtypes, function.restype = arguments, result
        if cls.lib.cfgjson_init() != 0:
            raise AssertionError("Parser initialization failed")

    def tearDown(self):
        self.lib.cfgjson_close()
        self.assertEqual(self.lib.heaptrap_count(), 0)

    def open(self, raw):
        self.lib.cfgjson_close()
        # Exactly sized input: the parser cannot assume a trailing terminator.
        self.input = c.create_string_buffer(raw, max(1, len(raw)))
        return self.lib.cfgjson_open(self.input, len(raw))

    def assert_value(self, view, expected):
        self.assertTrue(view)
        if isinstance(expected, dict):
            keys = (c.c_char_p * len(expected))(*(key.encode() for key in expected))
            self.assertTrue(self.lib.cfgjson_object(view, keys, len(keys), (1 << len(keys)) - 1))
            for key, item in expected.items():
                self.assert_value(self.lib.cfgjson_get(view, key.encode()), item)
            self.assertFalse(self.lib.cfgjson_get(view, b"absent-key"))
        elif isinstance(expected, list):
            count = c.c_size_t(99)
            self.assertTrue(self.lib.cfgjson_array(view, len(expected), c.byref(count)))
            self.assertEqual(count.value, len(expected))
            for index, item in enumerate(expected):
                self.assert_value(self.lib.cfgjson_at(view, index), item)
            self.assertFalse(self.lib.cfgjson_at(view, len(expected)))
        elif isinstance(expected, bool):
            value = c.c_bool(not expected)
            self.assertTrue(self.lib.cfgjson_bool(view, c.byref(value)))
            self.assertEqual(value.value, expected)
            integer = c.c_uint32(9)
            self.assertFalse(self.lib.cfgjson_uint(view, 0, 2**32-1, c.byref(integer)))
            self.assertEqual(integer.value, 9)
        elif isinstance(expected, int):
            if expected >= 0:
                value = c.c_uint32()
                self.assertTrue(self.lib.cfgjson_uint(view, 0, 2**32-1, c.byref(value)))
            else:
                value = c.c_int32()
                self.assertTrue(self.lib.cfgjson_int(view, -2**31, 2**31-1, c.byref(value)))
            self.assertEqual(value.value, expected)
        elif isinstance(expected, str):
            encoded = expected.encode("ascii")
            output = c.create_string_buffer(len(encoded) + 1)
            self.assertTrue(self.lib.cfgjson_text(view, output, len(output)))
            self.assertEqual(output.value, encoded)
            self.assertTrue(self.lib.cfgjson_is(view, encoded))
            self.assertFalse(self.lib.cfgjson_is(view, encoded + b"x"))
        else:
            self.assertIsNone(expected)
            number = c.c_uint32()
            flag = c.c_bool()
            count = c.c_size_t()
            self.assertFalse(self.lib.cfgjson_uint(view, 0, 2**32-1, c.byref(number)))
            self.assertFalse(self.lib.cfgjson_bool(view, c.byref(flag)))
            self.assertFalse(self.lib.cfgjson_array(view, 1, c.byref(count)))
            self.assertFalse(self.lib.cfgjson_is(view, b"null"))

    def test_nested_views_skip_subtrees_and_decode_escapes(self):
        raw = (br'{"\u0061":[{"\"\\/\b\f\n\r\t\u007f":"\"\\/\b\f\n\r\t\u007f"},'
               br'[],{},[null,false,true,0,-2147483648,4294967295]],"z":"end"}')
        self.assert_value(self.open(raw), json.loads(raw))

    def test_integer_ranges_wrong_types_and_output_preservation(self):
        root = self.open(b'{"min":-2147483648,"max":2147483647,"umax":4294967295,"zero":0}')
        signed = c.c_int32(99)
        unsigned = c.c_uint32(99)
        self.assertTrue(self.lib.cfgjson_int(self.lib.cfgjson_get(root, b"min"), -2**31, 2**31-1, c.byref(signed)))
        self.assertEqual(signed.value, -2**31)
        self.assertTrue(self.lib.cfgjson_int(self.lib.cfgjson_get(root, b"max"), -2**31, 2**31-1, c.byref(signed)))
        self.assertEqual(signed.value, 2**31-1)
        for key, low, high in ((b"umax", -2**31, 2**31-1), (b"zero", 1, 10), (b"max", 10, 1)):
            signed.value = 99
            self.assertFalse(self.lib.cfgjson_int(self.lib.cfgjson_get(root, key), low, high, c.byref(signed)))
            self.assertEqual(signed.value, 99)
        self.assertFalse(self.lib.cfgjson_uint(self.lib.cfgjson_get(root, b"min"), 0, 2**32-1, c.byref(unsigned)))
        self.assertEqual(unsigned.value, 99)
        self.assertFalse(self.lib.cfgjson_int(None, -2**31, 2**31-1, c.byref(signed)))
        self.assertFalse(self.lib.cfgjson_uint(root, 0, 100, c.byref(unsigned)))

    def test_duplicate_keys_and_invalid_string_encodings_are_rejected(self):
        cases = [br'{"a":0,"\u0061":1}', br'{"x":{"":0,"":1}}',
                 br'{"x":[{"a":0,"a":false}]}', br'{"x":"\u0000"}',
                 br'{"x":"\u0080"}', br'{"x":"\ud800"}', br'{"x":"\q"}',
                 br'{"x":"\u000"}', b'{"x":"\x7f"}', b'{"x":"\xff"}',
                 b'{"x":"\0"}', b'{"x":"unterminated}', b'{"x":"\\"}']
        for raw in cases:
            self.assertFalse(self.open(raw), raw)
        root = self.open(br'{"x":[0],"escaped":"\u007f","":{}}')
        array = self.lib.cfgjson_get(root, b"x")
        self.assertFalse(self.lib.cfgjson_at(array, c.c_size_t(-1).value))
        self.assertTrue(self.lib.cfgjson_is(self.lib.cfgjson_get(root, b"escaped"), b'\x7f'))

    def test_string_capacities_names_and_unknown_fields(self):
        root = self.open(b'{"name":"A_b.c-9","long":"abcdefghijklmnopqrstuvwxyzabcdef","empty":"","bad":"9x"}')
        output = c.create_string_buffer(32)
        self.assertTrue(self.lib.cfgjson_name(self.lib.cfgjson_get(root, b"name"), output))
        self.assertEqual(output.value, b"A_b.c-9")
        for key in (b"long", b"empty", b"bad"):
            output.value = b"unchanged"
            self.assertFalse(self.lib.cfgjson_name(self.lib.cfgjson_get(root, key), output))
            self.assertEqual(output.value, b"unchanged")
        value = self.lib.cfgjson_get(root, b"name")
        self.assertFalse(self.lib.cfgjson_text(value, output, 7))
        self.assertTrue(self.lib.cfgjson_text(value, output, 8))
        self.assertFalse(self.lib.cfgjson_text(value, None, 8))
        keys = (c.c_char_p * 1)(b"name")
        self.assertFalse(self.lib.cfgjson_object(root, keys, 1, 1))
        root = self.open(b'{"name":false}')
        self.assertTrue(self.lib.cfgjson_object(root, keys, 1, 1))
        self.assertFalse(self.lib.cfgjson_object(root, keys, 1, 3))
        self.assertFalse(self.lib.cfgjson_object(root, keys, 17, 0))

    def test_limits_failed_open_and_view_lifetime(self):
        raw = b'{"a":0}'
        root = self.open(raw)
        self.assertEqual(self.lib.cfgjson_peak(), 16)
        self.assertFalse(self.lib.cfgjson_open(None, 0))
        self.assert_value(root, {"a": 0})
        self.assertEqual(self.input.raw, raw)
        self.lib.cfgjson_close()
        self.assertFalse(self.lib.cfgjson_get(root, b"a"))
        self.assertEqual(self.lib.cfgjson_peak(), 16)
        # Work boundary: one root + three units per object number member.
        for count in (341, 342):
            raw = json.dumps({str(i): 0 for i in range(count)}, separators=(",", ":")).encode()
            self.assertEqual(bool(self.open(raw)), count == 341)
        for depth in (7, 8):
            raw = b'{"a":' + b'[' * depth + b'0' + b']' * depth + b'}'
            self.assertEqual(bool(self.open(raw)), depth == 7)
        for count in (510, 511):
            raw = json.dumps({"a": [False] * count}, separators=(",", ":")).encode()
            self.assertEqual(bool(self.open(raw)), count == 510)
            self.assertLessEqual(self.lib.cfgjson_peak(), 4096)
        raw = b'{"a":"' + b'x' * 8184 + b'"}'
        self.assertEqual(len(raw), 8192)
        self.assertTrue(self.open(raw))
        self.assertFalse(self.open(raw + b' '))

    def test_generated_values_match_python(self):
        rng = random.Random(2004)

        def generate(depth):
            choices = 4 if depth == 4 else 6
            selection = rng.randrange(choices)
            if selection == 0:
                return rng.choice([0, 1, -1, -2**31, 2**31-1, 2**32-1])
            if selection == 1:
                return rng.choice(["", "A", "a\"b\\c", "\n\t\b\f\r/\x7f"])
            if selection == 2:
                return rng.choice([True, False])
            if selection == 3:
                return None
            if selection == 4:
                return [generate(depth + 1) for _ in range(rng.randrange(4))]
            return {str(i): generate(depth + 1) for i in range(rng.randrange(4))}

        for index in range(500):
            expected = {"item": generate(0), "tail": index}
            raw = json.dumps(expected, separators=(",", ":")).encode()
            self.assert_value(self.open(raw), expected)

    def test_mutated_grammar_matches_independent_decoder(self):
        rng = random.Random(611)
        seeds = [b'{"a":[1,0,-1,true,false,null,{},[]],"b":"x\\n"}',
                 b'{"a":4294967295,"b":-2147483648}', b'{"":{},"b":[]}']

        def reject(_):
            raise ValueError("Unsupported value")

        def integer(token):
            value = int(token)
            if token == "-0" or not -2**31 <= value <= 2**32-1:
                raise ValueError("Unsupported integer")
            return value

        def pairs(items):
            result = {}
            for key, value in items:
                if key in result:
                    raise ValueError("Duplicate key")
                result[key] = value
            return result

        for _ in range(2000):
            raw = bytearray(rng.choice(seeds))
            index = rng.randrange(len(raw))
            if rng.randrange(2):
                raw[index] = rng.choice(b'{}[],:"012345tfnue -.+\n\t\\')
            else:
                del raw[index]
            try:
                expected = isinstance(json.loads(raw, parse_int=integer, parse_float=reject,
                                                 parse_constant=reject, object_pairs_hook=pairs), dict)
            except ValueError:
                expected = False
            self.assertEqual(bool(self.open(bytes(raw))), expected, bytes(raw))


if __name__ == "__main__":
    unittest.main()
