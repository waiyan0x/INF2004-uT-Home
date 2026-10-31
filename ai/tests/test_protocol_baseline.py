"""Independent-cost comparisons use synthetic fixtures, never bench labels."""
from copy import deepcopy
import hashlib
import json
import unittest
from unittest.mock import patch

from ai.protocol_baseline import assess_calibrated, load_calibration
from ai.protocol_model import train
from ai.recording import load_recording
from ai.tests import test_protocol as fixtures
from ai.tests import test_cost_capture as cost_fixtures
from ai.timing import digest
from tools.config_validator.planner import ConfigurationError


class CalibratedGateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixtures.ProtocolTests.setUpClass()
        fixture = fixtures.ProtocolTests()
        cls.study = fixture.study
        cls.data = fixture.fixture_dataset()
        for index, row in enumerate(cls.data['rows']):
            row['evidence'] = [dict(run_sha256=digest(['trial', index]), raw_sha256=digest(['raw', index]))]
        with patch('ai.protocol_model.dataset', return_value=cls.data):
            cls.model, _, _ = train(cls.study, [])
        cls.baseline = dict(study_sha256=digest(cls.study), baseline_file_sha256='c' * 64,
            families=[dict(family_id=f, margin_ms=1, calibrated_rule_interval_ms=500,
                           calibration_run_sha256=digest(['cal', f]), raw_sha256=digest(['cal-raw', f]))
                      for ids in cls.study['split'].values() for f in ids])

    def test_equal_calibrated_rule_is_not_benefit(self):
        report = assess_calibrated(self.study, self.model, self.data, self.baseline)
        self.assertIn('no_measured_benefit_over_calibrated_rule', report['reasons'])
        self.assertFalse(report['statistical_criteria_passed'])
        self.assertFalse(report['deployment_allowed'])
        self.assertFalse(report['automatic_apply'])

    def test_extra_comparison_cannot_rescue_failed_original_gate(self):
        baseline = deepcopy(self.baseline)
        for entry in baseline['families']:
            entry['calibrated_rule_interval_ms'] = 200
        report = assess_calibrated(self.study, self.model, self.data, baseline)
        self.assertTrue(all(v['safe_interval_gain'] for v in report['partitions']['test'].values()))
        self.assertIn('no_measured_benefit_over_simple_rule', report['reasons'])
        self.assertFalse(report['statistical_criteria_passed'])

    def test_passing_calibrated_choice_cannot_regress(self):
        data, baseline = deepcopy(self.data), deepcopy(self.baseline)
        for row in data['rows']:
            row['label'] = int(row['interval_ms'] <= 1000)
        for entry in baseline['families']:
            entry['calibrated_rule_interval_ms'] = 1000
        model = dict(self.model, dataset_sha256=digest(data))
        report = assess_calibrated(self.study, model, data, baseline)
        self.assertIn('heldout_regression_against_safe_calibrated_rule', report['reasons'])
        self.assertFalse(report['statistical_criteria_passed'])

    def test_bad_rule_choices_and_abstentions_are_reported_separately(self):
        for chosen in (2000, None):
            baseline = deepcopy(self.baseline)
            for entry in baseline['families']:
                entry['calibrated_rule_interval_ms'] = chosen
            report = assess_calibrated(self.study, self.model, self.data, baseline)
            for value in report['partitions']['test'].values():
                self.assertTrue(value['safe_coverage_gain'])
                self.assertEqual(value['calibrated_rule_violation'], chosen is not None)
                self.assertEqual(value['calibrated_rule_abstention'], chosen is None)

    def test_calibration_leakage_and_missing_families_are_rejected(self):
        baseline = deepcopy(self.baseline)
        baseline['families'][0]['raw_sha256'] = self.data['rows'][0]['evidence'][0]['raw_sha256']
        with self.assertRaisesRegex(ConfigurationError, 'leaked'):
            assess_calibrated(self.study, self.model, self.data, baseline)
        baseline = deepcopy(self.baseline)
        baseline['families'].pop()
        with self.assertRaisesRegex(ConfigurationError, 'Missing or duplicate'):
            assess_calibrated(self.study, self.model, self.data, baseline)


class CalibrationReplayTests(unittest.TestCase):
    def setUp(self):
        self.fixture = cost_fixtures.CostCaptureTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.root = self.fixture.fixture.root
        self.family = self.fixture.fixture.experiment['scenario']['family_id']
        self.folder, result = self.fixture.record(f'calibration/{self.family}/observation')
        self.assertTrue(result['usable'])
        experiment, run = load_recording(self.folder)
        # One-family fixture exercises raw replay; production validation still
        # requires a complete prospectively prepared multi-family study.
        self.study = dict(cases=[dict(experiment=experiment)], specification=dict(cadence_ms=200))
        self.entry = dict(family_id=self.family, margin_ms=1, calibrated_rule_interval_ms=500,
                          healthy=True, calibration_run_sha256=digest(run), raw_sha256=run['capture']['raw_sha256'],
                          workcost_sha256=hashlib.sha256((self.folder / 'workcost.json').read_bytes()).hexdigest())
        self.summary = dict(study_sha256=digest(self.study), families=[self.entry], error=None, restored=True)
        self.freeze()

    def freeze(self):
        path = self.root / 'calibration/summary.json'
        path.write_text(json.dumps(self.summary))
        baseline = dict(study_sha256=digest(self.study), calibration_is_training_data=False,
                        calibration_summary_sha256=hashlib.sha256(path.read_bytes()).hexdigest(), families=[self.entry])
        self.baseline_path = self.root / 'calibrated-baseline.json'
        self.baseline_path.write_text(json.dumps(baseline))
        self.frozen_hash = hashlib.sha256(self.baseline_path.read_bytes()).hexdigest()

    def load(self):
        with patch('ai.protocol_baseline.validate_study'):
            return load_calibration(self.study, self.root, self.frozen_hash)

    def test_verified_costs_are_rounded_up_and_remain_separate(self):
        result = self.load()
        self.assertEqual(result['families'][0]['margin_ms'], 1)
        self.assertFalse(result['calibration_is_training_data'])

    def test_changing_cost_choice_or_frozen_file_is_rejected(self):
        original = self.baseline_path.read_bytes()
        self.baseline_path.write_bytes(original + b'\n')
        with self.assertRaisesRegex(ConfigurationError, 'baseline hash changed'):
            self.load()
        self.baseline_path.write_bytes(original)
        self.entry['margin_ms'] = 0
        self.freeze()
        with self.assertRaisesRegex(ConfigurationError, 'independent measurements'):
            self.load()

    def test_missing_costs_are_not_replaced_with_zero(self):
        path = self.folder / 'workcost.json'
        path.unlink()
        with self.assertRaises((ConfigurationError, OSError)):
            self.load()

    def test_unhealthy_calibration_is_rejected_even_with_matching_hashes(self):
        with patch('ai.protocol_baseline.assess_run', return_value=dict(usable=True, unhealthy_samples=1, health_faults=0)):
            with self.assertRaisesRegex(ConfigurationError, 'Unhealthy calibration'):
                self.load()


if __name__ == '__main__':
    unittest.main()
