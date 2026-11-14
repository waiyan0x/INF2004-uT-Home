"""Real local TLS MQTT plus compiled C owner; bus effects remain simulated."""
import io
import json
import os
from pathlib import Path
import ssl
import tempfile
import unittest
from unittest.mock import patch

from tools.transaction.client import Mqtt, _MqttSocket
from tools.transaction.controller import Controller
from tools.transaction import session
from mqtt_fixture import LocalBroker, DeviceBridge, NODE
import test_tx_owner as owner

ROOT = Path(__file__).resolve().parents[2]


class MqttControllerTests(unittest.TestCase):
    setUpClass = classmethod(owner.OwnerTests.setUpClass.__func__)

    def setUp(self):
        owner.OwnerTests.setUp(self)
        folder = tempfile.TemporaryDirectory(prefix='uthome-tls-controller-')
        self.addCleanup(folder.cleanup)
        self.work = Path(folder.name)
        self.broker = LocalBroker(self.work)
        self.addCleanup(self.broker.close)
        self.broker.start()
        self.bridge = DeviceBridge(self.lib, self.broker)
        self.addCleanup(self.bridge.close)
        credentials = patch.dict(os.environ, {'UTHOME_MQTT_USER': 'controller-test',
                                              'UTHOME_MQTT_PASSWORD': self.broker.passwords['controller-test']})
        credentials.start()
        self.addCleanup(credentials.stop)
        self.path = self.work / 'session.sqlite3'

    def controller(self):
        return Controller(self.path, NODE, lambda: Mqtt('127.0.0.1', self.broker.port,
                                                        self.broker.ca, NODE, timeout=2))

    def test_real_broker_rule_switch_lost_reply_host_restart_and_broker_restart(self):
        controller = self.controller()
        try:
            controller.connect()
            controller.select(ROOT/'datasheets/bmp388.md', 'chip_id', 119)
            self.assertEqual(controller.load()['status'], 'ok')
            self.assertEqual(controller.execute()['raw_hex'], '50')
            controller.select(ROOT/'datasheets/bmp388.md', 'pressure_raw', 119)
            controller.load()
            self.assertEqual(controller.execute()['raw_hex'], '00 00 00')
            with self.assertRaises(ValueError):
                controller.select_rule({**controller.state['loaded']['rule'], 'read_length': 33})
            self.assertEqual(controller.execute()['raw_hex'], '00 00 00')
            self.bridge.command('drop')
            with self.assertRaises(TimeoutError):
                controller.execute()
            self.assertEqual(self.bridge.command(0), 4)
            controller.close()
            controller = self.controller()
            controller.connect()
            self.assertIsNotNone(controller.snapshot()['pending'])
            self.assertEqual(controller.recover()['raw_hex'], '00 00 00')
            self.assertEqual(self.bridge.command(0), 4)
            self.broker.close()
            with self.assertRaises((OSError, RuntimeError)):
                controller.refresh()
            self.broker.start()
            self.bridge.reconnect()
            controller.connect()
            self.assertEqual(self.bridge.command(0), 4)
            controller.select(ROOT/'datasheets/bmp388.md', 'chip_id', 119)
            controller.load()
            self.assertEqual(controller.execute()['raw_hex'], '50')
            self.assertEqual(self.bridge.command(0), 5)
        finally:
            controller.close()

    def test_verified_tls_rejects_wrong_hostname_and_wrong_broker_login(self):
        closed = []
        close = _MqttSocket.close
        def record_close(sock):
            close(sock)
            closed.append(sock.fileno())
        with patch.object(_MqttSocket, 'close', record_close):
            with self.assertRaises(ssl.SSLCertVerificationError):
                Mqtt('localhost', self.broker.port, self.broker.ca, NODE, timeout=2)
        self.assertTrue(closed)
        self.assertEqual(set(closed), {-1})
        with patch.dict(os.environ, {'UTHOME_MQTT_PASSWORD': 'wrong-test-password'}):
            with self.assertRaises((RuntimeError, ConnectionError)):
                Mqtt('127.0.0.1', self.broker.port, self.broker.ca, NODE, timeout=2)
        controller = self.controller()
        try:
            controller.connect()
            self.assertTrue(controller.snapshot()['connected'])
        finally:
            controller.close()

    def test_cli_serializes_explicit_commands_with_no_counters_in_input(self):
        lines = [dict(action='select', path=str(ROOT/'datasheets/bmp388.md'), operation='chip_id', address=119),
                 dict(action='load'), dict(action='execute'), dict(action='status'), dict(action='quit')]
        output = io.StringIO()
        code = session.run(self.controller, io.StringIO('\n'.join(json.dumps(line) for line in lines)+'\n'), output)
        self.assertEqual(code, 0, output.getvalue())
        events = [json.loads(line) for line in output.getvalue().splitlines()]
        replies = [event for event in events if event['event'] == 'command']
        self.assertEqual([event['action'] for event in replies], ['select', 'load', 'execute', 'status'])
        self.assertEqual(replies[2]['result']['raw_hex'], '50')
        self.assertEqual(self.bridge.command(0), 1)

    def test_cli_rejects_nonobjects_unknown_actions_and_oversized_lines(self):
        output = io.StringIO()
        code = session.run(self.controller, io.StringIO('false\nnull\n{"action":"run_anything"}\n'+'x'*65537), output)
        self.assertEqual(code, 1)
        errors = [json.loads(line) for line in output.getvalue().splitlines() if json.loads(line)['event'] == 'error']
        self.assertEqual(len(errors), 4, output.getvalue())
        self.assertEqual(self.bridge.command(0), 0)


if __name__ == '__main__':
    unittest.main()
