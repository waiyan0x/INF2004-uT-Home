import json
import tempfile
import unittest
from pathlib import Path

from tools.integration_runner import run_scenario


ROOT = Path(__file__).resolve().parents[2]


class ScenarioRunnerTests(unittest.TestCase):
    def test_first_prototype_report_is_machine_readable(self):
        report = run_scenario(ROOT / "configs/scenarios/first-prototype.json")
        self.assertTrue(report["host_checks_passed"])
        self.assertEqual("passed", report["results"][0]["status"])
        self.assertTrue(any(item["status"] == "manual_pending" for item in report["results"]))

    def test_invalid_profile_identifies_failed_node_step(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            scenario = {
                "scenario_id": "invalid",
                "nodes": [{"node_id": "bad", "profile": "profile.json"}],
                "demonstration_steps": [],
            }
            (root / "scenario.json").write_text(json.dumps(scenario))
            (root / "profile.json").write_text((ROOT / "configs/invalid/pin-conflict.json").read_text())
            report = run_scenario(root / "scenario.json")
            self.assertFalse(report["host_checks_passed"])
            self.assertEqual("profile:bad", report["results"][0]["step"])


if __name__ == "__main__":
    unittest.main()
