"""Continuation tests use fake telemetry; they never open a serial port."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch
import zlib

from ai.protocol_capture import collect
from ai.protocol_resume import resume_plan
from ai.recording import save_new
from ai.tests import test_protocol as fixtures
from ai.tests.test_recording import Board, Clock
from ai.timing import digest
from tools.config_validator.planner import ConfigurationError


class ProtocolResumeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixtures.ProtocolTests.setUpClass()
        cls.fixture = fixtures.ProtocolTests()
        cls.study, cls.identity = cls.fixture.study, cls.fixture.identity

    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.capture = self.root / 'old'
        self.capture.mkdir()
        self.summary = dict(study_sha256=digest(self.study), selected_families=['all-current'],
                            restored=True, error='Collection stopped by operator', measured_costs=False, runs=[])
        for interval, rep in ((200, 0), (200, 1), (500, 0), (1000, 0), (1000, 1)):
            folder, result = self.fixture.record(self.capture / 'all-current', interval, rep,
                                                  radar_quality='fault' if interval == 200 else 'fresh')
            self.summary['runs'].append(dict(family_id='all-current', interval_ms=interval, repetition=rep,
                                            folder=str(folder), usable=result['usable'], reason=result['reason']))
        save_new(self.capture / 'study.json', self.study)
        save_new(self.capture / 'identity.json', self.identity)
        self.write_summary()

    def write_summary(self):
        (self.capture / 'summary.json').write_text(json.dumps(self.summary))

    def plan(self, **kwargs):
        return resume_plan(self.study, self.identity, [self.capture], ['all-current'], **kwargs)

    def test_complete_negative_runs_are_kept_and_only_three_runs_remain(self):
        plan = self.plan()
        self.assertEqual(len(plan['completed_runs']), 5)
        self.assertEqual([(r['interval_ms'], r['repetition']) for r in plan['missing_runs']],
                         [(500, 1), (2000, 0), (2000, 1)])
        self.assertFalse(plan['board_accessed'])

    def test_changed_journal_or_identity_cannot_be_skipped(self):
        path = self.capture / 'all-current/run-200-0/raw.jsonl'
        path.write_bytes(path.read_bytes() + b'\n')
        with self.assertRaisesRegex(ConfigurationError, 'hash mismatch'):
            self.plan()
        with self.assertRaisesRegex(ConfigurationError, 'identity changed'):
            resume_plan(self.study, dict(self.identity, recorded_utc='changed'), [self.capture], ['all-current'])

    def test_failed_batch_is_refused_before_any_board_action(self):
        self.summary['error'] = 'test_disconnect'
        self.write_summary()
        workflow = Mock()
        with self.assertRaisesRegex(ConfigurationError, 'failed or restoration'):
            collect(workflow, 'TEST-ONLY', self.study, self.identity, self.fixture.profile,
                    self.root / 'next', ['all-current'], previous_captures=[self.capture])
        workflow.dispatch.assert_not_called()
        self.assertFalse((self.root / 'next').exists())

    def test_duplicate_and_unlisted_attempts_are_rejected(self):
        with self.assertRaisesRegex(ConfigurationError, 'Duplicate capture'):
            resume_plan(self.study, self.identity, [self.capture, self.capture], ['all-current'])
        self.summary['runs'].append(dict(self.summary['runs'][0]))
        self.write_summary()
        with self.assertRaisesRegex(ConfigurationError, 'Duplicate previous'):
            self.plan()
        self.summary['runs'].pop()
        self.write_summary()
        (self.capture / 'all-current/run-500-1').mkdir()
        with self.assertRaisesRegex(ConfigurationError, 'Unlisted attempted'):
            self.plan()

    def test_missing_history_and_changed_observer_or_context_are_rejected(self):
        with self.assertRaisesRegex(ConfigurationError, 'Observer'):
            self.plan(collect_costs=True)
        save_new(self.capture / 'static-bindings.json', {'all-current': {'fixture': 'old'}})
        with self.assertRaisesRegex(ConfigurationError, 'Execution context'):
            self.plan(bindings={'all-current': {'fixture': 'changed'}})
        self.summary['previous_captures'] = [{'folder': 'omitted', 'summary_sha256': 'a' * 64}]
        self.write_summary()
        with self.assertRaisesRegex(ConfigurationError, 'earlier capture history'):
            self.plan()

    def test_collection_only_applies_missing_intervals_and_restores(self):
        profile = self.fixture.profile
        crc = zlib.crc32(json.dumps(profile, ensure_ascii=True, separators=(',', ':')).encode('ascii'))
        workflow = Mock()
        workflow.client = Board(Clock())
        def dispatch(action, body):
            if action == 'connect':
                return dict(state=dict(active_crc=crc, saved_crc=crc, flags=80))
            if action == 'review':
                return dict(review_id='restore')
            if action == 'apply':
                return dict(status=1, active_crc=crc, saved_crc=crc)
            raise AssertionError(action)
        workflow.dispatch.side_effect = dispatch
        with patch('ai.protocol_capture.apply_trial', return_value={}) as apply_mock, \
                patch('ai.protocol_capture.record_run', return_value=dict(usable=True, reason=None)) as record_mock:
            result = collect(workflow, 'TEST-ONLY', self.study, self.identity, profile,
                             self.root / 'next', ['all-current'], previous_captures=[self.capture])
        self.assertIsNone(result['error'])
        self.assertTrue(result['restored'])
        self.assertEqual({call.args[2] for call in apply_mock.call_args_list}, {500, 2000})
        self.assertEqual({(call.args[2], call.args[3]) for call in record_mock.call_args_list},
                         {(500, 1), (2000, 0), (2000, 1)})
        self.assertEqual(len(result['previously_completed_runs']), 5)
        self.assertEqual(result['previous_captures'][0]['summary_sha256'],
                         hashlib.sha256((self.capture / 'summary.json').read_bytes()).hexdigest())
        workflow.close.assert_called_once()

    def test_completed_family_does_not_open_board_or_create_output(self):
        for interval, rep in ((500, 1), (2000, 0), (2000, 1)):
            folder, result = self.fixture.record(self.capture / 'all-current', interval, rep)
            self.summary['runs'].append(dict(family_id='all-current', interval_ms=interval, repetition=rep,
                                            folder=str(folder), usable=result['usable'], reason=result['reason']))
        self.write_summary()
        workflow = Mock()
        with self.assertRaisesRegex(ConfigurationError, 'already complete'):
            collect(workflow, 'TEST-ONLY', self.study, self.identity, self.fixture.profile,
                    self.root / 'next', ['all-current'], previous_captures=[self.capture])
        workflow.dispatch.assert_not_called()
        self.assertFalse((self.root / 'next').exists())


if __name__ == '__main__':
    unittest.main()
