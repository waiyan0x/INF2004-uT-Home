"""USB client faults and reconnect behavior using a bounded fake byte stream."""
import struct
import unittest
import zlib

from tools.config_transfer.client import Client, TransferError
from tools.config_transfer.protocol import Command, decode_reply, encode_request, HEADER


def response(command, rid, revision=0, first=0):
    payload = struct.pack("<6I", first, 17, 16, 123, 456, 0x51)
    head = HEADER.pack(b"UTH1", 1, int(command) | 0x80, 0, rid, revision, 24, 0)[:20]
    return head + struct.pack("<I", zlib.crc32(payload, zlib.crc32(head))) + payload


class Stream:
    def __init__(self, incoming=b"", read_size=5, write_size=3):
        self.incoming = incoming
        self.read_size = read_size
        self.write_size = write_size
        self.written = bytearray()
        self.seconds = 0

    def read(self, capacity):
        count = min(capacity, self.read_size)
        data, self.incoming = self.incoming[:count], self.incoming[count:]
        return data

    def write(self, data):
        count = min(len(data), self.write_size)
        self.written.extend(data[:count])
        return count

    def clock(self):
        self.seconds += 0.01
        return self.seconds


class UsbClientTests(unittest.TestCase):
    def test_fragmented_noise_corruption_and_wrong_reply_resynchronize(self):
        good = response(Command.QUERY, 9)
        corrupt = good[:-1] + bytes([good[-1] ^ 1])
        stream = Stream(b"boot\r\n" + good[:19] + corrupt + response(Command.QUERY, 8) + good)
        reply = Client(stream, clock=stream.clock).exchange(Command.QUERY, 9)
        self.assertEqual(reply.active_revision, 17)
        self.assertEqual(bytes(stream.written), encode_request(Command.QUERY, 9))

    def test_reconnect_probe_selects_next_id_then_refreshes_status(self):
        class Reactive(Stream):
            def write(self, data):
                self.written.extend(data)
                if len(self.written) == 24:
                    rid = HEADER.unpack(self.written)[4]
                    self.incoming = response(Command.PROBE, rid, first=99)
                elif len(self.written) == 48:
                    self.incoming = response(Command.QUERY, 100)
                return len(data)
        stream = Reactive()
        reply = Client(stream, clock=stream.clock).query()
        self.assertEqual(reply.request_id, 100)
        self.assertEqual(stream.written[24:], encode_request(Command.QUERY, 100))

    def test_probe_keeps_full_uint32_sequence_separate_from_status(self):
        reply = decode_reply(response(Command.PROBE, 1, first=0xFFFFFFFF),
                             command=Command.PROBE, request_id=1, revision=0)
        self.assertEqual(reply.last_request_id, 0xFFFFFFFF)
        self.assertFalse(hasattr(reply, "status"))
        with self.assertRaises(ValueError):
            decode_reply(response(Command.QUERY, 1, first=99),
                         command=Command.QUERY, request_id=1, revision=0)

    def test_lost_reply_does_not_resend_apply(self):
        stream = Stream()
        with self.assertRaisesRegex(TransferError, "outcome unknown"):
            Client(stream, clock=stream.clock, timeout=1).exchange(Command.APPLY, 4, 2, b"{}")
        self.assertEqual(stream.written, encode_request(Command.APPLY, 4, 2, b"{}"))

    def test_partial_send_failure_and_invalid_counts_stop(self):
        for count in (0, -1, None, 100):
            stream = Stream()
            stream.write = lambda data, count=count: count
            with self.subTest(count=count), self.assertRaises(TransferError):
                Client(stream, clock=stream.clock).exchange(Command.QUERY, 1)
        class Broken(Stream):
            def write(self, data):
                raise OSError("unplugged")
        stream = Broken()
        with self.assertRaisesRegex(TransferError, "send failed"):
            Client(stream, clock=stream.clock).exchange(Command.SAVE, 2, 1)

    def test_oversized_input_and_read_failure_stop(self):
        stream = Stream()
        stream.read = lambda capacity: b"x" * 65
        with self.assertRaisesRegex(TransferError, "bounded read"):
            Client(stream, clock=stream.clock).exchange(Command.QUERY, 1)
        def broken_read(capacity):
            raise OSError("disconnected")
        stream.read = broken_read
        with self.assertRaisesRegex(TransferError, "receive failed"):
            Client(stream, clock=stream.clock).exchange(Command.QUERY, 1)


    def test_exhausted_sequence_never_sends_wrapped_query(self):
        class Exhausted(Stream):
            def write(self, data):
                self.written.extend(data)
                rid = HEADER.unpack(self.written)[4]
                self.incoming = response(Command.PROBE, rid, first=0xFFFFFFFF)
                return len(data)
        stream = Exhausted()
        with self.assertRaisesRegex(TransferError, "exhausted"):
            Client(stream, clock=stream.clock).query()
        self.assertEqual(len(stream.written), 24)

    def test_invalid_cli_request_does_not_import_or_open_serial(self):
        from contextlib import redirect_stderr
        from io import StringIO
        from unittest.mock import patch
        from tools.config_transfer.__main__ import main
        with patch.dict("sys.modules", {"serial": None}), redirect_stderr(StringIO()):
            self.assertEqual(main(["save", "--port", "COM99", "--revision", "0"]), 1)
            self.assertEqual(main(["query", "--port", "COM99", "--revision", "1"]), 1)
