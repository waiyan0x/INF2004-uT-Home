"""USB Wi-Fi observations: bounded framing, strict decoding and no mutation."""
from contextlib import redirect_stdout
import io
import json
import struct
import types
import unittest
import zlib
from unittest.mock import Mock, call, patch

from tools.config_transfer.client import Client, TransferError
from tools.config_transfer.network import decode_network
from tools.config_transfer.protocol import Command, HEADER, decode_reply, encode_request
from test_usb_client import Stream


def payload(*, flags=15, stage=12, retry=2, error=0, reason=0,
            ipv4=b'\xc0\xa8\x04\x17', deadline=0):
    return struct.pack('<4BiHH4sQ', 1, flags, stage, retry, error, reason, 0,
                       ipv4, deadline)


def frame(data=None, *, magic=b'UTH1', version=1, command=0x8a,
          reserved=0, request_id=27, revision=0, size=24):
    data = payload() if data is None else data
    prefix = HEADER.pack(magic, version, command, reserved, request_id,
                         revision, size, 0)[:20]
    return prefix + struct.pack('<I', zlib.crc32(data, zlib.crc32(prefix))) + data


class NetworkUsbClientTests(unittest.TestCase):
    def test_connected_and_signed_failure_observations(self):
        reply = decode_network(frame(), request_id=27)
        self.assertEqual((reply.request_id, reply.stage, reply.retry_state),
                         (27, 'ready', 'online'))
        self.assertEqual(reply.ipv4, '192.168.4.23')
        self.assertTrue(reply.attempted and reply.started and reply.station_ready and reply.has_ip)
        self.assertFalse(reply.cancelling or reply.fault)
        failed = decode_network(frame(payload(flags=33, stage=7, retry=0,
            error=-2147483648, reason=0xffff, ipv4=bytes(4), deadline=0xffffffffffffffff)),
            request_id=27)
        self.assertEqual((failed.stage, failed.sdk_error, failed.disconnect_reason),
                         ('wifi_init', -2147483648, 0xffff))
        self.assertEqual(failed.deadline_ms, 0xffffffffffffffff)
        self.assertTrue(failed.fault)
        self.assertIsNone(failed.ipv4)

    def test_absent_ip_is_unavailable_and_transitional_flags_are_preserved(self):
        for flags in range(64):
            reply = decode_network(frame(payload(flags=flags, stage=0, retry=1,
                ipv4=b'\x01\x02\x03\x04' if flags & 8 else bytes(4))), request_id=27)
            self.assertEqual(reply.has_ip, bool(flags & 8))
            self.assertEqual(reply.cancelling, bool(flags & 16))
            self.assertEqual(reply.ipv4, '1.2.3.4' if flags & 8 else None)
            self.assertEqual((reply.stage, reply.retry_state), ('not_attempted', 'attempt'))
        reply = decode_network(frame(payload(flags=0, stage=0, retry=0, ipv4=bytes(4))),
                               request_id=27)
        self.assertFalse(reply.attempted)
        self.assertEqual(reply.retry_state, 'waiting')

    def test_tx_power_stage_is_appended_without_renumbering_existing_stages(self):
        from tools.config_transfer.network import STAGES
        self.assertEqual(STAGES[:15], ('not_attempted', 'netif_init', 'event_loop',
            'default_station', 'wifi_register', 'got_ip_register', 'lost_ip_register',
            'wifi_init', 'storage', 'mode', 'config', 'start', 'ready', 'connect', 'disconnect'))
        reply = decode_network(frame(payload(flags=33, stage=15, retry=0,
            error=0x3002, ipv4=bytes(4))), request_id=27)
        self.assertEqual((reply.stage, reply.sdk_error), ('tx_power', 0x3002))

    def test_rejects_bad_lengths_crc_and_mismatched_headers(self):
        good = frame()
        for invalid in (good[:-1], good + b'x', good[:-1] + bytes([good[-1] ^ 1])):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                decode_network(invalid, request_id=27)
        for kwargs in (dict(magic=b'BAD1'), dict(version=2), dict(command=0x89),
                       dict(reserved=1), dict(request_id=28), dict(revision=1), dict(size=25)):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                decode_network(frame(**kwargs), request_id=27)
        with self.assertRaises(ValueError):
            decode_network(frame(request_id=0), request_id=0)

    def test_rejects_unknown_payload_fields_and_unavailable_ip_data(self):
        for offset, value in ((0, 0), (0, 2), (1, 64), (2, 16), (3, 3),
                              (10, 1), (11, 1)):
            data = bytearray(payload())
            data[offset] = value
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                decode_network(frame(data), request_id=27)
        with self.assertRaises(ValueError):
            decode_network(frame(payload(flags=7)), request_id=27)

    def test_network_requests_have_no_payload_revision_or_mutation_decoder(self):
        wire = encode_request(Command.NETWORK, 27)
        self.assertEqual(HEADER.unpack(wire)[:7], (b'UTH1', 1, 10, 0, 27, 0, 0))
        self.assertEqual(len(wire), 24)
        for kwargs in (dict(request_id=0), dict(request_id=True), dict(revision=1),
                       dict(profile=b'x')):
            arguments = dict(request_id=27)
            arguments.update(kwargs)
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                encode_request(Command.NETWORK, **arguments)
        with self.assertRaisesRegex(ValueError, 'versioned decoder'):
            decode_reply(frame(), command=Command.NETWORK, request_id=27, revision=0)

    def test_fragmented_noise_and_unchecked_length_do_not_query_or_retry(self):
        bogus = HEADER.pack(b'UTH1', 1, 0x8a, 0, 27, 0, 0xffffffff, 0)
        corrupt = bytearray(frame())
        corrupt[-1] ^= 1
        stream = Stream(b'boot\n' + bogus + corrupt + frame(), read_size=3)
        with patch('tools.config_transfer.client.secrets.randbelow', return_value=26):
            reply = Client(stream, clock=stream.clock).network()
        self.assertEqual(reply.ipv4, '192.168.4.23')
        self.assertEqual(stream.written, encode_request(Command.NETWORK, 27))

    def test_old_or_non_wifi_image_silence_times_out_without_resending(self):
        for incoming in (b'', frame()[:-1], frame(request_id=28)):
            stream = Stream(incoming)
            with self.subTest(incoming=incoming), self.assertRaisesRegex(TransferError, 'timed out'):
                Client(stream, clock=stream.clock, timeout=1).exchange(Command.NETWORK, 27)
            self.assertEqual(stream.written, encode_request(Command.NETWORK, 27))

    def test_malformed_matching_reply_preserves_rejection_cause(self):
        stream = Stream(frame(payload(stage=16)))
        with self.assertRaisesRegex(TransferError, 'matching reply rejected') as caught:
            Client(stream, clock=stream.clock, timeout=1).exchange(Command.NETWORK, 27)
        self.assertIsInstance(caught.exception.__cause__, ValueError)
        self.assertEqual(stream.written, encode_request(Command.NETWORK, 27))

    def test_cli_network_outputs_observation_only_and_closes_port(self):
        from tools.config_transfer.__main__ import main
        stream = Mock()
        serial = types.SimpleNamespace(Serial=Mock(return_value=stream))
        client = Mock(spec=Client)
        client.network.return_value = decode_network(frame(), request_id=27)
        output = io.StringIO()
        with patch.dict('sys.modules', serial=serial), \
                patch('tools.config_transfer.__main__.Client', return_value=client), \
                redirect_stdout(output):
            result = main(['network', '--port', 'COM_TEST'])
        self.assertEqual(result, 0)
        self.assertEqual(json.loads(output.getvalue())['ipv4'], '192.168.4.23')
        self.assertEqual(client.mock_calls, [call.network()])
        self.assertIs(stream.dtr, False)
        self.assertIs(stream.rts, False)
        stream.open.assert_called_once_with()
        stream.close.assert_called_once_with()
