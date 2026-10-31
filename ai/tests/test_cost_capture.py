"""Measurement journals reject lost identity, allocation activity and resets."""
from copy import deepcopy
import json
import unittest

from ai import cost_capture
from ai.recording import load_recording, record_run
from ai.tests import test_recording
from tools.config_transfer.telemetry import AUDIT_KINDS
from tools.config_transfer.workcost import WorkcostReply
from tools.config_validator.planner import ConfigurationError


def audit_fixture():
    heap = dict(allocations=0, bytes=0, frees=0,
                kernel_calls={name: 0 for name in ('malloc', 'calloc', 'realloc', 'free')})
    return dict(sealed=True, saturated=False, application_startup=deepcopy(heap),
                normal=dict(heap, creates={name: 0 for name in AUDIT_KINDS}))


class CostCaptureTests(unittest.TestCase):
    def setUp(self):
        self.fixture = test_recording.RecordingTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.cost_calls = 0
        self.corrupt = lambda response: None
        self.fixture.board.mutate = lambda data, _: data.update(memory_audit=audit_fixture())
        self.fixture.board.workcost = self.workcost

    def workcost(self):
        self.cost_calls += 1
        count = self.cost_calls
        stats = dict(steps=count, elapsed_us=count*100, max_elapsed_us=100, max_lateness_us=0,
                     bus_wait_us=count*2, bus_active_us=count*20, transfer_bytes=count*16,
                     transfers=count, timeouts=0, errors=0, flags=0)
        rows = [dict(slot=i, source_index=0 if i == 0 else None, task_handle=17 if i == 0 else None,
                     bound=i == 0, available=i == 0, stats=stats if i == 0 else None,
                     stack=dict(bytes=8192, unused_min=7000, measured=True, empirical=True)) for i in range(4)]
        response = dict(result=0, generation=3, active_revision=self.fixture.board.config['active_revision'],
                        active_crc=self.fixture.board.config['active_crc'],
                        timestamp_us=round(self.fixture.clock.now*1000000), capture_us=50,
                        controller_stack=dict(bytes=8448, unused_min=4000, measured=True, empirical=True),
                        idle_stack=dict(bytes=1024, unused_min=896, measured=True, empirical=True), workers=rows)
        self.corrupt(response)
        return WorkcostReply(**response)

    def record(self, name):
        f = self.fixture
        folder = f.root / name
        result = record_run(f.board, f.experiment, 500, 0, f.identity, f.receipt, folder,
                            clock=f.clock.time, sleep=f.clock.sleep, collect_costs=True)
        return folder, result

    def test_journal_and_summary_replay_without_changing_labels(self):
        folder, result = self.record('costs')
        self.assertTrue(result['usable'] and result['passes'])
        load_recording(folder)
        summary = json.loads((folder/'workcost.json').read_text())
        self.assertGreater(summary['observations'], 10)
        self.assertGreater(summary['window']['workers'][0]['transfers'], 0)
        self.assertFalse(summary['model_features'])
        summary['observer_max_us'] += 1
        (folder/'workcost.json').write_text(json.dumps(summary))
        with self.assertRaisesRegex(ConfigurationError, 'differs'):
            load_recording(folder)

    def test_bad_identity_and_unavailable_costs_stop_without_retry(self):
        cases = {
            'generation': lambda d: d.update(generation=4),
            'revision': lambda d: d.update(active_revision=999),
            'crc': lambda d: d.update(active_crc=0),
            'missing': lambda d: d['workers'][0].update(available=False, stats=None),
            'saturated': lambda d: d['workers'][0]['stats'].update(flags=1),
            'margin': lambda d: d['controller_stack'].update(unused_min=512),
        }
        for name, change in cases.items():
            self.corrupt = change
            self.cost_calls = 0
            folder, result = self.record(name)
            self.assertFalse(result['usable'], name)
            self.assertEqual(self.cost_calls, 1, name)
            self.assertTrue((folder/'raw.jsonl').exists())
            self.assertFalse((folder/'workcost.json').exists())

    def test_midrun_counter_reset_is_not_a_new_independent_window(self):
        def corrupt(data):
            if self.cost_calls == 5:
                data['workers'][0]['stats']['steps'] = 1
        self.corrupt = corrupt
        _, result = self.record('reset')
        self.assertFalse(result['usable'])
        self.assertEqual(result['reason'], 'workcost_counter_reset')

    def test_allocation_phases_cannot_be_missing_or_nonzero(self):
        snapshot = dict(memory_audit=audit_fixture())
        cost_capture.check_audit(snapshot)
        for phase in ('application_startup', 'normal'):
            for field in ('allocations', 'bytes', 'frees'):
                bad = deepcopy(snapshot)
                bad['memory_audit'][phase][field] = 1
                with self.assertRaises(ConfigurationError):
                    cost_capture.check_audit(bad)
            bad = deepcopy(snapshot)
            bad['memory_audit'][phase]['kernel_calls']['malloc'] = 1
            with self.assertRaises(ConfigurationError):
                cost_capture.check_audit(bad)
        for bad in (dict(memory_audit=None), dict(memory_audit=dict(audit_fixture(), sealed=False)),
                    dict(memory_audit=dict(audit_fixture(), saturated=True))):
            with self.assertRaises(ConfigurationError):
                cost_capture.check_audit(bad)
