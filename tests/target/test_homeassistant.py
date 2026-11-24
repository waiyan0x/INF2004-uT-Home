"""Read-only HA projection against real C decisions and a local MQTT broker.

The observer checks the MQTT contract; it is not a running Home Assistant UI.
"""
import ctypes as c
import io
import json
import os
from pathlib import Path
import socket
import ssl
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

import paho.mqtt.client as mqtt

from tools.transaction import homeassistant as ha, session
from tools.transaction.client import Mqtt
from tools.transaction.controller import Controller
from mqtt_fixture import LocalBroker, DeviceBridge, NODE
import test_transaction_controller as control
import test_tx_owner as owner
import test_txjob as engine

ROOT = Path(__file__).resolve().parents[2]


class DisplayStateTests(unittest.TestCase):
    setUpClass = classmethod(control.ControllerTests.setUpClass.__func__)
    setUp = control.ControllerTests.setUp
    open_controller = control.ControllerTests.open_controller
    start = control.ControllerTests.start

    def view(self):
        return ha.project(self.controller.snapshot())

    def test_preview_update_and_execute_are_different_and_zero_is_valid(self):
        self.start()
        self.controller.refresh()
        state = self.view()
        self.assertEqual(state['status'], 'update_ok')
        self.assertFalse(state['data_available'])
        self.assertIsNone(state['raw_hex'])
        self.assertEqual(state['details']['configured']['write_length'], 1)
        self.assertEqual(self.begins, 0)
        self.data = b'\0'
        self.controller.execute()
        self.controller.refresh()
        state = self.view()
        self.assertTrue(state['data_available'])
        self.assertEqual((state['raw_hex'], state['rx_received']), ('00', 1))
        before = self.controller.snapshot()
        self.controller.select_rule({**self.profile, 'write_hex': '04', 'read_length': 3})
        self.assertEqual(self.view()['details']['configured']['write_hex'], '00')
        # Projection neither mutates the snapshot nor calls an engine operation.
        ha.project(before)
        self.assertTrue(before['display']['available'])
        self.assertEqual(self.begins, 1)
        waiting = ha.project(before, invalidate=True)
        self.assertEqual(waiting['status'], 'request_pending')
        self.assertIsNone(waiting['raw_hex'])

    def test_expiry_failure_and_disconnect_clear_previous_bytes(self):
        self.start()
        self.controller.execute()
        self.controller.refresh()
        self.assertTrue(self.view()['data_available'])
        self.clock.now += 5
        self.now += 5000
        self.controller.refresh()
        self.assertEqual(self.view()['status'], 'result_expired')
        self.assertIsNone(self.view()['raw_hex'])
        self.poll_status = 4
        self.controller.execute()
        self.controller.refresh()
        state = self.view()
        self.assertEqual(state['status'], 'execute_timeout')
        self.assertFalse(state['data_available'])
        self.assertIsNone(state['rx_received'])
        self.assertEqual(state['details']['last_result']['status'], 'timeout')
        self.offline = True
        self.controller.poll()
        self.assertEqual(self.view()['status'], 'esp_disconnected')
        self.assertIsNone(self.view()['details']['configured'])

    def test_rejected_update_preserves_active_rule_without_old_reading(self):
        self.start()
        self.controller.execute()
        self.controller.select_rule({**self.profile, 'write_hex': '04', 'read_length': 3})
        self.before_send = lambda packet: setattr(self, 'idle', False)
        self.assertEqual(self.controller.load()['status'], 'busy')
        self.before_send, self.idle = None, True
        self.controller.refresh()
        state = self.view()
        self.assertEqual(state['status'], 'update_busy')
        self.assertEqual(state['details']['configured']['revision'], 1)
        self.assertEqual(state['details']['configured']['read_length'], 1)
        self.assertIsNone(state['raw_hex'])

    def test_unknown_boot_and_generation_change_do_not_republish_old_data(self):
        self.start()
        self.drop = 'after'
        with self.assertRaises(TimeoutError):
            self.controller.execute()
        self.assertEqual(self.view()['status'], 'outcome_unknown')
        self.assertIsNone(self.view()['raw_hex'])
        self.controller.connect()
        self.controller.recover()
        self.controller.refresh()
        self.assertTrue(self.view()['data_available'])
        self.generation += 1
        self.controller.refresh()
        self.assertFalse(self.view()['active_available'])
        self.assertIsNone(self.view()['raw_hex'])
        self.assertEqual(self.lib.txjob_init(self.state, c.byref(self.ops), engine.BOOT+1), 0)
        self.controller.refresh()
        self.assertFalse(self.view()['data_available'])
        self.assertEqual(self.view()['details']['boot_id'], f'{engine.BOOT+1:016x}')
        self.assertEqual(self.begins, 1)

    def test_discovery_is_stable_bounded_and_has_no_command_channel(self):
        first = ha.discovery(NODE)
        self.assertEqual(first, ha.discovery(NODE))
        self.assertEqual(len(first), 6)
        ids = set()
        for topic, config in first.items():
            self.assertTrue(topic.startswith(f'homeassistant/sensor/{NODE}/'))
            self.assertNotIn('command_topic', json.dumps(config))
            self.assertEqual(config['expire_after'], 2)
            self.assertEqual(config['availability_mode'], 'all')
            ids.add(config['unique_id'])
        self.assertEqual(len(ids), 6)
        self.assertNotEqual(ids, {v['unique_id'] for v in ha.discovery('uthome-abcdef012345').values()})
        for bad in ('', 'uthome-0123456789ab/#', 'other', None):
            with self.assertRaises(ValueError):
                ha.discovery(bad)
        self.start()
        view = self.controller.snapshot()
        view['error'] = 'private-error-sentinel'
        view['active']['source']['path'] = 'private-path-sentinel'
        self.assertNotIn('private-', json.dumps(ha.project(view)))


