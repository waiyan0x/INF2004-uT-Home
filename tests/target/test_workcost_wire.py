"""Workload identity, corrupt frames, explicit gaps and stream fragmentation."""
from contextlib import redirect_stdout
from dataclasses import replace
import io
import json
import struct
import types
import unittest
import zlib
from unittest.mock import Mock, patch

from tools.config_transfer.protocol import HEADER, Command, encode_request, decode_reply
from tools.config_transfer.workcost import decode_workcost
from tools.config_transfer.client import Client, TransferError
from ai.measurements import validate_snapshot
from test_usb_client import Stream


def frame(data):
    prefix = HEADER.pack(b'UTH1', 1, 0x89, 0, 27, 0, len(data), 0)[:20]
    return prefix + struct.pack('<I', zlib.crc32(data, zlib.crc32(prefix))) + data


def payload():
    data = bytearray(384)
    struct.pack_into('<15I', data, 0, 1, 0, 4, 17, 123, 4, 1000, 1,
                     8448, 4096, 1, 1024, 0, 0, 44)
    for slot in range(4):
        struct.pack_into('<6I', data, 96 + slot * 72, slot, 0, 0, 0, 8192, 0)
    struct.pack_into('<17I', data, 96, 0, 2, 19, 7, 8192, 7000,
                     2, 40, 20, 0, 4, 10, 30, 2, 0, 0, 0)
    return data


