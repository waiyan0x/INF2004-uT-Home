"""Bounded rules and wire contract through the actual generic I2C primitive."""
import ctypes as c
import importlib.util
import json
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("rule_host", ROOT/"experiments/runtime_rules/host.py")
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


class Config(c.Structure):
    _fields_ = [("version", c.c_uint32), ("revision", c.c_uint32),
                ("device", c.c_char * 32), ("address", c.c_uint32),
                ("reg", c.c_uint32), ("read_size", c.c_uint32)]


class State(c.Structure):
    _fields_ = [("active", Config), ("loaded", c.c_bool)]


class TransactionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.offline = host.Offline(host.build(ROOT/"build/transaction-tests"))
        cls.lib = cls.offline.lib
        cls.a = host.load_rule(ROOT/"experiments/runtime_rules/examples/bmp-id.json")
        cls.b = host.load_rule(ROOT/"experiments/runtime_rules/examples/bmp-pressure.json")

    def setUp(self):
        self.lib.txsim_init()

    def send(self, rule=None, update=True, rid=1):
        return host.result(self.offline.exchange(host.request(rule or self.a, rid, update)))

    def test_switches_real_driver_transaction_and_preserves_rejected_update(self):
        outputs = host.demonstrate(self.offline.exchange, self.a, self.b)
        self.assertEqual([o["raw_hex"] for o in outputs], ["", "00", "", "00 01 02", "", "00 01 02"])
        self.assertEqual(self.lib.txsim_calls(), 3)
        self.assertEqual([self.lib.txsim_observed(i) for i in range(4)], [119, 4, 1, 3])

    def test_unconfigured_and_wrong_identity_never_access_driver(self):
        self.assertEqual(self.send(update=False)["status"], "unconfigured")
        self.send()
        for values in (dict(device="other"), dict(revision=7)):
            self.assertEqual(self.send(dict(self.a, **values), False)["status"], "mismatch")
        self.assertEqual(self.lib.txsim_calls(), 0)

    def test_all_transfer_bounds_and_boundary_addresses(self):
        for address in (8, 119):
            for reg in (0, 255):
                for length in (1, 16):
                    rule = dict(self.a, address=address, register=reg, read_length=length)
                    self.assertEqual(self.send(rule)["status"], "ok")
                    self.assertEqual(len(self.send(rule, False)["raw_hex"].split()), length)

    def test_invalid_wire_updates_leave_active_rule_unchanged(self):
        self.send(self.b)
        for index, value in ((44, 0), (44, 7), (44, 120), (44, 255),
                             (46, 0), (46, 17), (46, 255), (2, 2), (47, 1),
                             (12, 0), (12, 255)):
            message = bytearray(host.request(self.a, 10, True))
            message[index] = value
            self.assertEqual(host.result(self.offline.exchange(bytes(message)))["status"], "invalid")
            self.assertEqual(self.send(self.b, False)["revision"], 2)

    def test_malformed_and_truncated_envelopes_have_no_io(self):
        message = host.request(self.a, 1, True)
        for size in range(48):
            self.assertEqual(host.result(self.offline.exchange(message[:size]))["status"], "invalid")
        for payload in (message+b"junk", b"X"+message[1:], message[:3]+bytes([3])+message[4:]):
            self.assertEqual(host.result(self.offline.exchange(payload))["status"], "invalid")
        self.assertEqual(self.lib.txsim_calls(), 0)

    def test_error_never_replays_previous_bytes_and_claims_are_released(self):
        self.send(self.b)
        self.assertEqual(self.send(self.b, False)["raw_hex"], "00 01 02")
        for fault, status in ((-2, "timeout"), (-3, "io"), (-4, "io")):
            self.lib.txsim_fault(fault)
            reply = self.send(self.b, False)
            self.assertEqual((reply["status"], reply["raw_hex"]), (status, ""))
        self.lib.txsim_fault(0)
        self.assertEqual(self.send(self.b, False)["status"], "ok")

    def test_c_checks_full_width_parameters_independently(self):
        self.assertEqual(c.sizeof(Config), self.lib.txsim_size(0))
        self.assertEqual(c.sizeof(State), self.lib.txsim_size(1))
        self.lib.txrule_init.argtypes = [c.POINTER(State)]
        self.lib.txrule_load.argtypes = [c.POINTER(State), c.POINTER(Config)]
        state = State()
        self.lib.txrule_init(c.byref(state))
        good = Config(1, 1, b"bmp388", 119, 0, 1)
        self.assertEqual(self.lib.txrule_load(c.byref(state), c.byref(good)), 0)
        before = bytes(state)
        for field, value in (("version", 2), ("revision", 0), ("address", 256),
                             ("reg", 256), ("read_size", 0xffffffff)):
            bad = Config.from_buffer_copy(good)
            setattr(bad, field, value)
            self.assertEqual(self.lib.txrule_load(c.byref(state), c.byref(bad)), 1)
            self.assertEqual(bytes(state), before)

    def test_fixed_device_storage_requires_a_terminator_and_clean_padding(self):
        for name in (b"A"*32, b"bad name", bytes([0xff]), b"abc"+bytes([0])+b"x"):
            state = State()
            cfg = Config(1, 1, b"", 119, 0, 1)
            c.memmove(c.addressof(cfg) + Config.device.offset, name, len(name))
            self.assertEqual(self.lib.txrule_load(c.byref(state), c.byref(cfg)), 1)


if __name__ == "__main__":
    unittest.main()
