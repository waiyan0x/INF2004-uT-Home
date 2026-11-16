"""Desk actions use the real controller and compiled C engine through one owner."""
from copy import deepcopy
import http.client
import json
import threading
import time
import unittest

from tools.configurator.runtime import Runtime, catalogue
from tools.configurator.web import make_server
from tools.transaction.controller import Controller
import test_transaction_controller as control


class RuntimeTests(unittest.TestCase):
    setUpClass = classmethod(control.ControllerTests.setUpClass.__func__)
    open_controller = control.ControllerTests.open_controller

    def setUp(self):
        control.ControllerTests.setUp(self)
        self.controller.close()
        self.block_query = threading.Event()
        self.query_entered = threading.Event()
        self.release_query = threading.Event()

        def connector():
            link = control.EngineLink(self)
            exchange = link.exchange

            def delayed(packet):
                if packet[3] == 3 and self.block_query.is_set():
                    self.query_entered.set()
                    if not self.release_query.wait(5):
                        raise TimeoutError('test query delay')
                return exchange(packet)

            link.exchange = delayed
            return link

        self.runtime = Runtime(lambda: Controller(self.path, control.NODE, connector, clock=self.clock),
                               target=dict(node=control.NODE))
        self.addCleanup(self.runtime.close)
        self.addCleanup(self.release_query.set)
        self.description = next(item for item in catalogue() if item['id'] == 'bmp388.md')

    def wait(self):
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            view = self.runtime.snapshot()
            if not view['busy']:
                return view
            time.sleep(0.01)
        self.fail('runtime action did not settle')

    def action(self, action, **values):
        self.runtime.dispatch(dict(action=action, **values))
        return self.wait()

    def load(self, operation='chip_id', **values):
        return self.action('load', **(dict(description='bmp388.md', operation=operation,
            address=119, sha256=self.description['sha256']) | values))

    def test_catalogue_and_startup_do_not_connect_or_execute(self):
        self.assertEqual(self.sent, [])
        self.assertFalse(self.runtime.snapshot()['enabled'])
        preview = self.description['operations'][1]['rules']['119']
        self.assertEqual((preview['write_hex'], preview['read_length']), ('04', 3))
        unsupported = next(item for item in catalogue() if item['id'] == 'sen0390.md')
        self.assertEqual(unsupported['operations'][0]['rules'], {})
        self.assertTrue(unsupported['operations'][0]['reason'])
        self.assertEqual(self.sent, [])
        view = self.action('connect')
        self.assertTrue(view['state']['ready'])
        self.assertEqual(self.begins, 0)

    def test_load_execute_switch_and_disconnect_have_explicit_io(self):
        self.action('connect')
        for number, operation in enumerate(('chip_id', 'pressure_raw', 'chip_id'), 1):
            self.data = b'\0\0\0' if operation == 'pressure_raw' else b'\x50'
            view = self.load(operation)
            self.assertIsNone(view['error'])
            self.assertEqual(view['display']['status'], 'update_ok')
            self.assertFalse(view['display']['data_available'])
            self.assertEqual(self.begins, number - 1)
            view = self.action('execute')
            self.assertEqual(view['display']['status'], 'execute_ok')
            self.assertEqual(view['display']['raw_hex'], '00 00 00' if operation == 'pressure_raw' else '50')
            self.assertEqual(view['state']['active']['rule']['revision'], number)
            self.assertEqual(self.begins, number)
        view = self.action('disconnect')
        self.assertFalse(view['enabled'])
        self.assertFalse(view['display']['data_available'])
        self.assertIsNotNone(view['state']['last'])
        self.action('connect')
        self.assertEqual(self.begins, 3)

    def test_invalid_or_changed_description_cannot_replace_active_rule(self):
        self.action('connect')
        self.load()
        active = deepcopy(self.runtime.snapshot()['state']['active'])
        for values in (dict(sha256='0' * 64), dict(description='../README.md'),
                       dict(operation='missing'), dict(address=120)):
            view = self.load(**values)
            self.assertTrue(view['error'])
            self.assertEqual(view['state']['active'], active)
        source = next(item for item in catalogue() if item['id'] == 'sen0390.md')
        view = self.load(operation='illumination_raw', description=source['id'],
                         sha256=source['sha256'], address=source['addresses'][0])
        self.assertIn('unsupported', view['error'])
        self.assertEqual(self.begins, 0)

    def test_expiry_and_rejected_load_preserve_receipt_without_live_bytes(self):
        self.action('connect')
        self.load()
        self.data = b'\0'
        view = self.action('execute')
        self.assertEqual(view['display']['raw_hex'], '00')
        self.clock.now += 5
        self.now += 5000
        view = self.action('status')
        self.assertEqual(view['display']['status'], 'result_expired')
        self.assertIsNone(view['display']['raw_hex'])
        self.assertEqual(view['state']['last']['result']['raw_hex'], '00')
        self.before_send = lambda packet: setattr(self, 'idle', False)
        view = self.load('pressure_raw')
        self.assertEqual(view['display']['status'], 'update_busy')
        self.assertEqual(view['state']['active']['rule']['read_length'], 1)
        self.assertEqual(self.begins, 1)

    def test_unknown_completion_blocks_new_io_and_recovers_exact_result(self):
        self.action('connect')
        self.load()
        self.drop = 'after'
        view = self.action('execute')
        self.assertEqual(view['display']['status'], 'outcome_unknown')
        self.assertIsNone(view['display']['raw_hex'])
        self.assertFalse(view['state']['can_execute'])
        self.assertEqual(self.begins, 1)
        self.action('connect')
        self.action('execute')
        self.assertEqual(self.begins, 1)
        view = self.action('recover')
        self.assertIsNone(view['state']['pending'])
        self.assertEqual(view['display']['status'], 'execute_ok')
        self.assertEqual(self.begins, 1)

    def test_busy_action_is_not_queued_twice_and_snapshots_are_copied(self):
        self.block_query.set()
        self.runtime.dispatch(dict(action='connect'))
        self.assertTrue(self.query_entered.wait(2))
        with self.assertRaisesRegex(ValueError, 'pending'):
            self.runtime.dispatch(dict(action='execute'))
        view = self.runtime.snapshot()
        view['target']['node'] = 'changed'
        self.assertEqual(self.runtime.snapshot()['target']['node'], control.NODE)
        self.release_query.set()
        self.wait()
        self.assertEqual(self.begins, 0)

    def test_stalled_background_query_expires_copied_snapshot(self):
        self.action('connect')
        self.load()
        self.action('execute')
        self.block_query.set()
        self.clock.now += 1
        self.assertTrue(self.query_entered.wait(2))
        time.sleep(2.05)
        view = self.runtime.snapshot()
        self.assertFalse(view['state']['status_fresh'])
        self.assertFalse(view['state']['can_execute'])
        self.assertIsNone(view['display']['raw_hex'])
        self.assertEqual(self.begins, 1)

    def test_http_actions_keep_session_checks_and_reject_arbitrary_inputs(self):
        server = make_server(0, runtime=self.runtime)
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        self.addCleanup(server.workflow.close)
        self.addCleanup(server.server_close)
        self.addCleanup(thread.join)
        self.addCleanup(server.shutdown)

        def request(body, token):
            connection = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=3)
            try:
                connection.request('POST', '/api/runtime', json.dumps(body),
                                   {'Content-Type': 'application/json', 'X-Session': token})
                reply = connection.getresponse()
                return reply.status, json.loads(reply.read())
            finally:
                connection.close()

        status, _ = request(dict(action='connect'), 'wrong')
        self.assertEqual(status, 403)
        self.assertEqual(self.sent, [])
        status, _ = request(dict(action='snapshot'), server.token)
        self.assertEqual(status, 200)
        for body in (dict(action='execute', extra=True), dict(action='quit'), [],
                     dict(action='load', description='../private.json')):
            status, _ = request(body, server.token)
            self.assertEqual(status, 400)
        self.assertEqual(self.sent, [])
