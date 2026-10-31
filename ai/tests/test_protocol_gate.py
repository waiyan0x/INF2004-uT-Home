"""Gate tests use clearly synthetic in-memory labels, never bench artifacts."""
from copy import deepcopy
import unittest
from unittest.mock import patch

from ai.protocol_gate import assess, choices
from ai.protocol_model import train
from ai.tests import test_protocol as fixtures
from ai.timing import digest
from tools.config_validator.planner import ConfigurationError


class ProtocolGateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixtures.ProtocolTests.setUpClass()
        cls.fixture = fixtures.ProtocolTests()
        cls.study = cls.fixture.study
        cls.data = cls.fixture.fixture_dataset()
        with patch("ai.protocol_model.dataset", return_value=cls.data):
            cls.model, _, _ = train(cls.study, [])

    def test_equal_rule_does_not_become_benefit_or_deployment(self):
        report = assess(self.study, self.model, self.data)
        self.assertFalse(report["statistical_criteria_passed"])
        self.assertIn("no_measured_benefit_over_simple_rule", report["reasons"])
        self.assertFalse(report["deployment_allowed"])
        self.assertFalse(report["automatic_apply"])
        self.assertEqual(set(report["partitions"]), {"train", "validation", "test"})

    def test_changed_binding_and_duplicate_rows_are_rejected(self):
        data = deepcopy(self.data)
        data["rows"][0]["label"] ^= 1
        with self.assertRaisesRegex(ConfigurationError, "binding"):
            assess(self.study, self.model, data)
        data["rows"].append(deepcopy(data["rows"][0]))
        model = dict(self.model, dataset_sha256=digest(data))
        with self.assertRaisesRegex(ConfigurationError, "duplicate"):
            assess(self.study, model, data)

    def test_forged_features_and_missing_families_are_rejected(self):
        data = deepcopy(self.data)
        data["rows"][0]["features"][0] += 1
        model = dict(self.model, dataset_sha256=digest(data))
        with self.assertRaisesRegex(ConfigurationError, "features"):
            assess(self.study, model, data)
        data["excluded"] = ["test-only"]
        model = dict(self.model, dataset_sha256=digest(data))
        with self.assertRaisesRegex(ConfigurationError, "Incomplete"):
            assess(self.study, model, data)

    def decisions(self, labels, accepted):
        # Deliberately small helper-only vectors: interval index 1, goal index 2.
        rows = [dict(interval_ms=n, features=[0, n, 800, 0, 100], label=label)
                for n, label in zip((200, 500, 1000), labels)]
        with patch("ai.protocol_gate.workload.compatibility", return_value="fixture"), \
                patch("ai.protocol_gate.predict", side_effect=lambda _, vector: int(vector[1] in accepted)):
            return choices({"contexts": ["fixture"]}, {}, rows)

    def test_unsafe_longer_interval_is_never_a_benefit(self):
        value = self.decisions((1, 1, 0), {1000})
        self.assertTrue(value["violation"])
        self.assertFalse(value["safe_interval_gain"])
        self.assertFalse(value["safe_coverage_gain"])
        self.assertTrue(value["regression_against_safe_rule"])
        self.assertIsNone(value["safe_interval_delta_ms"])

    def test_true_interval_gain_coverage_gain_and_abstention_are_distinct(self):
        value = self.decisions((1, 1, 1), {1000})
        self.assertTrue(value["safe_interval_gain"])
        self.assertEqual(value["safe_interval_delta_ms"], 500)
        self.assertFalse(value["safe_coverage_gain"])
        value = self.decisions((1, 0, 0), {200})
        self.assertTrue(value["safe_coverage_gain"])
        self.assertFalse(value["safe_interval_gain"])
        self.assertTrue(self.decisions((1, 0, 0), set())["unnecessary_abstention"])
        self.assertFalse(self.decisions((0, 0, 0), set())["unnecessary_abstention"])


if __name__ == "__main__":
    unittest.main()
