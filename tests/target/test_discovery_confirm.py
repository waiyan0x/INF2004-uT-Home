"""D4 confirmation stays session-bound and cannot configure or run equipment."""
from dataclasses import replace
import json
import unittest

from test_discovery_ui import DiscoveryClient
from test_configurator import Port
from tools.configurator.workflow import Workflow
from tools.config_transfer.discovery import Discovery
from tools.config_validator.planner import ROOT, load_document


class ConfirmClient(DiscoveryClient):
    def __init__(self, stream):
        super().__init__(stream)
        self.caps = replace(self.caps, features=7, node_schema='2.1.0', drivers=self.caps.drivers + ('pwm_output',))
        self.uart = Discovery(20, 0, 0, 'complete', 2, 123, 1000, 1000, 'a'*64, (
            dict(parser_id='ld2450_reports', received_bytes=90, valid_reports=3, framing_errors=0,
                 discarded_bytes=0, receive_errors=0, pending_bytes=0, window_ms=1000),))

    def discover_uart(self, *, wiring_confirmed):
        assert wiring_confirmed is True
        self.probes += 1
        self.timestamp = 2010
        return self.uart, self.caps


class ConfirmationTests(unittest.TestCase):
    def setUp(self):
        self.now = 0
        self.flow = Workflow(opener=lambda _: Port(), client_factory=ConfirmClient, clock=lambda: self.now)
        self.addCleanup(self.flow.close)
        self.flow.dispatch('connect', dict(port='fake'))
        self.client = self.flow.client
        self.document = load_document(ROOT / 'configs/nodes/distance-only.json')
        self.document['schema_version'] = '2.1.0'
        self.body = dict(profile=json.dumps(self.document), preset_id='bench_uart', wiring_confirmed=True)

    def prepare(self, preset):
        self.body['preset_id'] = preset
        return self.flow.dispatch('discover', self.body)

    def confirm(self, report, **changes):
        return self.flow.dispatch('confirm_discovered', self.body | dict(
            discovery_id=report['discovery_id'], index=0, identity_confirmed=True) | changes)

    def add(self, report, **changes):
        return self.flow.dispatch('add_discovered', self.body | dict(discovery_id=report['discovery_id'], index=0) | changes)

    def test_radar_candidate_confirmation_settings_and_add_are_separate(self):
        report = self.prepare('bench_uart')
        self.assertEqual(report['devices'][0]['identity']['status'], 'candidate')
        with self.assertRaisesRegex(ValueError, 'no supported settings'): self.add(report)
        report = self.prepare('bench_uart')
        confirmed = self.confirm(report)
        device = confirmed['devices'][0]
        self.assertEqual(device['identity']['status'], 'user_confirmed')
        self.assertEqual(device['identity']['confirmation']['source'], 'user')
        self.assertNotEqual(confirmed['discovery_id'], report['discovery_id'])
        draft = self.add(confirmed)['profile']
        self.assertEqual(draft['devices'][:-1], self.document['devices'])
        self.assertEqual(draft['interfaces'][-1]['baud_rate'], 256000)
        self.assertEqual(draft['devices'][-1]['behaviour']['destination'], draft['queues'][-1]['id'])
        self.assertIsNone(self.flow.pending)
        self.assertFalse(self.client.calls)

    def test_silence_noise_unknown_and_ambiguous_cannot_be_manually_promoted(self):
        for changes in (dict(valid_reports=0), dict(valid_reports=2), dict(receive_errors=1), dict(discarded_bytes=1)):
            self.client.uart = replace(self.client.uart, records=(self.client.uart.records[0] | changes,))
            report = self.prepare('bench_uart')
            with self.assertRaises(ValueError): self.confirm(report)
        self.assertFalse(self.client.calls)

    def test_oled_requires_existing_queue_and_never_probes_or_creates_a_sensor(self):
        # The six-pin OLED reserves GPIO4, so use a compatible environmental draft.
        self.document = load_document(ROOT / 'configs/nodes/two-environment-sensors.json')
        self.document['devices'] = self.document['devices'][:1]
        self.document['queues'] = self.document['queues'][:1]
        self.body['profile'] = json.dumps(self.document)
        report = self.prepare('bench_oled')
        self.assertEqual(report['capture']['result'], 'manual')
        self.assertEqual(report['devices'][0]['observation']['status'], 'not_observed')
        with self.assertRaisesRegex(ValueError, 'existing readings queue'): self.confirm(report, source_queue='invented')
        queue = self.document['queues'][0]['id']
        confirmed = self.confirm(report, source_queue=queue)
        draft = self.add(confirmed)['profile']
        self.assertEqual(draft['queues'], self.document['queues'])
        self.assertEqual(draft['devices'][-1]['behaviour']['sources'], [queue])
        self.assertEqual(draft['devices'][-1]['interface']['cs_gpio'], 4)
        self.assertEqual(draft['interfaces'][-1]['clock_hz'], 4000000)
        self.assertEqual(self.client.probes, 0)
        self.assertFalse(self.client.calls)
        self.body['profile'] = json.dumps(draft)
        again = self.confirm(self.prepare('bench_oled'), source_queue=queue)
        with self.assertRaisesRegex(ValueError, 'alone on its SPI bus'): self.add(again)

    def test_manual_gpio_confirmation_and_safe_disabled_pwm(self):
        self.document['devices'] = []
        self.document['queues'] = []
        self.body['profile'] = json.dumps(self.document)
        report = self.prepare('bench_distance')
        with self.assertRaisesRegex(ValueError, 'explicitly'): self.confirm(report, identity_confirmed='yes')
        confirmed = self.confirm(report)
        draft = self.add(confirmed)['profile']
        self.assertTrue(draft['devices'][0]['interface']['echo_voltage_divider_required'])
        self.body['profile'] = json.dumps(draft)
        confirmed = self.confirm(self.prepare('bench_pwm'))
        draft = self.add(confirmed)['profile']
        self.assertFalse(draft['devices'][-1]['enabled'])
        self.assertFalse(draft.get('rules'))
        self.assertEqual(self.client.probes, 0)
        self.assertFalse(self.client.calls)

    def test_pwm_requires_supported_schema_and_driver_without_silent_conversion(self):
        self.document['schema_version'] = '2.0.0'; self.body['profile'] = json.dumps(self.document)
        confirmed = self.confirm(self.prepare('bench_pwm'))
        with self.assertRaisesRegex(ValueError, 'schema 2.1.0'): self.add(confirmed)
        self.client.caps = self.flow.caps = replace(self.flow.caps, drivers=('hc_sr04',))
        confirmed = self.confirm(self.prepare('bench_pwm'))
        self.assertEqual(confirmed['devices'][0]['recommendation']['status'], 'blocked')
        with self.assertRaises(ValueError): self.add(confirmed)

    def test_oled_pin_conflicts_are_rejected(self):
        report = self.prepare('bench_oled')
        confirmed = self.confirm(report, source_queue=self.document['queues'][0]['id'])
        with self.assertRaisesRegex(ValueError, 'GPIO|pin'): self.add(confirmed)
        self.assertEqual(self.client.probes, 0)

    def test_confirmation_cannot_extend_expiry_or_survive_draft_state_reset_change(self):
        report = self.prepare('bench_uart')
        self.now = 30
        confirmed = self.confirm(report)
        self.assertEqual(confirmed['expires_in_ms'], 30000)
        self.now = 60
        with self.assertRaisesRegex(ValueError, 'expired'): self.add(confirmed)
        for change in ('draft', 'wiring', 'token', 'index', 'state', 'restart'):
            report = self.prepare('bench_uart')
            body = {}
            if change == 'draft': body['profile'] = self.body['profile'] + ' '
            if change == 'wiring': body['wiring_confirmed'] = False
            if change == 'token': body['discovery_id'] = 'old'
            if change == 'index': body['index'] = True
            if change == 'state': self.client.current = replace(self.client.current, active_crc=23)
            if change == 'restart': self.client.timestamp = 1
            with self.subTest(change=change), self.assertRaises(ValueError): self.confirm(report, **body)

    def test_manual_choices_work_on_old_firmware_but_radar_requires_new_feature(self):
        self.client.caps = self.flow.caps = replace(self.flow.caps, features=0)
        self.confirm(self.prepare('bench_distance'))
        with self.assertRaisesRegex(ValueError, 'does not support discovery'): self.prepare('bench_uart')
        self.assertEqual(self.client.probes, 0)
