"""Malformed telemetry bounds, read-only client behavior and partial streams."""
import struct
import unittest
import zlib
from unittest.mock import patch

from tools.config_transfer.protocol import HEADER, Command, encode_request
from tools.config_transfer.telemetry import decode_telemetry
from tools.config_transfer.client import Client, TransferError
from test_usb_client import Stream


def frame(payload, rid=27):
    head = HEADER.pack(b"UTH1", 1, 0x86, 0, rid, 0, len(payload), 0)[:20]
    return head + struct.pack("<I", zlib.crc32(payload, zlib.crc32(head))) + payload


def payload():
    words = [0] * 40
    words[0] = 1
    words[21] = 8
    return bytearray(struct.pack("<40I", *words))


class TelemetryTests(unittest.TestCase):
    def test_empty_capture_and_explicit_busy(self):
        data = payload()
        result = decode_telemetry(frame(data), request_id=27)
        self.assertEqual(result.samples, ())
        self.assertEqual(result.health["last_event"], None)
        data = bytearray(160)
        struct.pack_into("<2I", data, 0, 1, 1)
        result = decode_telemetry(frame(data), request_id=27)
        self.assertEqual(result.result, 1)
        self.assertEqual(result.health, {})

    def test_wrong_version_counts_reserved_and_crc_fail(self):
        for offset, value in ((0, 2), (4, 3), (56, 13), (60, 5), (84, 9), (72, 2), (32, 1)):
            data = payload()
            struct.pack_into("<I", data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                decode_telemetry(frame(data), request_id=27)
        good = frame(payload())
        for changed in (good[:-1], good + b"x", good[:-1] + bytes([good[-1] ^ 1])):
            with self.assertRaises(ValueError):
                decode_telemetry(changed, request_id=27)

    def test_fragmented_telemetry_never_queries_or_consumes_id(self):
        good = frame(payload())
        bogus = HEADER.pack(b"UTH1", 1, 0x86, 0, 27, 0, 0xFFFFFFFF, 0)
        stream = Stream(b"boot\n" + bogus + good[:-1] + good, read_size=3)
        with patch("tools.config_transfer.client.secrets.randbelow", return_value=26):
            result = Client(stream, clock=stream.clock).telemetry()
        self.assertEqual(result.result, 0)
        self.assertEqual(stream.written, encode_request(Command.TELEMETRY, 27))

    def test_truncated_long_reply_times_out_without_retry(self):
        stream = Stream(HEADER.pack(b"UTH1", 1, 0x86, 0, 27, 0, 1824, 0))
        with self.assertRaises(TransferError):
            Client(stream, clock=stream.clock, timeout=1).exchange(Command.TELEMETRY, 27)
        self.assertEqual(stream.written, encode_request(Command.TELEMETRY, 27))

    def test_sample_identity_padding_status_and_values_are_validated(self):
        data = payload()
        struct.pack_into("<2I", data, 56, 1, 1)
        data.extend(struct.pack("<8I", 0, 1, 0, 0, 0, 0, 0, 0))
        row = bytearray(128)
        struct.pack_into("<10I", row, 0, 0, 1, 0, 1, 0, 0, 0, 1, 1000, 0)
        row[40:46] = b"sensor"
        struct.pack_into("<2f", row, 72, 100000, -1.5)
        data.extend(row)
        result = decode_telemetry(frame(data), request_id=27)
        self.assertEqual(result.samples[0]["value"]["temperature_c"], -1.5)
        for offset, value in ((0, 4), (4, 2), (8, 8), (12, 2), (28, 3),
                              (32, 0), (36, 1), (72, 0x7FC00000), (80, 1), (112, 1)):
            changed = data.copy()
            struct.pack_into("<I", changed, 192 + offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                decode_telemetry(frame(changed), request_id=27)
        changed = data + row
        struct.pack_into("<I", changed, 56, 2)
        with self.assertRaisesRegex(ValueError, "sample row"):
            decode_telemetry(frame(changed), request_id=27)

    def test_maximum_snapshot_is_bounded_and_fragmentable(self):
        data = payload()
        struct.pack_into("<2I", data, 56, 12, 4)
        for queue in range(4):
            data.extend(struct.pack("<8I", queue, 4, 0, 0, 0, 0, 0, 0))
        for index in range(12):
            row = bytearray(128)
            source = index if index < 8 else 65535
            queue = 0 if index < 8 else index - 8
            struct.pack_into("<10I", row, 0, queue, 4, source, 0, 0, 0, 0, 0, 100, 0xFFFFFFFF)
            row[40:46] = b"health"
            data.extend(row)
        self.assertEqual(len(data), 1824)
        stream = Stream(frame(data), read_size=31)
        result = Client(stream, clock=stream.clock).exchange(Command.TELEMETRY, 27)
        self.assertEqual(len(result.samples), 12)
        self.assertTrue(all(sample["value"] is None for sample in result.samples))
