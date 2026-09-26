import json
import unittest
from pathlib import Path

from tools.configurator.monitoring import MonitoringError, validate_snapshot


ROOT = Path(__file__).resolve().parents[2]


class MonitoringTests(unittest.TestCase):
    def setUp(self):
        self.snapshot = json.loads((ROOT / "configs/health/first-prototype.json").read_text())

    def test_first_prototype_snapshot_is_accepted(self):
        self.assertEqual("healthy", validate_snapshot(self.snapshot)["overall"])

    def test_disconnected_bmp388_fixture_is_degraded(self):
        fixture = json.loads((ROOT / "configs/health/bmp388-disconnected.json").read_text())
        snapshot = validate_snapshot(fixture)
        self.assertEqual("degraded", snapshot["overall"])
        self.assertEqual("I2C timeout reading BMP388", snapshot["devices"][0]["error"])

    def test_malformed_state_is_rejected(self):
        self.snapshot["tasks"][0]["state"] = "mystery"
        with self.assertRaises(MonitoringError):
            validate_snapshot(self.snapshot)

    def test_unbounded_collection_is_rejected(self):
        self.snapshot["tasks"] = [{"name": str(i), "state": "running"} for i in range(33)]
        with self.assertRaises(MonitoringError):
            validate_snapshot(self.snapshot)

    def test_prototype_readings_are_accepted(self):
        metrics = {reading["metric"] for reading in validate_snapshot(self.snapshot)["readings"]}
        self.assertEqual({"temperature", "pressure", "presence", "distance"}, metrics)

    def test_unsupported_metric_is_rejected(self):
        self.snapshot["readings"][0]["metric"] = "radiation"
        with self.assertRaises(MonitoringError):
            validate_snapshot(self.snapshot)

    def test_non_finite_reading_is_rejected(self):
        self.snapshot["readings"][0]["value"] = float("inf")
        with self.assertRaises(MonitoringError):
            validate_snapshot(self.snapshot)

    def test_boolean_is_not_a_reading_value(self):
        self.snapshot["readings"][0]["value"] = True
        with self.assertRaises(MonitoringError):
            validate_snapshot(self.snapshot)

    def test_unbounded_readings_are_rejected(self):
        self.snapshot["readings"] = [
            {"device_id": "d", "metric": "temperature", "value": 1.0, "unit": "C"}
        ] * 33
        with self.assertRaises(MonitoringError):
            validate_snapshot(self.snapshot)

    def test_queue_usage_must_fit_depth(self):
        self.snapshot["queues"][0]["used"] = 9
        with self.assertRaises(MonitoringError):
            validate_snapshot(self.snapshot)

    def test_counter_must_be_nonnegative_integer(self):
        self.snapshot["counters"][0]["value"] = -1
        with self.assertRaises(MonitoringError):
            validate_snapshot(self.snapshot)

    def test_device_error_must_be_bounded_text(self):
        self.snapshot["devices"][0]["error"] = "x" * 513
        with self.assertRaises(MonitoringError):
            validate_snapshot(self.snapshot)

    def test_boolean_uptime_is_rejected(self):
        self.snapshot["uptime_ms"] = True
        with self.assertRaises(MonitoringError):
            validate_snapshot(self.snapshot)


if __name__ == "__main__":
    unittest.main()