class WorkcostWireTests(unittest.TestCase):
    def test_scheduler_versions_gaps_and_reserved_fields(self):
        self.assertIsNone(decode_workcost(frame(payload()), request_id=27).scheduler)
        data = payload()
        struct.pack_into('<I', data, 0, 2)
        self.assertIsNone(decode_workcost(frame(data), request_id=27).scheduler)
        struct.pack_into('<5I', data, 60, 1, 100, 1, 70, 1)
        value = decode_workcost(frame(data), request_id=27)
        self.assertEqual(value.scheduler['elapsed_ms'], (1 << 32) + 100)
        for offset, word in ((0, 1), (60, 0), (60, 2), (76, 0), (76, 1001), (80, 1), (92, 1)):
            changed = data.copy()
            struct.pack_into('<I', changed, offset, word)
            with self.subTest(offset=offset, word=word), self.assertRaises(ValueError):
                decode_workcost(frame(changed), request_id=27)

    def test_scheduler_window_wrap_and_context_rejection(self):
        data = payload()
        struct.pack_into('<I', data, 0, 2)
        struct.pack_into('<5I', data, 60, 1, 100, 1, 0xffffffcd, 1)
        before = decode_workcost(frame(data), request_id=27)
        later = replace(before, timestamp_us=before.timestamp_us+200000,
                        scheduler=dict(before.scheduler, elapsed_ms=(1 << 32)+300, idle_ms=99))
        window = later.scheduler_window(before)
        self.assertEqual((window['duration_ms'], window['idle_ms'], window['non_idle_ms']), (200, 150, 50))
        self.assertEqual(window['sampled_non_idle_percent'], 25)
        self.assertFalse(window['cpu_cycles_measured'])
        for changed in (replace(later, result=1), replace(later, scheduler=None),
                        replace(later, generation=before.generation+1),
                        replace(later, active_revision=before.active_revision+1),
                        replace(later, active_crc=before.active_crc+1),
                        replace(later, timestamp_us=before.timestamp_us),
                        replace(later, scheduler=dict(later.scheduler, tick_ms=2)),
                        replace(later, scheduler=dict(later.scheduler, elapsed_ms=99)),
                        replace(later, scheduler=dict(later.scheduler, elapsed_ms=(1 << 32)+150)),
                        replace(later, scheduler=dict(later.scheduler, elapsed_ms=(1 << 32)+60101)),
                        replace(later, scheduler=dict(later.scheduler, idle_ms=150))):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                changed.scheduler_window(before)

    def test_cli_workcost_is_read_only_and_closes_serial(self):
        from tools.config_transfer.__main__ import main
        for result in (0, 1, 2):
            data = payload() if result == 0 else bytearray(96)
            if result:
                struct.pack_into('<2I', data, 0, 1, result)
            reply = decode_workcost(frame(data), request_id=27)
            stream = Mock()
            serial = types.SimpleNamespace(Serial=Mock(return_value=stream))
            client = Mock(spec=Client)
            client.workcost.return_value = reply
            output = io.StringIO()
            with self.subTest(result=result), patch.dict('sys.modules', serial=serial), \
                    patch('tools.config_transfer.__main__.Client', return_value=client), \
                    redirect_stdout(output):
                status = main(['workcost', '--port', 'COM_TEST'])
            self.assertEqual(status, 0 if result == 0 else 2)
            self.assertEqual(json.loads(output.getvalue())['result'], result)
            self.assertEqual(client.mock_calls, [unittest.mock.call.workcost()])
            serial.Serial.assert_called_once_with(port=None, baudrate=115200, timeout=0.05,
                                                  write_timeout=0.2, rtscts=False, dsrdtr=False)
            self.assertIs(stream.dtr, False)
            self.assertIs(stream.rts, False)
            self.assertEqual(stream.port, 'COM_TEST')
            stream.open.assert_called_once_with()
            stream.close.assert_called_once_with()

    def test_success_and_measurement_identity(self):
        value = decode_workcost(frame(payload()), request_id=27)
        self.assertEqual(value.timestamp_us, (1 << 32) + 1000)
        self.assertEqual(value.controller_stack['unused_min'], 4096)
        self.assertIsNone(value.idle_stack['unused_min'])
        self.assertEqual(value.workers[0]['stats']['transfer_bytes'], 30)
        measurement = value.measurement(firmware_elf_sha256='a'*64, profile_sha256='b'*64)
        self.assertEqual(validate_snapshot(measurement)['workers'][0]['source_index'], 2)
        self.assertEqual(len(measurement['workers']), 1)

    def test_failures_are_header_only_and_cannot_form_measurements(self):
        for result in (1, 2):
            data = bytearray(96)
            struct.pack_into('<2I', data, 0, 1, result)
            value = decode_workcost(frame(data), request_id=27)
            self.assertEqual(value.result, result)
            self.assertEqual(value.workers, ())
            with self.assertRaises(ValueError):
                value.measurement(firmware_elf_sha256='a'*64, profile_sha256='b'*64)
            data[8] = 1
            with self.assertRaises(ValueError):
                decode_workcost(frame(data), request_id=27)

    def test_malformed_padding_bindings_and_stats(self):
        for offset, word in ((0, 3), (4, 3), (20, 3), (32, 8192), (36, 8449),
                             (40, 2), (48, 1), (60, 1), (92, 1), (96, 1),
                             (100, 8), (104, 0), (104, 0x80000000), (108, 8),
                             (112, 8191), (116, 8193), (120, 0), (128, 41),
                             (136, 31), (152, 1), (160, 2), (164, 1),
                             (172, 1), (180, 2), (188, 1), (192, 1)):
            data = payload()
            struct.pack_into('<I', data, offset, word)
            with self.subTest(offset=offset, word=word), self.assertRaises(ValueError):
                decode_workcost(frame(data), request_id=27)
        data = payload()
        data[168:240] = data[96:168]
        struct.pack_into('<I', data, 168, 1)
        with self.assertRaises(ValueError):
            decode_workcost(frame(data), request_id=27)

    def test_missing_stats_and_saturation_remain_explicit(self):
        data = payload()
        struct.pack_into('<I', data, 108, 5)
        data[120:164] = bytes(44)
        value = decode_workcost(frame(data), request_id=27)
        self.assertIsNone(value.workers[0]['stats'])
        self.assertFalse(value.workers[0]['available'])
        data = payload()
        struct.pack_into('<I', data, 160, 1)
        self.assertEqual(decode_workcost(frame(data), request_id=27).workers[0]['stats']['flags'], 1)

    def test_crc_identity_length_and_request_contract(self):
        good = frame(payload())
        for broken in (good[:-1], good+b'x', good[:-1]+bytes([good[-1]^1])):
            with self.assertRaises(ValueError):
                decode_workcost(broken, request_id=27)
        with self.assertRaises(ValueError):
            decode_workcost(good, request_id=28)
        for kwargs in (dict(revision=1), dict(profile=b'x')):
            with self.assertRaises(ValueError):
                encode_request(Command.WORKCOST, 27, **kwargs)
        with self.assertRaises(ValueError):
            decode_reply(good, command=Command.WORKCOST, request_id=27, revision=0)

    def test_fragmented_read_is_one_observation_without_query_or_retry(self):
        bogus = HEADER.pack(b'UTH1', 1, 0x89, 0, 27, 0, 0xffffffff, 0)
        stream = Stream(b'boot\n'+bogus+frame(payload()), read_size=3)
        with patch('tools.config_transfer.client.secrets.randbelow', return_value=26):
            reply = Client(stream, clock=stream.clock).workcost()
        self.assertEqual(reply.active_revision, 17)
        self.assertEqual(stream.written, encode_request(Command.WORKCOST, 27))
        stream = Stream(frame(payload())[:-1])
        with self.assertRaises(TransferError):
            Client(stream, clock=stream.clock, timeout=1).exchange(Command.WORKCOST, 27)
        self.assertEqual(stream.written, encode_request(Command.WORKCOST, 27))
