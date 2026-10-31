"""Independent SDK-format tables and boundary attacks against fixed storage."""
import ctypes
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

from test_qualification import qualification

ROOT = Path(__file__).resolve().parents[2]


def canonical_table():
    entries = []
    kinds = {"data": 1, "app": 0}
    subtypes = {"nvs": 2, "phy": 1, "factory": 0}
    for line in (ROOT / "tests/target/runtime_port_board/configuration.csv").read_text().splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        label, kind, subtype, offset, size, flags = [part.strip() for part in line.split(",")]
        if flags:
            raise AssertionError("The reviewed layout has no flags")
        entries.append(struct.pack("<HBBII16sI", 0x50aa,
                                   kinds[kind] if kind in kinds else int(kind, 0),
                                   subtypes[subtype] if subtype in subtypes else int(subtype, 0),
                                   int(offset, 0), int(size, 0), label.encode(), 0))
    raw = b"".join(entries)
    raw += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(raw).digest()
    return raw.ljust(3072, b"\xff")


def setUpModule():
    subprocess.run([sys.executable, str(ROOT / "tools/c_standard/check.py"), "--root", str(ROOT)],
                   check=True)


class LayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("gcc")
        if compiler is None:
            raise AssertionError("GCC is required")
        cls.folder = tempfile.TemporaryDirectory(prefix="uthome-layout-")
        cls.addClassCleanup(cls.folder.cleanup)
        cls.library_path = Path(cls.folder.name) / "cfgpart.dll"
        subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        "-shared", "-O2", str(ROOT / "firmware/src/config/cfgpart.c"),
                        "-o", str(cls.library_path)], check=True, capture_output=True)
        cls.library = ctypes.CDLL(str(cls.library_path))
        cls.check = cls.library.cfgpart_check
        cls.check.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.c_size_t]
        cls.check.restype = ctypes.c_bool
        cls.writable = cls.library.cfgpart_writable
        cls.writable.argtypes = [ctypes.c_size_t, ctypes.c_size_t]
        cls.writable.restype = ctypes.c_bool

    @classmethod
    def tearDownClass(cls):
        # Windows holds a loaded DLL open; release this test-only handle before
        # TemporaryDirectory cleans its own uniquely created directory.
        if sys.platform == "win32":
            import _ctypes
            _ctypes.FreeLibrary(cls.library._handle)

    def check_bytes(self, raw, offset=0):
        value = (ctypes.c_uint8 * len(raw)).from_buffer_copy(raw)
        return self.check(value, offset, len(raw))

    def test_canonical_sdk_layout_and_chunk_boundaries(self):
        raw = canonical_table()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), qualification.CONFIGURATION_TABLE_SHA256)
        self.assertTrue(self.check_bytes(raw))
        for chunk in (1, 7, 32, 127, 256, 3072):
            for offset in range(0, len(raw), chunk):
                self.assertTrue(self.check_bytes(raw[offset:offset+chunk], offset))

    def test_every_corrupted_table_byte_is_rejected(self):
        raw = bytearray(canonical_table())
        for index in range(len(raw)):
            raw[index] ^= 1
            self.assertFalse(self.check_bytes(raw), f"changed byte {index}")
            raw[index] ^= 1

    def test_rechecksummed_unreviewed_layout_is_rejected(self):
        for index in (2, 3, 4, 8, 12, 28, 96, 104):
            raw = bytearray(canonical_table())
            raw[index] ^= 1
            raw[144:160] = hashlib.md5(raw[:128]).digest()
            self.assertFalse(self.check_bytes(raw))

    def test_missing_and_overflowing_chunks_fail(self):
        value = (ctypes.c_uint8 * 1)(0xff)
        maximum = ctypes.c_size_t(-1).value
        self.assertFalse(self.check(None, 0, 1))
        for offset, size in ((0, 0), (3072, 1), (3071, 2), (maximum, 1), (1, maximum)):
            self.assertFalse(self.check(value, offset, size))

    def test_writes_cannot_escape_the_two_slots(self):
        maximum = ctypes.c_size_t(-1).value
        for address, size in ((0x110000, 0x6000), (0x113000, 0x3000), (0x115fff, 1), (0x116000, 0)):
            self.assertTrue(self.writable(address, size))
        for address, size in ((0, 1), (0x8000, 1), (0x9000, 1), (0x10000, 1),
                              (0x10ffff, 2), (0x110000, 0x6001), (0x116000, 1),
                              (maximum, 1), (0x110000, maximum)):
            self.assertFalse(self.writable(address, size))


class GuardScopeTests(unittest.TestCase):
    def test_both_guards_are_required_and_cannot_leak(self):
        source = Path("sdk/components/spi_flash/spi_flash_os_func_app.c").resolve()
        flags = ("cc -Desp_partition_is_flash_region_writable=c3store_region_allowed"
                 " -Desp_partition_main_flash_region_safe=c3store_region_allowed")
        self.assertEqual(str(source), qualification.verify_flash_guard({source: flags}))
        invalid = ({}, {source: "cc"}, {source: flags + " -Desp_partition_main_flash_region_safe=other"},
                   {source: flags, Path("other.c").resolve(): flags})
        for commands in invalid:
            with self.subTest(commands=commands), self.assertRaises(qualification.QualificationError):
                qualification.verify_flash_guard(commands)