class DisplayBrokerTests(unittest.TestCase):
    setUpClass = classmethod(owner.OwnerTests.setUpClass.__func__)

    def setUp(self):
        owner.OwnerTests.setUp(self)
        folder = tempfile.TemporaryDirectory(prefix='uthome-ha-')
        self.addCleanup(folder.cleanup)
        self.work = Path(folder.name)
        self.broker = LocalBroker(self.work, home_assistant=True)
        self.addCleanup(self.broker.close)
        self.broker.start()
        self.bridge = DeviceBridge(self.lib, self.broker)
        self.addCleanup(self.bridge.close)
        credentials = patch.dict(os.environ, {'UTHOME_MQTT_USER': 'controller-test',
                                              'UTHOME_MQTT_PASSWORD': self.broker.passwords['controller-test']})
        credentials.start()
        self.addCleanup(credentials.stop)
        self.controller = self.open_controller()
        self.addCleanup(lambda: self.controller.close())
        self.display = self.open_display()
        self.addCleanup(lambda: self.display.close())
        self.messages = []
        self.observer = None
        self.addCleanup(self.stop_observer)
        self.start_observer()

    def open_controller(self):
        return Controller(self.work/'session.sqlite3', NODE,
                          lambda: Mqtt('127.0.0.1', self.broker.port, self.broker.ca, NODE, timeout=2))

    def open_display(self):
        return ha.Display('127.0.0.1', self.broker.port, self.broker.ca, NODE)

    def start_observer(self):
        ready = threading.Event()
        self.observer = mqtt.Client(client_id='ha-observer', clean_session=True)
        self.observer.username_pw_set('observer-test', self.broker.passwords['observer-test'])
        self.observer.tls_set_context(ssl.create_default_context(cafile=str(self.broker.ca)))
        self.observer.on_connect = lambda client, *args: client.subscribe([
            (f'homeassistant/sensor/{NODE}/+/config', 1), (f'uthome/v1/{NODE}/display/#', 1)])
        self.observer.on_subscribe = lambda *args: ready.set()
        self.observer.on_message = lambda client, userdata, message: self.messages.append(
            (message.topic, message.payload.decode(), message.retain))
        self.observer.connect('127.0.0.1', self.broker.port)
        self.observer.loop_start()
        self.assertTrue(ready.wait(5))

    def stop_observer(self):
        if self.observer is not None:
            self.observer.disconnect()
            self.observer.loop_stop()
            self.observer = None

    def until(self, predicate, *, pump=True, observe=True):
        deadline = time.monotonic()+8
        while time.monotonic() < deadline:
            if pump:
                self.display.poll(self.controller.poll() if observe else self.controller.snapshot())
            if predicate():
                return
            time.sleep(0.02)
        self.fail(f'display deadline: {self.display.status()}, last={self.latest()}')

    def latest(self):
        for topic, payload, retained in reversed(self.messages):
            if topic == self.display.state_topic:
                self.assertFalse(retained)
                return json.loads(payload)
        return {}

    def configs(self):
        return [message for message in self.messages if message[0].endswith('/config')]

    def select(self, operation):
        self.controller.select(ROOT/'datasheets/bmp388.md', operation, 119)
        self.controller.load()

    def test_markdown_switch_updates_display_without_another_driver_call(self):
        self.controller.connect()
        for operation, expected, register, count in (
                ('chip_id', '50', '00', 1), ('pressure_raw', '00 00 00', '04', 3), ('chip_id', '50', '00', 1)):
            self.select(operation)
            self.until(lambda: self.latest().get('status') == 'update_ok' and
                       (self.latest().get('details', {}).get('configured') or {}).get('read_length') == count)
            self.assertIsNone(self.latest()['raw_hex'])
            reply = self.controller.execute()
            self.until(lambda: (self.latest().get('details', {}).get('last_result') or {}).get('request_id') == reply['request_id'])
            self.assertEqual(self.latest()['raw_hex'], expected)
            self.assertEqual(self.latest()['rx_received'], count)
            self.assertEqual(self.latest()['details']['configured']['write_hex'], register)
        self.assertEqual(self.bridge.command(0), 3)
        self.assertGreaterEqual(len(self.configs()), 6)
        before = len(self.configs())
        self.observer.publish('homeassistant/status', 'online').wait_for_publish(2)
        self.until(lambda: len(self.configs()) >= before+6)
        self.assertEqual(self.bridge.command(0), 3)
        self.assertTrue(all(not retained for topic, _, retained in self.messages if topic.endswith('/state')))

    def test_i2c_spi_uart_i2c_switch_reaches_display_on_one_compiled_owner(self):
        self.controller.connect()
        boot = None
        cases = (
            ('bmp388.md', 'chip_id', 119, None, 1, 1, '50'),
            ('ssd1306.md', 'display_off', None, None, 1, 0, ''),
            ('ld2450.md', 'raw_32', None, bytes(range(32)), 0, 32,
             ' '.join(f'{value:02X}' for value in range(32))),
            ('bmp388.md', 'chip_id', 119, None, 1, 1, '50'),
        )
        for revision, (file, operation, address, feed, tx, rx, expected) in enumerate(cases, 1):
            with self.subTest(operation=operation, revision=revision):
                before = [self.bridge.command(index) for index in (0, 1, 2, 5)]
                self.controller.select(ROOT/'datasheets'/file, operation, address)
                self.assertEqual(self.controller.load()['status'], 'ok')
                self.until(lambda: self.latest().get('status') == 'update_ok' and
                           (self.latest().get('details', {}).get('configured') or {}).get('revision') == revision)
                self.assertIsNone(self.latest()['raw_hex'])
                self.assertEqual([self.bridge.command(index) for index in (0, 1, 2, 5)], before)
                if feed is not None:
                    self.bridge.command(('uart_feed', feed))
                reply = self.controller.execute()
                self.assertEqual(reply['status'], 'ok')
                if boot is None:
                    boot = reply['boot_id']
                self.assertEqual(reply['boot_id'], boot)
                self.until(lambda: (self.latest().get('details', {}).get('last_result') or {}).get('request_id') == reply['request_id'])
                self.assertEqual((self.latest()['raw_hex'], self.latest()['tx_accepted'],
                                  self.latest()['rx_received']), (expected, tx, rx))
                self.assertEqual(self.latest()['details']['operation'], operation)
        self.assertEqual([self.bridge.command(index) for index in (0, 1, 2, 5)], [2, 1, 32, 0])
        self.assertEqual(len({topic for topic, _, _ in self.configs()}), 6)

    def test_retained_discovery_offline_will_and_host_reopen_have_no_raw_replay(self):
        self.controller.connect()
        self.select('chip_id')
        self.controller.execute()
        self.until(lambda: self.latest().get('raw_hex') == '50')
        self.messages.clear()
        # Abrupt socket loss sends no MQTT DISCONNECT; verify the real broker will.
        self.display.client._sock_close()
        self.until(lambda: any(topic.endswith('/availability') and payload == 'offline'
                               for topic, payload, _ in self.messages), pump=False)
        self.display.close()
        self.stop_observer()
        self.messages.clear()
        self.start_observer()
        self.until(lambda: len(self.configs()) == 6 and any(topic.endswith('/availability')
                   for topic, _, _ in self.messages), pump=False)
        self.assertTrue(all(retained for _, _, retained in self.configs()))
        self.assertEqual(self.latest(), {})  # State was never retained by the broker.
        self.controller.close()
        self.controller = self.open_controller()
        self.display = self.open_display()
        # Reopened journal alone is insufficient evidence of current ESP data.
        self.until(lambda: self.latest().get('status') == 'esp_disconnected', observe=False)
        self.assertIsNone(self.latest()['raw_hex'])
        self.assertEqual(self.bridge.command(0), 1)

    def test_broker_restart_reannounces_without_executing(self):
        self.controller.connect()
        self.select('chip_id')
        self.controller.execute()
        self.until(lambda: self.latest().get('raw_hex') == '50')
        self.broker.close()
        self.display.poll(self.controller.poll())
        self.assertFalse(self.display.status()['connected'])
        self.stop_observer()
        self.broker.start()
        self.bridge.reconnect()
        self.controller.connect()
        self.messages.clear()
        self.start_observer()
        self.until(lambda: len(self.configs()) >= 6 and self.latest().get('active_available'))
        self.assertEqual(self.bridge.command(0), 1)

    def test_session_option_publishes_results_and_closes_display(self):
        self.controller.close()  # The session worker creates its own journal owner.
        lines = [dict(action='select', path=str(ROOT/'datasheets/bmp388.md'), operation='chip_id', address=119),
                 dict(action='load'), dict(action='execute'), dict(action='status'), dict(action='quit')]
        output = io.StringIO()
        with patch.object(session.sys, 'stdin', io.StringIO('\n'.join(map(json.dumps, lines))+'\n')), \
                patch.object(session.sys, 'stdout', output):
            code = session.main(['--broker', '127.0.0.1', '--port', str(self.broker.port),
                                 '--ca', str(self.broker.ca), '--node', NODE,
                                 '--state', str(self.work/'session.sqlite3'), '--home-assistant'])
        self.assertEqual(code, 0, output.getvalue())
        self.until(lambda: any(topic.endswith('/availability') and payload == 'offline'
                               for topic, payload, _ in self.messages), pump=False)
        self.assertTrue(any(topic.endswith('/state') and json.loads(payload)['raw_hex'] == '50'
                            for topic, payload, _ in self.messages))
        self.assertEqual(self.bridge.command(0), 1)

    def test_display_connection_failure_does_not_stop_commands(self):
        self.controller.connect()
        with socket.socket() as unavailable:
            unavailable.bind(('127.0.0.1', 0))  # Bound but not listening.
            self.display.port = unavailable.getsockname()[1]
            state = self.display.poll(self.controller.snapshot())
            self.assertFalse(state['connected'])
            self.assertIsNotNone(state['error'])
            self.select('chip_id')
            self.assertEqual(self.controller.execute()['raw_hex'], '50')
        self.assertEqual(self.bridge.command(0), 1)


if __name__ == '__main__':
    unittest.main()
