"""Validate AI transport contracts and encode model input vectors."""

from __future__ import annotations

import json
from functools import lru_cache
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator


AI_ROOT = Path(__file__).resolve().parents[1]
CONTRACT_DIR = AI_ROOT / "contracts"

_DEVICE_FEATURES = {
    "bmp388": {
        "protocol": "i2c",
        "peripheral_category": "environment_sensor",
        "io_direction": "input",
        "data_pattern": "periodic",
    },
    "ssd1306_spi": {
        "protocol": "spi",
        "peripheral_category": "status_display",
        "io_direction": "output",
        "data_pattern": "queue_consumer",
    },
    "hlk_ld2450": {
        "protocol": "uart",
        "peripheral_category": "presence_sensor",
        "io_direction": "input",
        "data_pattern": "stream",
    },
    "hc_sr04": {
        "protocol": "gpio",
        "peripheral_category": "distance_sensor",
        "io_direction": "input",
        "data_pattern": "periodic",
    },
}

_PROFILE_BY_PATTERN = {
    "periodic": "periodic_sensor",
    "stream": "stream_sensor",
    "queue_consumer": "queue_display",
}

_APPLICABILITY_BY_PROFILE = {
    "periodic_sensor": (True, False),
    "stream_sensor": (False, True),
    "queue_display": (True, False),
}


class ContractValidationError(ValueError):
    """Raised when a document fails structural or semantic validation."""

    def __init__(self, errors: list[str]) -> None:
        self.errors = tuple(errors)
        super().__init__("\n".join(errors))


@lru_cache(maxsize=None)
def _load_contract(filename: str) -> dict[str, Any]:
    with (CONTRACT_DIR / filename).open(encoding="utf-8") as contract_file:
        return json.load(contract_file)


@lru_cache(maxsize=1)
def _load_baseline() -> dict[str, Any]:
    path = AI_ROOT / "data" / "sources" / "first-prototype-baseline.json"
    with path.open(encoding="utf-8") as source:
        return json.load(source)


def load_json_document(path: str | Path) -> dict[str, Any]:
    """Load a JSON object from disk and reject non-object roots."""

    with Path(path).open(encoding="utf-8") as document_file:
        document = json.load(document_file)
    if not isinstance(document, dict):
        raise ContractValidationError(["$: document root must be an object"])
    return document


def _format_path(path: Any) -> str:
    formatted = "$"
    for part in path:
        if isinstance(part, int):
            formatted += f"[{part}]"
        else:
            formatted += f".{part}"
    return formatted


def _schema_errors(document: dict[str, Any], schema_name: str) -> list[str]:
    validator = Draft202012Validator(_load_contract(schema_name))
    errors = sorted(
        validator.iter_errors(document),
        key=lambda error: tuple(str(part) for part in error.path),
    )
    return [f"{_format_path(error.path)}: {error.message}" for error in errors]


def _raise_errors(errors: list[str]) -> None:
    if errors:
        raise ContractValidationError(errors)


def validate_request(document: dict[str, Any]) -> dict[str, Any]:
    """Validate request structure, cross-field rules, and device consistency."""

    errors = _schema_errors(document, "feature-request.schema.json")
    if errors:
        raise ContractValidationError(errors)

    features = document["features"]
    pattern = features["data_pattern"]
    period_applies = pattern in {"periodic", "queue_consumer"}

    if features["min_timeout_us"] > features["max_timeout_us"]:
        errors.append("$.features: min_timeout_us must not exceed max_timeout_us")

    if features["min_buffer_size_bytes"] > features["max_buffer_size_bytes"]:
        errors.append(
            "$.features: min_buffer_size_bytes must not exceed max_buffer_size_bytes"
        )

    if period_applies:
        if features["min_period_ms"] < 1 or features["max_period_ms"] < 1:
            errors.append(
                "$.features: period bounds must be positive for periodic or queue-consumer data"
            )
        elif features["min_period_ms"] > features["max_period_ms"]:
            errors.append("$.features: min_period_ms must not exceed max_period_ms")

        desired_period = features["desired_period_ms"]
        if desired_period and not (
            features["min_period_ms"]
            <= desired_period
            <= features["max_period_ms"]
        ):
            errors.append(
                "$.features.desired_period_ms: must be zero or inside the period bounds"
            )
    elif any(
        features[field]
        for field in ("min_period_ms", "max_period_ms", "desired_period_ms")
    ):
        errors.append(
            "$.features: stream inputs must use zero for period bounds and desired_period_ms"
        )

    if pattern == "stream":
        if features["min_buffer_size_bytes"] < 1:
            errors.append("$.features: stream inputs require a positive buffer-size range")
    elif features["min_buffer_size_bytes"] or features["max_buffer_size_bytes"]:
        errors.append(
            "$.features: non-stream inputs must use zero for both buffer-size bounds"
        )

    expected_features = _DEVICE_FEATURES[document["device_type"]]
    for field, expected in expected_features.items():
        actual = features[field]
        if actual != expected:
            errors.append(
                f"$.features.{field}: {document['device_type']} requires {expected!r}, got {actual!r}"
            )

    baseline = _load_baseline()["devices"][document["device_type"]]
    for field, expected in baseline["feature_constants"].items():
        if features[field] != expected:
            errors.append(
                f"$.features.{field}: baseline 1.0.0 requires {expected!r}"
            )

    _raise_errors(errors)
    return document


