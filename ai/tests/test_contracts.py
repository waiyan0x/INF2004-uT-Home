"""Contract tests for the host-side AI harness."""

from __future__ import annotations

import unittest

from ai.uthome_ai.contracts import (
    ContractValidationError,
    encode_feature_vector,
    validate_exchange,
    validate_recommendation,
    validate_request,
)


def bmp388_request() -> dict:
    return {
        "contract_version": "0.2.0",
        "request_id": "bmp-balanced-001",
        "device_type": "bmp388",
        "features": {
            "protocol": "i2c",
            "peripheral_category": "environment_sensor",
            "io_direction": "input",
            "data_pattern": "periodic",
            "payload_size_bytes": 6,
            "min_period_ms": 200,
            "max_period_ms": 60000,
            "native_rate_millihz": 25000,
            "min_timeout_us": 10000,
            "max_timeout_us": 100000,
            "min_buffer_size_bytes": 0,
            "max_buffer_size_bytes": 0,
            "operating_mode": "balanced",
            "desired_period_ms": 2000,
            "active_device_count": 4,
            "queue_depth": 8,
        },
    }


def bmp388_recommendation() -> dict:
    return {
        "contract_version": "0.2.0",
        "request_id": "bmp-balanced-001",
        "model_version": "contract-test",
        "status": "recommended",
        "profile": "periodic_sensor",
        "parameters": {
            "period_ms": 2000,
            "timeout_us": 20000,
            "buffer_size_bytes": 0,
            "task_priority": 12,
            "period_applicable": True,
            "buffer_applicable": False,
        },
        "confidence_per_mille": 900,
        "abstain_reason": None,
        "warnings": [],
    }


class RequestContractTests(unittest.TestCase):
    def test_valid_request_encodes_in_contract_order(self) -> None:
        request = bmp388_request()
        validate_request(request)

        vector = encode_feature_vector(request)

        self.assertEqual(len(vector), 16)
        self.assertEqual(vector[:5], [0, 0, 0, 0, 6])
        self.assertEqual(vector[12:], [1, 2000, 4, 8])

    def test_unknown_property_is_rejected(self) -> None:
        request = bmp388_request()
        request["features"]["invented_field"] = 1

        with self.assertRaises(ContractValidationError):
            validate_request(request)

    def test_period_bounds_are_ordered(self) -> None:
        request = bmp388_request()
        request["features"]["min_period_ms"] = 5000
        request["features"]["max_period_ms"] = 1000

        with self.assertRaisesRegex(ContractValidationError, "min_period_ms"):
            validate_request(request)

    def test_device_features_must_match_profile(self) -> None:
        request = bmp388_request()
        request["features"]["protocol"] = "spi"

        with self.assertRaisesRegex(ContractValidationError, "bmp388 requires"):
            validate_request(request)

    def test_stream_requires_zero_periods_and_positive_buffer(self) -> None:
        request = bmp388_request()
        request["device_type"] = "hlk_ld2450"
        request["features"].update(
            {
                "protocol": "uart",
                "peripheral_category": "presence_sensor",
                "data_pattern": "stream",
                "payload_size_bytes": 30,
                "min_period_ms": 0,
                "max_period_ms": 0,
                "native_rate_millihz": 10000,
                "min_buffer_size_bytes": 512,
                "max_buffer_size_bytes": 512,
                "min_timeout_us": 200000,
                "max_timeout_us": 500000,
                "desired_period_ms": 0,
            }
        )

        validate_request(request)
        vector = encode_feature_vector(request)
        self.assertEqual(vector[:5], [2, 2, 0, 1, 30])


class RecommendationContractTests(unittest.TestCase):
    def test_valid_recommendation(self) -> None:
        validate_recommendation(bmp388_recommendation())

    def test_valid_abstention(self) -> None:
        recommendation = bmp388_recommendation()
        recommendation.update(
            {
                "status": "abstained",
                "profile": None,
                "parameters": None,
                "confidence_per_mille": 0,
                "abstain_reason": "unsupported feature combination",
            }
        )
        validate_recommendation(recommendation)

    def test_inapplicable_parameter_must_be_zero(self) -> None:
        recommendation = bmp388_recommendation()
        recommendation["parameters"]["buffer_size_bytes"] = 64

        with self.assertRaisesRegex(ContractValidationError, "buffer_size_bytes"):
            validate_recommendation(recommendation)


class ExchangeContractTests(unittest.TestCase):
    def test_valid_exchange(self) -> None:
        validate_exchange(bmp388_request(), bmp388_recommendation())

    def test_request_ids_must_match(self) -> None:
        recommendation = bmp388_recommendation()
        recommendation["request_id"] = "another-request"

        with self.assertRaisesRegex(ContractValidationError, "does not match"):
            validate_exchange(bmp388_request(), recommendation)

    def test_recommended_period_must_be_inside_request_bounds(self) -> None:
        recommendation = bmp388_recommendation()
        recommendation["parameters"]["period_ms"] = 60001

        with self.assertRaisesRegex(ContractValidationError, "period_ms"):
            validate_exchange(bmp388_request(), recommendation)

    def test_profile_must_match_data_pattern(self) -> None:
        recommendation = bmp388_recommendation()
        recommendation["profile"] = "queue_display"

        with self.assertRaisesRegex(ContractValidationError, "requires"):
            validate_exchange(bmp388_request(), recommendation)


if __name__ == "__main__":
    unittest.main()
