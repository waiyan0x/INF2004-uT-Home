"""Version-five raw bytes preserve identity and reject malformed value storage."""
import struct
import unittest

from tools.config_transfer.telemetry import decode_telemetry
from test_telemetry import frame, payload


def raw_payload(value=b"\x50", *, quality=1, status=1):
    data = payload() + bytearray(192)
    struct.pack_into("<I", data, 0, 5)
    struct.pack_into("<2I", data, 56, 1, 1)
    data.extend(struct.pack("<8I", 0, 6, 1, 0, 0, 0, 0, 0))
    row = bytearray(128)
    age = 0xFFFFFFFF if quality == 0 else 0
    struct.pack_into("<10I", row, 0, 0, 6, 0, quality, 0, 0, 0, status, 1000, age)
    row[40:46] = b"bmp_id"
    if status == 1:
        struct.pack_into("<I", row, 72, len(value))
        row[76:76 + len(value)] = value
    data.extend(row)
    return data


class RawTelemetryTests(unittest.TestCase):
    def decode(self, data):
        return decode_telemetry(frame(data), request_id=27)

    def test_raw_bytes_include_zero_and_full_length_without_scaling(self):
        for value in (b"\x50", b"\x00", bytes(range(16))):
            with self.subTest(value=value):
                sample = self.decode(raw_payload(value)).samples[0]
                self.assertEqual(sample["value"], {"bytes": list(value)})
                self.assertEqual(sample["quality"], "fresh")

    def test_fault_and_missing_have_no_values(self):
        for quality, status in ((0, 0), (3, 2), (3, 3), (3, 4)):
            with self.subTest(quality=quality):
                self.assertIsNone(self.decode(raw_payload(quality=quality, status=status)).samples[0]["value"])

    def test_raw_clock_fault_preserves_metadata_without_measurement_bytes(self):
        data = raw_payload(quality=3, status=1)
        start = 352 + 32
        struct.pack_into("<I", data, start + 16, 1)
        struct.pack_into("<I", data, start + 36, 0xFFFFFFFF)
        data[start + 72:start + 112] = bytes(40)
        sample = self.decode(data).samples[0]
        self.assertEqual(sample["quality"], "fault")
        self.assertEqual(sample["timestamp_ms"], 1)
        self.assertIsNone(sample["value"])
        data[start + 76] = 80
        with self.assertRaisesRegex(ValueError, "fault values"):
            self.decode(data)

    def test_raw_length_unused_bytes_and_legacy_version_are_strict(self):
        start = 352 + 32
        for offset, value in ((0, 4), (start + 72, 0), (start + 72, 17),
                              (start + 80, 1), (start + 92, 1), (start + 112, 1)):
            data = raw_payload()
            struct.pack_into("<I", data, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                self.decode(data)
        data = raw_payload(quality=3, status=2)
        data[start + 76] = 80
        with self.assertRaisesRegex(ValueError, "fault values"):
            self.decode(data)

    def test_v5_still_decodes_existing_queue_types(self):
        data = raw_payload()
        struct.pack_into("<I", data, 352 + 4, 5)
        struct.pack_into("<I", data, 352 + 32 + 4, 5)
        struct.pack_into("<I", data, 352 + 32 + 72, 1400)
        data[352 + 32 + 76:352 + 32 + 92] = bytes(16)
        self.assertEqual(self.decode(data).samples[0]["value"], {"millilux": 1400})


if __name__ == "__main__":
    unittest.main()
