"""Synthetic UART evidence must stay separate from physical model identity."""
from copy import deepcopy
from dataclasses import replace
import struct
import unittest

from test_observations import frame
from tools.config_transfer.capabilities import Capabilities
from tools.config_transfer.client import Client, TransferError
from tools.config_transfer.discovery import decode_discovery, discovery_request
from tools.config_transfer.protocol import Command
from tools.configurator.discovery import connection_preset
from tools.config_validator import Catalogue, load_document
from tools.config_validator.planner import ROOT
from tools.device_discovery import identify_uart, confirm_identity


def response():
    data = bytearray(112)
    struct.pack_into('<IIIIQQII', data, 0, 1, 0, 2, 0, 123, 1000, 1000, 1)
    data[40] = 1
    struct.pack_into('<8I', data, 80, 1, 90, 3, 0, 0, 0, 0, 1000)
    return data


class UARTObservationTests(unittest.TestCase):
    def setUp(self):
        self.payload = discovery_request(123, wiring_confirmed=True, preset_code=2)
        self.caps = Capabilities(10, False, 8192, 0x4FB, 8, 4, 7,
                                 'esp32-c3-supermini', '2.1.0', ('hlk_ld2450',))
        self.preset = connection_preset('bench_uart')

    def decode(self, data):
        return decode_discovery(frame(data), request_id=27, revision=0, payload=self.payload)

    def identify(self, observed, **kwargs):
        return identify_uart(observed, self.caps, self.preset, **kwargs)['devices'][0]

    def test_full_window_is_candidate_until_user_confirms_model(self):
        device = self.identify(self.decode(response()))
        self.assertEqual(device['identity']['status'], 'candidate')
        self.assertIsNone(device['identity']['selected'])
        self.assertNotIn('instance', device['recommendation'])
        confirm_identity(device, self.caps, Catalogue())
        self.assertEqual(device['identity']['status'], 'user_confirmed')
        self.assertEqual(device['capabilities']['protocol'], 'uart')
        self.assertEqual(device['recommendation']['status'], 'awaiting_review')
        self.assertFalse(device['capabilities']['hardware_qualified'])

    def test_silence_short_window_noise_and_errors_cannot_match(self):
        observation = self.decode(response())
        for changes in (dict(valid_reports=0), dict(valid_reports=2), dict(window_ms=999),
                        dict(receive_errors=1), dict(framing_errors=1), dict(discarded_bytes=1)):
            device = self.identify(replace(observation, records=(observation.records[0] | changes,)))
            self.assertEqual(device['identity']['status'], 'unknown', changes)
        for result in ('incomplete', 'fault', 'busy', 'stale'):
            self.assertEqual(self.identify(replace(observation, result=result))['identity']['status'], 'unknown')

    def test_malformed_uart_counts_and_wrong_preset_fail_closed(self):
        for offset, value in ((8, 1), (36, 2), (80, 2), (84, 89), (84, 128001),
                              (88, 4), (92, 1), (96, 1), (100, 1), (104, 30), (108, 999), (32, 999)):
            data = response()
            struct.pack_into('<I', data, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                self.decode(data)

    def test_empty_and_busy_are_valid_observations_not_identification(self):
        data = response()
        struct.pack_into('<8I', data, 80, 1, 0, 0, 0, 0, 0, 0, 1000)
        self.assertEqual(self.identify(self.decode(data))['identity']['status'], 'unknown')
        data = data[:80]
        struct.pack_into('<IIIIQQII', data, 0, 1, 1, 2, 0, 123, 0, 0, 0)
        self.assertEqual(self.decode(data).result, 'busy')

    def test_ambiguous_catalogue_and_missing_driver_do_not_offer_settings(self):
        catalogue = Catalogue()
        descriptor = deepcopy(catalogue.descriptors[('hlk_ld2450', '1.0.0')])
        catalogue.descriptors[('other_radar', '1.0.0')] = descriptor
        policy = load_document(ROOT / 'catalogue/discovery.json')
        entry = deepcopy(policy['entries'][1]); entry['descriptor']['id'] = 'other_radar'
        policy['entries'].append(entry)
        device = self.identify(self.decode(response()), catalogue=catalogue, policy=policy)
        self.assertEqual(device['identity']['status'], 'ambiguous')
        with self.assertRaises(ValueError): confirm_identity(device, self.caps, catalogue)
        device = self.identify(self.decode(response()))
        confirm_identity(device, replace(self.caps, drivers=()), catalogue)
        self.assertEqual(device['recommendation']['status'], 'blocked')

    def test_confirmation_and_feature_checks_precede_probe(self):
        client = Client(None)
        calls = []
        client.capabilities = lambda: self.caps
        client.query = lambda: type('State', (), dict(flags=0, active_revision=9))()
        client.exchange = lambda *args: calls.append(args)
        with self.assertRaises(ValueError): client.discover_uart()
        self.assertFalse(calls)
        client.capabilities = lambda: replace(self.caps, features=3)
        with self.assertRaises(TransferError): client.discover_uart(wiring_confirmed=True)
        self.assertFalse(calls)
        client.capabilities = lambda: self.caps
        client.discover_uart(wiring_confirmed=True)
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0][0], Command.DISCOVER)
        self.assertEqual(struct.unpack_from('<I', calls[0][3], 4)[0], 2)
