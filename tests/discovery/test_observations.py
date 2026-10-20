"""Discovery decoding and catalogue identification; all examples are synthetic."""
from copy import deepcopy
from dataclasses import replace
import struct
import unittest
import zlib

from tools.config_transfer.capabilities import Capabilities
from tools.config_transfer.client import Client, TransferError
from tools.config_transfer.discovery import decode_discovery, discovery_request
from tools.config_transfer.protocol import Command, HEADER, encode_request
from tools.config_validator import Catalogue, load_document
from tools.config_validator.planner import ROOT, check
from tools.device_discovery import identify_i2c


def frame(data, revision=0):
    prefix = HEADER.pack(b"UTH1", 1, 0x88, 0, 27, revision, len(data), 0)[:20]
    return prefix + struct.pack("<I", zlib.crc32(data, zlib.crc32(prefix))) + data


def response():
    data = bytearray(112)
    struct.pack_into("<IIIIQQII", data, 0, 1, 0, 1, 0, 123, 456, 4, 2)
    data[40] = 1
    struct.pack_into("<4I", data, 80, 118, 0, 0, 0x50)
    struct.pack_into("<4I", data, 96, 119, 0, 0, 0x60)
    return data


class DiscoveryObservationTests(unittest.TestCase):
    def setUp(self):
        self.payload = discovery_request(123, wiring_confirmed=True)
        self.caps = Capabilities(10, False, 8192, 0x4FB, 8, 4, 3,
                                 "esp32-c3-supermini", "2.1.0", ("bmp388",))

    def decode(self, data):
        return decode_discovery(frame(data), request_id=27, revision=0, payload=self.payload)

    def test_observation_is_distinct_from_identity_and_driver_support(self):
        observed = self.decode(response())
        result = identify_i2c(observed, self.caps)
        first, second = result["devices"]
        self.assertEqual(first["identity"]["status"], "verified")
        self.assertEqual(first["capabilities"]["protocol"], "i2c")
        self.assertEqual(second["observation"]["status"], "responding")
        self.assertEqual(second["identity"]["status"], "unknown")
        self.assertEqual(second["recommendation"]["status"], "awaiting_identity")
        self.assertEqual(first["recommendation"]["settings"], {})
        unsupported = identify_i2c(observed, replace(self.caps, drivers=()))["devices"][0]
        self.assertEqual(unsupported["identity"]["status"], "verified")
        self.assertEqual(unsupported["capabilities"]["support"], "unavailable")
        self.assertEqual(unsupported["recommendation"]["status"], "blocked")

    def test_corrupt_mismatched_unrequested_and_inconsistent_data_rejected(self):
        changes = [(0, 2), (4, 9), (8, 99), (12, 1), (16, 321),
                   (36, 8), (72, 1), (80, 120), (84, 1), (88, 4), (92, 256), (96, 118)]
        for offset, value in changes:
            data = response()
            struct.pack_into("<I", data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                self.decode(data)
        raw = frame(response())
        for bad in (raw[:-1], raw + b"x", raw[:-1] + bytes([raw[-1] ^ 1])):
            with self.assertRaises(ValueError):
                decode_discovery(bad, request_id=27, revision=0, payload=self.payload)
        with self.assertRaises(ValueError):
            decode_discovery(raw, request_id=28, revision=0, payload=self.payload)

    def test_partial_fault_and_no_response_never_establish_identity(self):
        for result_code in (2, 5):
            data = response()
            struct.pack_into("<I", data, 4, result_code)
            observed = self.decode(data)
            result = identify_i2c(observed, self.caps)
            self.assertTrue(all(d["identity"]["status"] == "unknown" for d in result["devices"]))
        data = response()
        struct.pack_into("<4I", data, 80, 118, 0, 1, 0)
        self.assertEqual(identify_i2c(self.decode(data), self.caps)["devices"][0]["identity"]["status"], "unknown")

    def test_new_catalogue_identity_needs_no_resolver_branch(self):
        catalogue = Catalogue()
        descriptor = deepcopy(catalogue.descriptors[("bmp388", "1.0.0")])
        descriptor["descriptor_id"] = "example_sensor"
        descriptor["instance_schema"]["properties"]["descriptor"]["properties"]["id"]["const"] = "example_sensor"
        catalogue.descriptors[("example_sensor", "1.0.0")] = descriptor
        policy = load_document(ROOT / "catalogue/discovery.json")
        entry = deepcopy(policy["entries"][0])
        entry["descriptor"]["id"] = "example_sensor"
        entry["identification"]["value"] = 0x60
        policy["entries"].append(entry)
        observed = self.decode(response())
        result = identify_i2c(observed, self.caps, catalogue=catalogue, policy=policy)
        self.assertEqual(result["devices"][1]["identity"]["selected"]["id"], "example_sensor")
        entry["identification"]["value"] = 0x50
        result = identify_i2c(observed, self.caps, catalogue=catalogue, policy=policy)
        self.assertEqual(result["devices"][0]["identity"]["status"], "ambiguous")
        self.assertIsNone(result["devices"][0]["identity"]["selected"])

    def test_preset_matches_board_and_existing_interface_contract(self):
        preset = load_document(ROOT / "catalogue/connections/bench-i2c.json")
        catalogue = Catalogue()
        board = catalogue.boards[("esp32-c3", "esp32-c3-supermini", "1.0.0")]
        check(preset["interface"], catalogue.node_schema["properties"]["interfaces"]["items"], "preset")
        self.assertEqual(preset["interface"]["port"], 0)
        self.assertEqual((preset["interface"]["sda_gpio"], preset["interface"]["scl_gpio"]), (0, 1))
        self.assertTrue({0, 1} <= set(board["target_limits"]["allowed_gpios"]))
        check(preset["interface"]["clock_hz"], board["interface_limits"]["i2c"]["clock_hz"], "clock")
        self.assertEqual(preset["reads"], [{"address": 118, "register": 0}, {"address": 119, "register": 0}])

    def test_confirmation_precedes_serial_access_and_failed_reply_is_not_retried(self):
        class Stream:
            def __init__(self):
                self.written = bytearray()
                self.now = 0
            def write(self, data):
                self.written.extend(data)
                return len(data)
            def read(self, count):
                return b""
            def clock(self):
                self.now += 0.1
                return self.now
        stream = Stream()
        client = Client(stream, clock=stream.clock, timeout=1)
        with self.assertRaises(ValueError):
            client.discover_i2c()
        self.assertFalse(stream.written)
        with self.assertRaises(TransferError):
            client.exchange(Command.DISCOVER, 27, 0, self.payload)
        self.assertEqual(stream.written, encode_request(Command.DISCOVER, 27, 0, self.payload))


if __name__ == "__main__":
    unittest.main()
