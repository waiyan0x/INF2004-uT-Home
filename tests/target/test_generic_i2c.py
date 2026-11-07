"""Host tests for fixed-storage reviewed I2C register reads."""
import ctypes as c
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
U8P = c.POINTER(c.c_uint8)


class Request(c.Structure):
    _fields_ = [("address", c.c_uint8), ("write", U8P), ("write_size", c.c_size_t),
                ("read", U8P), ("read_size", c.c_size_t), ("deadline", c.c_uint64),
                ("pause_ms", c.c_uint32)]


TRANSFER = c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(Request))
NOW = c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(c.c_uint64))
DELAY = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_uint32, c.c_uint64)
SET = c.CFUNCTYPE(c.c_int, c.c_void_p, c.c_bool)


class Operations(c.Structure):
    _fields_ = [("transfer", TRANSFER), ("now", NOW), ("delay", DELAY)]


class Bus(c.Structure):
    _fields_ = [("operations", c.POINTER(Operations)), ("context", c.c_void_p),
                ("claims", c.c_uint8 * 16)]


class Recipe(c.Structure):
    _fields_ = [("address", c.c_uint8), ("reg", c.c_uint8), ("size", c.c_uint8), ("pause_ms", c.c_uint8)]


class Sensor(c.Structure):
    _fields_ = [("bus", c.POINTER(Bus)), ("recipe", Recipe),
                ("timeout", c.c_uint32), ("claimed", c.c_bool)]


class DigitalOutput(c.Structure):
    _fields_ = [("set", SET), ("context", c.c_void_p)]


class Light(c.Structure):
    _fields_ = [("sensor", Sensor), ("enable", c.POINTER(DigitalOutput)),
                ("ready", c.c_uint64), ("high", c.c_bool), ("started", c.c_bool)]


