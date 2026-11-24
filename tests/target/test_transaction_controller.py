"""Durable host recovery against the actual C decision engine, without hardware."""
import ctypes as c
from copy import deepcopy
import json
from pathlib import Path
import sqlite3
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.transaction import client
from tools.transaction.controller import Controller, StateError, MAX_ID
from tools.transaction.journal import Journal
import test_txjob as engine
import test_tx_owner as owner

ROOT = Path(__file__).resolve().parents[2]
NODE = 'uthome-0123456789ab'


class Clock:
    def __init__(self):
        self.now = 10

    def __call__(self):
        return self.now


class EngineLink:
    def __init__(self, case):
        self.case = case

    def exchange(self, payload):
        case = self.case
        case.sent.append(payload)
        case.now += 1
        output = c.create_string_buffer(96)
        if payload[3] == 3:
            case.assertEqual(case.lib.txjob_query(case.state, payload, 96, case.generation,
                                                  case.idle, case.now, output), 0)
            return output.raw
        if case.before_send:
            case.before_send(payload)
        if case.drop == 'before':
            case.drop = None
            raise TimeoutError('injected loss before receipt')
        case.assertEqual(case.lib.txjob_submit(case.state, payload, 96, case.generation,
                                              case.idle, case.now), 0)
        if case.drop == 'pending':
            case.drop = None
            raise TimeoutError('injected loss while running')
        case.lib.txjob_step(case.state, case.now + 1)
        case.assertEqual(case.lib.txjob_take(case.state, output, 96), 0)
        if case.drop == 'after':
            case.drop = None
            raise TimeoutError('injected lost reply')
        return output.raw

    def poll(self):
        if self.case.offline:
            raise ConnectionError('injected broker loss')

    def close(self):
        self.case.closed_links += 1