def validate_recommendation(document: dict[str, Any]) -> dict[str, Any]:
    """Validate response structure and profile-specific applicability rules."""

    errors = _schema_errors(document, "recommendation.schema.json")
    if errors:
        raise ContractValidationError(errors)

    if document["status"] == "recommended":
        profile = document["profile"]
        parameters = document["parameters"]
        expected_period, expected_buffer = _APPLICABILITY_BY_PROFILE[profile]

        if parameters["period_applicable"] != expected_period:
            errors.append(
                f"$.parameters.period_applicable: must be {expected_period} for {profile}"
            )
        if parameters["buffer_applicable"] != expected_buffer:
            errors.append(
                f"$.parameters.buffer_applicable: must be {expected_buffer} for {profile}"
            )
        if parameters["period_applicable"] != (parameters["period_ms"] > 0):
            errors.append(
                "$.parameters.period_ms: must be positive exactly when period_applicable is true"
            )
        if parameters["buffer_applicable"] != (
            parameters["buffer_size_bytes"] > 0
        ):
            errors.append(
                "$.parameters.buffer_size_bytes: must be positive exactly when buffer_applicable is true"
            )

    _raise_errors(errors)
    return document


def _resolve_source(document: dict[str, Any], source: str) -> Any:
    value: Any = document
    for part in source.split("."):
        value = value[part]
    return value


def encode_feature_vector(document: dict[str, Any]) -> list[int]:
    """Validate a request and encode its ordered, unscaled uint32 vector."""

    validate_request(document)
    io_contract = _load_contract("model-io-contract.json")
    encodings = io_contract["categorical_encodings"]
    vector: list[int] = []

    for field in sorted(io_contract["feature_vector"]["fields"], key=lambda item: item["index"]):
        value = _resolve_source(document, field["source"])
        encoding = field["encoding"]
        if encoding == "identity":
            encoded = int(value)
        else:
            encoding_name = encoding.rsplit(".", 1)[-1]
            encoded = int(encodings[encoding_name][value])
        if not 0 <= encoded <= 0xFFFFFFFF:
            raise ContractValidationError(
                [f"$.{field['source']}: encoded value is outside uint32 range"]
            )
        vector.append(encoded)

    return vector


def validate_exchange(
    request: dict[str, Any], recommendation: dict[str, Any]
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Validate request/response correlation and recommended parameter bounds."""

    validate_request(request)
    validate_recommendation(recommendation)
    errors: list[str] = []

    if recommendation["request_id"] != request["request_id"]:
        errors.append("$.request_id: recommendation does not match the request")

    if recommendation["status"] == "recommended":
        features = request["features"]
        profile = recommendation["profile"]
        parameters = recommendation["parameters"]
        expected_profile = _PROFILE_BY_PATTERN[features["data_pattern"]]
        baseline = _load_baseline()["devices"][request["device_type"]]
        expected_priority = baseline["defaults"]["task_priority"]
        if parameters["task_priority"] != expected_priority:
            errors.append(
                f"$.parameters.task_priority: {request['device_type']} requires {expected_priority}"
            )
        # These drivers accept millisecond timeouts. Requiring exact conversion
        # avoids silently extending a requested deadline by rounding upward.
        if (
            request["device_type"] in {"bmp388", "ssd1306_spi", "hlk_ld2450"}
            and parameters["timeout_us"] % 1000
        ):
            errors.append("$.parameters.timeout_us: must be an exact number of milliseconds")

        if profile != expected_profile:
            errors.append(
                f"$.profile: {features['data_pattern']} requires {expected_profile!r}, got {profile!r}"
            )

        if not (
            features["min_timeout_us"]
            <= parameters["timeout_us"]
            <= features["max_timeout_us"]
        ):
            errors.append("$.parameters.timeout_us: outside request bounds")

        if parameters["period_applicable"] and not (
            features["min_period_ms"]
            <= parameters["period_ms"]
            <= features["max_period_ms"]
        ):
            errors.append("$.parameters.period_ms: outside request bounds")

        if (
            features["desired_period_ms"]
            and parameters["period_ms"] != features["desired_period_ms"]
        ):
            errors.append("$.parameters.period_ms: must preserve desired_period_ms")

        if parameters["buffer_applicable"] and not (
            features["min_buffer_size_bytes"]
            <= parameters["buffer_size_bytes"]
            <= features["max_buffer_size_bytes"]
        ):
            errors.append("$.parameters.buffer_size_bytes: outside request bounds")

    _raise_errors(errors)
    return request, recommendation
