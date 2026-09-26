import json
import unittest
from pathlib import Path

from tools.configurator.generator import GenerationError, generate_profile
from tools.configurator.validator import validate_profile


ROOT = Path(__file__).resolve().parents[2]


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.recommendation = {
            "schema_version": "1.0.0",
            "template": "first_prototype",
            "node_id": "generated-node-01",
            "parameters": {
                "environment_interval_ms": 5000,
                "distance_interval_ms": 1500,
                "distance_sensor_enabled": False,
            },
            "metadata": {"source": "member4", "confidence": 0.91},
        }

    def test_generation_is_deterministic_and_valid(self):
        first = generate_profile(self.recommendation)
        second = generate_profile(json.loads(json.dumps(self.recommendation)))
        self.assertEqual(first, second)
        self.assertTrue(validate_profile(first).valid)
        self.assertEqual("generated-node-01", first["node_id"])
        self.assertNotIn("metadata", first)

    def test_parameters_are_applied(self):
        profile = generate_profile(self.recommendation)
        devices = {item["device_id"]: item for item in profile["devices"]}
        self.assertEqual(5000, devices["environment_sensor"]["behaviour"]["interval_ms"])
        self.assertEqual(1500, devices["distance_sensor"]["behaviour"]["interval_ms"])
        self.assertFalse(devices["distance_sensor"]["enabled"])

    def test_unknown_template_is_rejected(self):
        self.recommendation["template"] = "free_form"
        with self.assertRaises(GenerationError):
            generate_profile(self.recommendation)

    def test_recommendation_must_be_an_object(self):
        for value in (None, [], "template", 3):
            with self.subTest(value=value), self.assertRaises(GenerationError):
                generate_profile(value)

    def test_unknown_or_out_of_range_parameter_is_rejected(self):
        self.recommendation["parameters"]["raw_json"] = {}
        with self.assertRaises(GenerationError):
            generate_profile(self.recommendation)
        del self.recommendation["parameters"]["raw_json"]
        self.recommendation["parameters"]["environment_interval_ms"] = 10
        with self.assertRaises(GenerationError):
            generate_profile(self.recommendation)


if __name__ == "__main__":
    unittest.main()
