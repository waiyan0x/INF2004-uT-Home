import copy
import json
import re
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

    def test_malformed_json_shapes_return_errors_without_crashing(self) -> None:
        malformed = [
            {**load_profile(), "interfaces": None},
            {**load_profile(), "devices": None},
            {**load_profile(), "queues": [{"id": []}]},
            {**load_profile(), "devices": [{"device_id": [], "driver": []}]},
            {**load_profile(), "interfaces": [{"id": "bad", "protocol": []}]},
        ]
        for profile in malformed:
            with self.subTest(profile=profile):
                self.assertFalse(validate_profile(profile).valid)

    def test_unsupported_driver_has_correct_code(self) -> None:
        profile = load_profile()
        profile["devices"][0]["driver"] = "unknown_sensor"
        codes = {error.code for error in validate_profile(profile).errors}
        self.assertIn("UNSUPPORTED_DRIVER", codes)
        self.assertNotIn("UNSUPPORTED_PROTOCOL", codes)

    def test_runtime_device_capacity_is_enforced(self) -> None:
        profile = load_profile()
        for index in range(5):
            device = copy.deepcopy(profile["devices"][0])
            device["device_id"] = f"extra_{index}"
            device["task"]["name"] = f"extra_task_{index}"
            device["enabled"] = False
            profile["devices"].append(device)
        codes = {error.code for error in validate_profile(profile).errors}
        self.assertIn("RUNTIME_DEVICE_CAPACITY_EXCEEDED", codes)

    def test_runtime_task_capacity_is_enforced(self) -> None:
        profile = load_profile()
        profile["runtime"]["max_device_tasks"] = 5
        codes = {error.code for error in validate_profile(profile).errors}
        self.assertIn("RUNTIME_TASK_CAPACITY_EXCEEDED", codes)

    def test_device_id_must_fit_runtime_buffer(self) -> None:
        profile = load_profile()
        profile["devices"][0]["device_id"] = "d" * 32
        codes = {error.code for error in validate_profile(profile).errors}
        self.assertIn("RUNTIME_DEVICE_ID_TOO_LONG", codes)

    def test_host_limits_match_runtime_header(self) -> None:
        from tools.configurator.validator import MAX_RUNTIME_DEVICES, MAX_RUNTIME_DEVICE_TASKS, RUNTIME_DEVICE_ID_MAX

        header = (ROOT.parents[1] / "firmware/include/uthome/runtime.h").read_text(encoding="utf-8")
        for macro, expected in (
            ("UTHOME_RUNTIME_MAX_DEVICES", MAX_RUNTIME_DEVICES),
            ("UTHOME_RUNTIME_MAX_DEVICE_TASKS", MAX_RUNTIME_DEVICE_TASKS),
            ("UTHOME_DEVICE_ID_MAX", RUNTIME_DEVICE_ID_MAX),
        ):
            match = re.search(rf"^#define\s+{macro}\s+(\d+)U?\s*$", header, re.MULTILINE)
            self.assertIsNotNone(match, macro)
            self.assertEqual(expected, int(match.group(1)))


if __name__ == "__main__":
    unittest.main()
