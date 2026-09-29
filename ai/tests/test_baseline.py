"""Boundary regressions for the selected first-prototype policy."""

import json
from pathlib import Path
import unittest

from ai.uthome_ai.contracts import (
    ContractValidationError,
    encode_feature_vector,
    validate_exchange,
    validate_recommendation,
    validate_request,
)


ROOT = Path(__file__).resolve().parents[2]
POLICY = json.loads((ROOT / "ai/data/sources/first-prototype-baseline.json").read_text(encoding="utf-8"))


def exchange(device_type, mode="balanced"):
    spec = POLICY["devices"][device_type]
    request = {
        "contract_version": POLICY["ai_contract_version"],
        "request_id": f"{device_type}-{mode}",
        "device_type": device_type,
        "features": {
            **spec["feature_constants"],
            "operating_mode": mode,
            "desired_period_ms": 0,
            "active_device_count": 4,
            "queue_depth": 8,
        },
    }
    response = {
        "contract_version": POLICY["ai_contract_version"],
        "request_id": request["request_id"],
        "model_version": "policy-fixture",
        "status": "recommended",
        "profile": spec["profile"],
        "parameters": {
            **spec["defaults"],
            "period_ms": spec["mode_period_ms"][mode],
            "period_applicable": device_type != "hlk_ld2450",
            "buffer_applicable": device_type == "hlk_ld2450",
        },
        "confidence_per_mille": 900,
        "abstain_reason": None,
        "warnings": [],
    }
    return request, response


class BaselineTests(unittest.TestCase):
    def test_all_device_mode_examples_are_valid_and_encodable(self):
        for device in POLICY["devices"]:
            for mode in ("power_saving", "balanced", "responsive"):
                with self.subTest(device=device, mode=mode):
                    request, response = exchange(device, mode)
                    validate_exchange(request, response)
                    self.assertEqual(len(encode_feature_vector(request)), 16)

    def test_legacy_contract_is_rejected(self):
        request, response = exchange("bmp388")
        request["contract_version"] = "0.1.0"
        response["contract_version"] = "0.1.0"
        with self.assertRaises(ContractValidationError):
            validate_request(request)
        with self.assertRaises(ContractValidationError):
            validate_recommendation(response)

    def test_caller_cannot_widen_sensor_ranges(self):
        cases = [
            ("hc_sr04", "min_period_ms", 1),
            ("hc_sr04", "min_timeout_us", 100),
            ("bmp388", "max_timeout_us", 1000000),
            ("ssd1306_spi", "payload_size_bytes", 512),
            ("hlk_ld2450", "native_rate_millihz", 20000),
            ("hlk_ld2450", "max_buffer_size_bytes", 1024),
        ]
        for device, field, value in cases:
            with self.subTest(device=device, field=field):
                request, _ = exchange(device)
                request["features"][field] = value
                with self.assertRaisesRegex(ContractValidationError, "baseline 1.0.0"):
                    validate_request(request)

    def test_runtime_queue_limit_is_enforced(self):
        request, _ = exchange("bmp388")
        request["features"]["queue_depth"] = 9
        with self.assertRaises(ContractValidationError):
            validate_request(request)

    def test_explicit_period_overrides_operating_mode_without_rounding(self):
        for device in ("bmp388", "ssd1306_spi", "hc_sr04"):
            with self.subTest(device=device):
                request, response = exchange(device, "power_saving")
                desired = request["features"]["min_period_ms"] + 7
                request["features"]["desired_period_ms"] = desired
                with self.assertRaisesRegex(ContractValidationError, "preserve desired_period_ms"):
                    validate_exchange(request, response)
                response["parameters"]["period_ms"] = desired
                validate_exchange(request, response)

    def test_wrong_device_priority_is_rejected_even_if_globally_valid(self):
        request, response = exchange("hlk_ld2450")
        response["parameters"]["task_priority"] = 16
        validate_recommendation(response)
        with self.assertRaisesRegex(ContractValidationError, "task_priority"):
            validate_exchange(request, response)

    def test_kernel_reserved_priorities_are_not_model_outputs(self):
        for priority in (0, 1, 24, 32, 255):
            with self.subTest(priority=priority):
                _, response = exchange("bmp388")
                response["parameters"]["task_priority"] = priority
                with self.assertRaises(ContractValidationError):
                    validate_recommendation(response)

    def test_millisecond_backed_timeouts_cannot_be_rounded_silently(self):
        for device in ("bmp388", "ssd1306_spi", "hlk_ld2450"):
            with self.subTest(device=device):
                request, response = exchange(device)
                response["parameters"]["timeout_us"] += 1
                with self.assertRaisesRegex(ContractValidationError, "exact number of milliseconds"):
                    validate_exchange(request, response)

    def test_sonar_keeps_microsecond_timeout_resolution(self):
        request, response = exchange("hc_sr04")
        response["parameters"]["timeout_us"] = 29999
        validate_exchange(request, response)

    def test_low_confidence_must_abstain(self):
        _, response = exchange("bmp388")
        response["confidence_per_mille"] = 799
        with self.assertRaises(ContractValidationError):
            validate_recommendation(response)
        response.update(status="abstained", profile=None, parameters=None,
                        confidence_per_mille=0, abstain_reason="below policy threshold")
        validate_recommendation(response)

    def test_balanced_template_matches_selected_defaults(self):
        profile = json.loads((ROOT / "configs/nodes/first-prototype.json").read_text(encoding="utf-8"))
        for device in profile["devices"]:
            baseline = POLICY["devices"][device["driver"]]
            with self.subTest(driver=device["driver"]):
                self.assertEqual(device["device_id"], baseline["device_id"])
                self.assertEqual(device["task"]["priority"], baseline["defaults"]["task_priority"])
                behaviour = device["behaviour"]
                timeout = behaviour.get("timeout_us", behaviour.get("timeout_ms", 0) * 1000)
                self.assertEqual(timeout, baseline["defaults"]["timeout_us"])
                period = behaviour.get("interval_ms", behaviour.get("refresh_interval_ms", 0))
                self.assertEqual(period, baseline["defaults"]["period_ms"])
        uart = next(item for item in profile["interfaces"] if item["protocol"] == "uart")
        self.assertEqual(uart["rx_buffer_bytes"], 512)


if __name__ == "__main__":
    unittest.main()