class ControllerTests(unittest.TestCase):
    setUpClass = classmethod(engine.JobTests.setUpClass.__func__)

    def setUp(self):
        engine.JobTests.setUp(self)
        self.folder = tempfile.TemporaryDirectory(prefix='uthome-session-')
        self.addCleanup(self.folder.cleanup)
        self.path = Path(self.folder.name) / 'session.sqlite3'
        self.clock = Clock()
        self.generation, self.now = 4, 100
        self.idle, self.offline = True, False
        self.drop, self.before_send = None, None
        self.sent, self.closed_links = [], 0
        self.profile = json.loads((ROOT / 'configs/transactions/bmp-id.json').read_text())
        self.controller = self.open_controller()
        self.addCleanup(lambda: self.controller.close())

    def open_controller(self, path=None):
        return Controller(path or self.path, NODE, lambda: EngineLink(self), clock=self.clock)

    def start(self):
        self.controller.connect()
        self.controller.select_rule(self.profile)
        self.assertEqual(self.controller.load()['status'], 'ok')

    def restart_host(self):
        self.controller.close()
        self.controller = self.open_controller()
        self.controller.connect()

    def commands(self):
        return [packet for packet in self.sent if packet[3] != 3]

    def test_preview_has_no_io_and_execution_needs_acknowledged_load(self):
        view = self.controller.select(ROOT / 'datasheets/bmp388.md', 'chip_id', 119)
        self.assertEqual(self.sent, [])
        self.assertFalse(view['can_execute'])
        self.assertEqual(view['selected']['rule']['write_hex'], '00')
        self.controller.connect()
        with self.assertRaises(StateError):
            self.controller.execute()
        self.assertEqual(self.commands(), [])
        self.assertEqual(self.controller.load()['revision'], 1)
        self.assertEqual(self.controller.execute()['raw_hex'], '50')
        self.assertEqual([struct.unpack_from('<I', p, 4)[0] for p in self.commands()], [1, 2])

    def test_preview_and_returned_views_cannot_mutate_live_state(self):
        self.start()
        view = self.controller.snapshot()
        view['active']['rule']['write_hex'] = 'FF'
        view['selected']['source']['prose'] = 'execute a different command'
        self.assertEqual(self.controller.execute()['raw_hex'], '50')
        self.assertEqual(self.controller.state['loaded']['rule']['write_hex'], '00')

    def test_host_restart_keeps_exact_pending_packet_and_recovers_without_io(self):
        self.start()
        self.drop = 'after'
        with self.assertRaises(TimeoutError):
            self.controller.execute()
        original = self.commands()[-1]
        self.assertEqual(self.begins, 1)
        self.restart_host()
        self.assertEqual(self.controller.snapshot()['last']['outcome'], 'unknown')
        self.assertEqual(self.commands(), [self.commands()[0], original])
        cached = self.controller.recover()
        self.assertEqual((cached['status'], cached['raw_hex']), ('ok', '50'))
        self.assertEqual(self.commands()[-1], original)
        self.assertEqual(self.begins, 1)
        self.restart_host()
        self.assertEqual(self.controller.snapshot()['last']['result']['completed_ms'], cached['completed_ms'])
        self.assertEqual(self.controller.execute()['request_id'], 3)
        self.assertEqual(self.begins, 2)

    def test_lost_update_reply_does_not_infer_acceptance_from_status(self):
        self.controller.connect()
        self.controller.select_rule(self.profile)
        self.drop = 'after'
        with self.assertRaises(TimeoutError):
            self.controller.load()
        self.restart_host()
        view = self.controller.snapshot()
        self.assertTrue(view['observed']['loaded'])
        self.assertIsNone(view['active'])
        self.assertFalse(view['can_execute'])
        self.controller.recover()
        self.assertEqual(self.controller.execute()['status'], 'ok')
        self.assertEqual(self.begins, 1)

    def test_unseen_command_is_never_replayed_and_new_boot_keeps_unknown_outcome(self):
        self.start()
        self.drop = 'before'
        with self.assertRaises(TimeoutError):
            self.controller.execute()
        self.restart_host()
        count = len(self.commands())
        with self.assertRaisesRegex(StateError, 'not recoverable'):
            self.controller.recover()
        with self.assertRaises(StateError):
            self.controller.load()
        self.assertEqual(len(self.commands()), count)
        self.assertEqual(self.begins, 0)
        self.assertEqual(self.lib.txjob_init(self.state, c.byref(self.ops), engine.BOOT + 1), 0)
        self.controller.refresh()
        view = self.controller.snapshot()
        self.assertIsNone(view['pending'])
        self.assertEqual(view['last']['outcome'], 'unknown')
        self.assertIn('restarted', view['last']['reason'])
        self.assertIsNone(view['active'])
        with self.assertRaises(StateError):
            self.controller.execute()
        self.assertEqual(self.controller.load()['request_id'], 1)
        self.assertEqual(self.controller.execute()['request_id'], 2)

    def test_running_command_recovery_waits_for_completion_without_restarting_it(self):
        self.start()
        self.drop = 'pending'
        with self.assertRaises(TimeoutError):
            self.controller.execute()
        self.restart_host()
        self.assertTrue(self.controller.snapshot()['observed']['busy'])
        before = len(self.commands())
        with self.assertRaises(StateError):
            self.controller.recover()
        self.assertEqual(len(self.commands()), before)
        self.lib.txjob_step(self.state, self.now + 1)
        original = c.create_string_buffer(96)
        self.assertEqual(self.lib.txjob_take(self.state, original, 96), 0)
        self.assertEqual(self.controller.recover()['completed_ms'], client.result(original.raw)['completed_ms'])
        self.assertEqual(self.begins, 1)

    def check_recovery_session_change(self, change):
        self.start()
        self.drop = 'after'
        with self.assertRaises(TimeoutError):
            self.controller.execute()
        self.restart_host()
        self.before_send = change
        with self.assertRaises(ValueError):
            self.controller.recover()
        self.assertEqual(self.begins, 1)
        self.assertIsNotNone(self.controller.snapshot()['pending'])
        self.before_send = None
        self.controller.connect()
        view = self.controller.snapshot()
        self.assertIsNone(view['pending'])
        self.assertIsNone(view['active'])
        self.assertEqual(view['last']['outcome'], 'unknown')
        self.assertEqual(self.begins, 1)

    def test_esp_reboot_between_recovery_query_and_retry_never_reexecutes(self):
        self.check_recovery_session_change(
            lambda packet: self.lib.txjob_init(self.state, c.byref(self.ops), engine.BOOT + 1))

    def test_plan_change_between_recovery_query_and_retry_never_reexecutes(self):
        self.check_recovery_session_change(lambda packet: setattr(self, 'generation', 5))

    def test_generation_change_invalidates_loaded_rule_and_preserves_unknown(self):
        self.start()
        self.drop = 'after'
        with self.assertRaises(TimeoutError):
            self.controller.execute()
        self.generation += 1
        self.restart_host()
        self.assertIsNone(self.controller.snapshot()['active'])
        self.assertIsNone(self.controller.snapshot()['pending'])
        self.assertEqual(self.controller.snapshot()['last']['outcome'], 'unknown')
        self.assertIn('plan changed', self.controller.snapshot()['last']['reason'])
        self.assertEqual(self.controller.load()['request_id'], 3)
        self.assertEqual(self.controller.execute()['generation'], 5)

    def test_rejected_update_preserves_known_active_rule_and_clears_display(self):
        self.start()
        self.controller.execute()
        self.controller.refresh()
        self.assertTrue(self.controller.snapshot()['display']['available'])
        self.controller.select_rule({**self.profile, 'write_hex': '04', 'read_length': 3})
        self.before_send = lambda packet: setattr(self, 'idle', False)
        self.assertEqual(self.controller.load()['status'], 'busy')
        self.before_send, self.idle = None, True
        self.assertFalse(self.controller.snapshot()['display']['available'])
        self.assertEqual(self.controller.execute()['revision'], 1)
        self.assertEqual(self.begins, 2)
        before = deepcopy(self.controller.state['selected'])
        with self.assertRaises(ValueError):
            self.controller.select_rule({**self.profile, 'read_length': 33})
        self.assertEqual(self.controller.state['selected'], before)

    def test_zero_bytes_expiry_failed_read_and_disconnect_never_show_stale_success(self):
        self.start()
        self.data = b'\0'
        self.assertEqual(self.controller.execute()['raw_hex'], '00')
        self.controller.refresh()
        self.assertEqual(self.controller.snapshot()['display']['raw_hex'], '00')
        self.clock.now += 5
        self.assertFalse(self.controller.snapshot()['display']['available'])
        self.now += 5000
        self.controller.refresh()
        self.assertFalse(self.controller.snapshot()['display']['available'])
        self.poll_status = 4
        self.assertEqual(self.controller.execute()['status'], 'timeout')
        self.controller.refresh()
        self.assertIsNone(self.controller.snapshot()['display']['raw_hex'])
        self.offline = True
        self.controller.poll()
        self.assertFalse(self.controller.snapshot()['connected'])
        self.assertFalse(self.controller.snapshot()['can_execute'])

    def test_five_second_expiry_boundary_uses_original_esp_completion(self):
        self.start()
        self.data = b'\0'
        reply = self.controller.execute()
        for age in (0, 4999, 5000, 5001):
            with self.subTest(age_ms=age):
                # The next query increments the simulated ESP clock by one ms.
                self.now = reply['completed_ms'] + age - 1
                self.controller.refresh()
                display = self.controller.snapshot()['display']
                self.assertEqual(display['age_ms'], age)
                self.assertEqual(display['available'], age < 5000)
                self.assertEqual(display['raw_hex'], '00' if age < 5000 else None)
                self.assertEqual(self.begins, 1)

    def test_recovered_result_retains_original_completion_time_and_age(self):
        self.start()
        self.drop = 'after'
        with self.assertRaises(TimeoutError):
            self.controller.execute()
        self.now += 6000
        self.restart_host()
        answer = self.controller.recover()
        self.assertLess(answer['completed_ms'], 1000)
        self.assertFalse(self.controller.snapshot()['display']['available'])

    def test_periodic_reconnect_queries_only_and_never_replays_unknown_write(self):
        self.start()
        self.drop = 'after'
        with self.assertRaises(TimeoutError):
            self.controller.execute()
        count = len(self.commands())
        self.controller.poll()
        self.assertFalse(self.controller.snapshot()['connected'])
        self.clock.now += 2
        self.controller.poll()
        self.assertTrue(self.controller.snapshot()['connected'])
        self.assertEqual(len(self.commands()), count)
        self.assertIsNotNone(self.controller.snapshot()['pending'])
        self.assertEqual(self.begins, 1)

    def test_nonidle_plan_missing_journal_and_foreign_history_refuse_commands(self):
        self.idle = False
        self.controller.connect()
        self.controller.select_rule(self.profile)
        with self.assertRaises(StateError):
            self.controller.load()
        self.assertEqual(self.commands(), [])
        self.idle = True
        self.controller.load()
        other = self.open_controller(Path(self.folder.name) / 'lost.sqlite3')
        try:
            other.connect()
            self.assertIn('unowned_session', other.snapshot()['issue'])
            with self.assertRaises(StateError):
                other.load()
        finally:
            other.close()
        foreign = client.request(self.profile, 20, self.generation, engine.BOOT)
        self.assertEqual(self.lib.txjob_submit(self.state, foreign, 96, self.generation, True, self.now), 0)
        out = c.create_string_buffer(96)
        self.lib.txjob_take(self.state, out, 96)
        self.controller.refresh()
        self.assertIn('foreign_command', self.controller.snapshot()['issue'])
        with self.assertRaises(StateError):
            self.controller.execute()

    def test_wrong_result_settings_or_identity_leave_exact_pending_command(self):
        self.start()
        exchange = self.controller.link.exchange
        def wrong(payload):
            wire = exchange(payload)
            if payload[3] == 2:
                data = bytearray(wire)
                data[84] = 118
                return bytes(data)
            return wire
        self.controller.link.exchange = wrong
        with self.assertRaisesRegex(ValueError, 'different transaction'):
            self.controller.execute()
        self.assertIsNotNone(self.controller.snapshot()['pending'])
        self.assertEqual(self.controller.snapshot()['last']['outcome'], 'unknown')
        self.restart_host()
        self.assertEqual(self.controller.recover()['address'], 119)
        self.assertEqual(self.begins, 1)

    def test_journal_write_failure_prevents_publication(self):
        self.start()
        count = len(self.commands())
        with patch.object(self.controller.journal, 'write', side_effect=sqlite3.OperationalError('disk full')):
            with self.assertRaises(sqlite3.OperationalError):
                self.controller.execute()
        self.assertEqual(len(self.commands()), count)
        self.assertTrue(self.controller.snapshot()['journal_fault'])
        with self.assertRaises(StateError):
            self.controller.connect()

    def test_save_failure_after_io_leaves_recoverable_pending_packet(self):
        self.start()
        save = self.controller.journal.write
        def fail_completion(state):
            if state['last'] and state['last']['reply'] is not None and state['last']['command']['kind'] == 'execute':
                raise sqlite3.OperationalError('disk full after I/O')
            save(state)
        with patch.object(self.controller.journal, 'write', side_effect=fail_completion):
            with self.assertRaises(sqlite3.OperationalError):
                self.controller.execute()
        self.assertEqual(self.begins, 1)
        self.restart_host()
        self.assertEqual(self.controller.recover()['status'], 'ok')
        self.assertEqual(self.begins, 1)

    def test_exact_packet_is_committed_before_transport_receives_it(self):
        self.start()
        def inspect(payload):
            pending = self.controller.journal.read()['pending']
            self.assertEqual(pending['packet'], payload.hex())
        self.before_send = inspect
        self.controller.execute()

    def test_id_and_revision_exhaustion_do_not_wrap_or_publish(self):
        self.start()
        state = deepcopy(self.controller.state)
        state['high_id'] = MAX_ID
        self.controller._save(state)
        count = len(self.commands())
        with self.assertRaises(ValueError):
            self.controller.execute()
        self.assertEqual(len(self.commands()), count)
        self.assertFalse(self.controller.snapshot()['can_execute'])
        state = deepcopy(self.controller.state)
        state['revision'] = MAX_ID
        self.controller._save(state)
        with self.assertRaises(ValueError):
            self.controller.select_rule(self.profile)

    def test_exclusive_owner_wrong_node_and_corrupt_state_fail_closed(self):
        with self.assertRaises(sqlite3.OperationalError):
            self.open_controller()
        self.controller.close()
        with self.assertRaises(ValueError):
            Controller(self.path, 'uthome-ffffffffffff', lambda: EngineLink(self))
        self.controller = self.open_controller()
        self.start()
        self.drop = 'before'
        with self.assertRaises(TimeoutError):
            self.controller.execute()
        self.controller.close()
        journal = Journal(self.path)
        state = journal.read()
        state['pending']['packet'] = '00' * 96
        journal.write(state)
        journal.close()
        with self.assertRaises(ValueError):
            self.open_controller()
        # Cleanup remains valid without repairing or silently resetting the file.
        self.controller = type('Closed', (), {'close': lambda self: None})()

    def test_journal_lock_excludes_a_second_process(self):
        run = subprocess.run([sys.executable, '-c',
                              'import sys; from tools.transaction.journal import Journal; Journal(sys.argv[1])',
                              str(self.path)], cwd=ROOT, capture_output=True, text=True)
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('database is locked', run.stderr)

    def test_process_crash_after_commit_preserves_unsent_unknown_command(self):
        self.controller.connect()
        status = self.controller.link.exchange(client.query(321))
        self.controller.close()
        code = '''
import os,sys
from tools.transaction.controller import Controller
from tools.transaction import client
class Link:
    def exchange(self,p):
        if p[3] != 3: os._exit(77)
        result=bytearray(bytes.fromhex(sys.argv[2]));result[4:8]=p[4:8];return bytes(result)
    def close(self): pass
c=Controller(sys.argv[1],sys.argv[3],Link)
c.connect()
import json
c.select_rule(json.loads(sys.argv[4]))
c.load()
'''
        run = subprocess.run([sys.executable, '-c', code, str(self.path), status.hex(), NODE,
                              json.dumps(self.profile)], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(run.returncode, 77, run.stderr)
        self.controller = self.open_controller()
        self.controller.connect()
        self.assertIsNotNone(self.controller.snapshot()['pending'])
        with self.assertRaises(StateError):
            self.controller.recover()
        self.assertEqual(self.begins, 0)


class OwnerControllerTests(unittest.TestCase):
    setUpClass = classmethod(owner.OwnerTests.setUpClass.__func__)
    setUp = owner.OwnerTests.setUp
    pump = owner.OwnerTests.pump

    def test_one_compiled_owner_switches_a_b_rejected_b_a_with_automatic_ids(self):
        case = self
        class Link:
            def exchange(self, packet):
                case.assertEqual(case.lib.txptest_send(packet, 96), 0)
                out = c.create_string_buffer(96)
                for _ in range(600):
                    case.pump()
                    if case.lib.txptest_reply(out) == 0:
                        return out.raw
                raise TimeoutError('fixture deadline')
            def poll(self):
                pass
            def close(self):
                pass
        with tempfile.TemporaryDirectory(prefix='uthome-owner-session-') as folder:
            controller = Controller(Path(folder) / 'session.sqlite3', NODE, Link)
            try:
                controller.connect()
                controller.select(ROOT / 'datasheets/bmp388.md', 'chip_id', 119)
                controller.load()
                self.assertEqual(controller.execute()['raw_hex'], '50')
                controller.select(ROOT / 'datasheets/bmp388.md', 'pressure_raw', 119)
                controller.load()
                self.assertEqual(controller.execute()['raw_hex'], '00 00 00')
                with self.assertRaises(ValueError):
                    controller.select_rule({**controller.state['loaded']['rule'], 'read_length': 33})
                self.assertEqual(controller.execute()['raw_hex'], '00 00 00')
                controller.select(ROOT / 'datasheets/bmp388.md', 'chip_id', 119)
                controller.load()
                self.assertEqual(controller.execute()['raw_hex'], '50')
                self.assertEqual(self.lib.txptest_observed(0), 4)
                self.assertEqual(controller.snapshot()['last']['result']['request_id'], 7)
            finally:
                controller.close()


if __name__ == '__main__':
    unittest.main()