class GenericI2CTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        gcc = shutil.which("gcc")
        if gcc is None:
            raise AssertionError("GCC required")
        cls.temp = tempfile.TemporaryDirectory(prefix="uthome-generic-i2c-")
        cls.addClassCleanup(cls.temp.cleanup)
        library = Path(cls.temp.name) / ("geni2c.dll" if sys.platform == "win32" else "geni2c.so")
        subprocess.run([gcc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic", "-shared",
                        *([] if sys.platform == "win32" else ["-fPIC"]),
                        "-I", str(ROOT / "firmware/hal/include"),
                        str(ROOT / "firmware/hal/i2cbus.c"),
                        str(ROOT / "firmware/drivers/generic_i2c/geni2c.c"),
                        str(ROOT / "firmware/drivers/generic_i2c/senlux.c"), "-o", str(library)],
                       check=True, capture_output=True)
        cls.lib = c.CDLL(str(library))
        if sys.platform == "win32":
            import _ctypes
            cls.addClassCleanup(_ctypes.FreeLibrary, cls.lib._handle)
        cls.lib.geni2c_open.argtypes = [c.POINTER(Sensor), c.POINTER(Bus), c.POINTER(Recipe), c.c_uint32]
        cls.lib.geni2c_read.argtypes = [c.POINTER(Sensor), U8P, c.c_size_t]
        cls.lib.geni2c_close.argtypes = [c.POINTER(Sensor)]
        cls.lib.geni2c_scale_le.argtypes = [U8P, c.c_size_t, c.c_uint32, c.c_uint32,
                                            c.c_uint32, c.POINTER(c.c_uint32)]
        cls.lib.i2cbus_transfer.argtypes = [c.POINTER(Bus), c.POINTER(Request)]
        cls.lib.senlux_open.argtypes = [c.POINTER(Light), c.POINTER(Bus), c.POINTER(DigitalOutput), c.c_uint32]
        cls.lib.senlux_read.argtypes = [c.POINTER(Light), c.POINTER(c.c_uint32)]
        cls.lib.senlux_close.argtypes = [c.POINTER(Light)]

    def setUp(self):
        self.calls = []
        self.failure = 0
        self.current_ms = 100
        self.enable_calls = []
        self.enable_failure = 0
        self.enable_set = SET(lambda context, high: self._set_enable(high))
        self.enable = DigitalOutput(self.enable_set, 1)
        def transfer(context, pointer):
            request = pointer.contents
            self.calls.append((request.address, request.write[0], request.write_size,
                               request.read_size, request.deadline, request.pause_ms))
            for index in range(request.read_size): request.read[index] = index + 1
            return self.failure
        self.transfer = TRANSFER(transfer)
        self.now = NOW(lambda context, value: self._set_now(value))
        self.delay = DELAY(lambda context, duration, deadline: 0)
        self.ops = Operations(self.transfer, self.now, self.delay)
        self.bus = Bus(c.pointer(self.ops), 1, (c.c_uint8 * 16)())
        self.recipe = Recipe(0x4A, 0, 4, 0)
        self.sensor = Sensor()

    def _set_now(self, value):
        value[0] = self.current_ms
        return 0

    def _set_enable(self, high):
        self.enable_calls.append(high)
        return self.enable_failure

    def open(self):
        return self.lib.geni2c_open(c.byref(self.sensor), c.byref(self.bus), c.byref(self.recipe), 20)

    def test_read_uses_one_deadline_and_claim_then_release(self):
        self.assertEqual(self.open(), 0)
        self.assertEqual(self.calls, [])
        another = Sensor()
        self.assertNotEqual(self.lib.geni2c_open(c.byref(another), c.byref(self.bus), c.byref(self.recipe), 20), 0)
        result = (c.c_uint8 * 4)()
        self.assertEqual(self.lib.geni2c_read(c.byref(self.sensor), result, 4), 0)
        self.assertEqual(list(result), [1, 2, 3, 4])
        self.assertEqual(self.calls, [(0x4A, 0, 1, 4, 120, 0)])
        self.assertEqual(self.lib.geni2c_close(c.byref(self.sensor)), 0)
        self.assertEqual(self.lib.geni2c_close(c.byref(self.sensor)), 0)
        self.assertEqual(self.lib.geni2c_open(c.byref(another), c.byref(self.bus), c.byref(self.recipe), 20), 0)
        self.assertEqual(self.lib.geni2c_close(c.byref(another)), 0)

    def test_failed_transfer_never_exposes_partial_data(self):
        self.assertEqual(self.open(), 0)
        self.failure = -3
        result = (c.c_uint8 * 4)(9, 9, 9, 9)
        self.assertEqual(self.lib.geni2c_read(c.byref(self.sensor), result, 4), -3)
        self.assertEqual(list(result), [9, 9, 9, 9])
        self.assertEqual(self.lib.geni2c_close(c.byref(self.sensor)), 0)

    def test_copies_recipe_so_caller_changes_do_not_redirect_io(self):
        self.assertEqual(self.open(), 0)
        self.recipe.address, self.recipe.reg, self.recipe.size = 0x55, 0xFF, 1
        result = (c.c_uint8 * 4)()
        self.assertEqual(self.lib.geni2c_read(c.byref(self.sensor), result, 4), 0)
        self.assertEqual(self.calls, [(0x4A, 0, 1, 4, 120, 0)])
        self.assertEqual(self.lib.geni2c_close(c.byref(self.sensor)), 0)

    def test_rejects_invalid_shape_and_capacity_before_bus_io(self):
        self.recipe.size = 33
        self.assertNotEqual(self.open(), 0)
        self.recipe.size = 4
        self.assertEqual(self.open(), 0)
        result = (c.c_uint8 * 3)()
        self.assertNotEqual(self.lib.geni2c_read(c.byref(self.sensor), result, 3), 0)
        self.assertEqual(self.calls, [])
        self.assertEqual(self.lib.geni2c_close(c.byref(self.sensor)), 0)

    def test_reviewed_pause_is_bounded_copied_and_uses_one_deadline(self):
        self.recipe.pause_ms = 11
        self.assertNotEqual(self.open(), 0)
        self.recipe.pause_ms = 10
        self.assertEqual(self.open(), 0)
        self.recipe.pause_ms = 0
        result = (c.c_uint8 * 4)()
        self.assertEqual(self.lib.geni2c_read(c.byref(self.sensor), result, 4), 0)
        self.assertEqual(self.calls, [(0x4A, 0, 1, 4, 120, 10)])
        self.assertEqual(self.lib.geni2c_close(c.byref(self.sensor)), 0)

    def test_bus_rejects_pause_outside_combined_transfer(self):
        reg, result = (c.c_uint8 * 1)(0), (c.c_uint8 * 4)()
        for write_size, read_size, pause in ((1, 4, 11), (0, 4, 10), (1, 0, 10)):
            request = Request(0x4A, reg, write_size, result, read_size, 120, pause)
            self.assertEqual(self.lib.i2cbus_transfer(c.byref(self.bus), c.byref(request)), -1)
        self.assertEqual(self.calls, [])

    def test_light_enable_waits_then_reads_and_cleanup_retries(self):
        light, value = Light(), c.c_uint32(42)
        self.assertEqual(self.lib.senlux_open(c.byref(light), c.byref(self.bus), c.byref(self.enable), 50), 0)
        self.assertEqual(self.enable_calls, [False])
        self.assertEqual(self.lib.senlux_read(c.byref(light), c.byref(value)), 1)
        self.assertEqual(value.value, 42)
        self.current_ms = 1100
        self.assertEqual(self.lib.senlux_read(c.byref(light), c.byref(value)), 1)
        self.assertEqual(self.enable_calls, [False, True])
        self.current_ms = 2100
        self.assertEqual(self.lib.senlux_read(c.byref(light), c.byref(value)), 0)
        self.assertEqual(value.value, (0x04030201 * 7) // 5)
        self.assertEqual(self.calls, [(0x4A, 0, 1, 4, 2150, 10)])
        self.enable_failure = -1
        self.assertNotEqual(self.lib.senlux_close(c.byref(light)), 0)
        self.assertTrue(light.sensor.claimed)
        self.enable_failure = 0
        self.assertEqual(self.lib.senlux_close(c.byref(light)), 0)
        self.assertFalse(light.sensor.claimed)

    def test_failed_enable_open_cannot_read_and_requires_cleanup(self):
        light, value = Light(), c.c_uint32(42)
        self.enable_failure = -1
        self.assertNotEqual(self.lib.senlux_open(c.byref(light), c.byref(self.bus), c.byref(self.enable), 50), 0)
        self.assertNotEqual(self.lib.senlux_read(c.byref(light), c.byref(value)), 0)
        self.assertEqual(value.value, 42)
        self.enable_failure = 0
        self.assertEqual(self.lib.senlux_close(c.byref(light)), 0)
        self.assertEqual(self.calls, [])

    def test_enable_waits_start_after_each_gpio_write_completes(self):
        light, value = Light(), c.c_uint32(42)
        def delayed_set(context, high):
            self.current_ms += 100
            return 0
        self.enable_set = SET(delayed_set)
        self.enable = DigitalOutput(self.enable_set, 1)
        self.assertEqual(self.lib.senlux_open(c.byref(light), c.byref(self.bus), c.byref(self.enable), 50), 0)
        self.current_ms = 1100
        self.assertEqual(self.lib.senlux_read(c.byref(light), c.byref(value)), 1)
        self.assertFalse(light.high)
        self.current_ms = 1200
        self.assertEqual(self.lib.senlux_read(c.byref(light), c.byref(value)), 1)
        self.assertTrue(light.high)
        self.current_ms = 2200
        self.assertEqual(self.lib.senlux_read(c.byref(light), c.byref(value)), 1)
        self.current_ms = 2300
        self.assertEqual(self.lib.senlux_read(c.byref(light), c.byref(value)), 0)
        self.assertEqual(self.lib.senlux_close(c.byref(light)), 0)

    def test_little_endian_rational_scaling_and_limits(self):
        data = (c.c_uint8 * 4)(0xE8, 0x03, 0, 0)
        value = c.c_uint32(17)
        self.assertEqual(self.lib.geni2c_scale_le(data, 4, 7, 5, 200000000, c.byref(value)), 0)
        self.assertEqual(value.value, 1400)  # 1000 raw = 1.4 lux, expressed in millilux.
        for size, denominator, maximum in ((3, 5, 200000000), (4, 0, 200000000), (4, 5, 100)):
            value.value = 17
            self.assertNotEqual(self.lib.geni2c_scale_le(data, size, 7, denominator, maximum, c.byref(value)), 0)
            self.assertEqual(value.value, 17)
        largest = (c.c_uint8 * 4)(255, 255, 255, 255)
        self.assertNotEqual(self.lib.geni2c_scale_le(largest, 4, 0xFFFFFFFF, 1, 0xFFFFFFFF, c.byref(value)), 0)
        self.assertEqual(value.value, 17)
