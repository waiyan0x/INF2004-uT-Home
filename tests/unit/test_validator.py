import copy
import json
import unittest
from pathlib import Path

from tools.configurator.validator import validate_profile


ROOT = Path(__file__).resolve().parents[2]
VALID_PROFILE_PATH = ROOT / "configs" / "nodes" / "first-prototype.json"


def load_profile(path: Path = VALID_PROFILE_PATH) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


class ProfileValidatorTests(unittest.TestCase):
    def test_valid_profile_has_no_errors(self) -> None:
        result = validate_profile(load_profile())
        self.assertTrue(result.valid)
        self.assertEqual((), result.errors)

    def test_pin_conflict_fixture_has_stable_code(self) -> None:
        profile = load_profile(ROOT / "configs" / "invalid" / "pin-conflict.json")
        codes = [error.code for error in validate_profile(profile).errors]
        self.assertIn("PIN_CONFLICT", codes)

    def test_unsupported_protocol_fixture_has_stable_code(self) -> None:
        profile = load_profile(ROOT / "configs" / "invalid" / "unsupported-protocol.json")
        codes = [error.code for error in validate_profile(profile).errors]
        self.assertIn("UNSUPPORTED_PROTOCOL", codes)

    def test_duplicate_device_id_is_rejected(self) -> None:
        profile = load_profile()
        duplicate = copy.deepcopy(profile["devices"][0])
        profile["devices"].append(duplicate)
        codes = [error.code for error in validate_profile(profile).errors]
        self.assertIn("DUPLICATE_DEVICE_ID", codes)

    def test_missing_interface_reference_is_rejected(self) -> None:
        profile = load_profile()
        profile["devices"][0]["interface"]["ref"] = "missing_bus"
        errors = validate_profile(profile).errors
        self.assertTrue(any(error.code == "INTERFACE_NOT_FOUND" for error in errors))

    def test_missing_queue_reference_is_rejected(self) -> None:
        profile = load_profile()
        profile["devices"][0]["behaviour"]["destination"] = "missing.queue"
        errors = validate_profile(profile).errors
        self.assertTrue(any(error.code == "QUEUE_NOT_FOUND" for error in errors))

    def test_hc_sr04_requires_voltage_divider_declaration(self) -> None:
        profile = load_profile()
        profile["devices"][3]["interface"]["echo_voltage_divider_required"] = False
        errors = validate_profile(profile).errors
        self.assertTrue(any(error.code == "VOLTAGE_DIVIDER_REQUIRED" for error in errors))

    def test_enabled_devices_cannot_exceed_runtime_limit(self) -> None:
        profile = load_profile()
        profile["runtime"]["max_device_tasks"] = 3
        errors = validate_profile(profile).errors
        self.assertTrue(any(error.code == "TASK_LIMIT_EXCEEDED" for error in errors))

    def test_errors_are_sorted_by_path_then_code(self) -> None:
        profile = load_profile()
        profile["devices"][0]["interface"]["ref"] = "missing_bus"
        profile["devices"][0]["behaviour"]["destination"] = "missing.queue"
        errors = validate_profile(profile).errors
        ordering = [(error.path, error.code) for error in errors]
        self.assertEqual(sorted(ordering), ordering)

    def test_result_serialization_has_consistent_shape(self) -> None:
        profile = load_profile(ROOT / "configs" / "invalid" / "pin-conflict.json")
        payload = validate_profile(profile).to_dict()
        self.assertEqual(False, payload["valid"])
        self.assertEqual({"code", "path", "message"}, set(payload["errors"][0]))


if __name__ == "__main__":
    unittest.main()
