"""Generate safe profiles from bounded recommendation templates."""

from __future__ import annotations

import copy
import json
from pathlib import Path
from typing import Any

from .validator import ROOT, validate_profile


TEMPLATE_PATH = ROOT / "configs" / "nodes" / "first-prototype.json"


class GenerationError(ValueError):
    """The recommendation cannot be mapped to an approved template."""


def _bounded_integer(value: object, name: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise GenerationError(f"{name} must be an integer from {minimum} to {maximum}.")
    return value


def generate_profile(recommendation: dict[str, Any], template_path: Path = TEMPLATE_PATH) -> dict[str, Any]:
    """Create a deterministic profile using only the registered prototype template."""
    allowed_root = {"schema_version", "template", "node_id", "parameters", "metadata"}
    unknown_root = set(recommendation) - allowed_root
    if unknown_root:
        raise GenerationError(f"Unknown recommendation fields: {sorted(unknown_root)}.")
    if recommendation.get("schema_version") != "1.0.0":
        raise GenerationError("schema_version must be '1.0.0'.")
    if recommendation.get("template") != "first_prototype":
        raise GenerationError("Only the 'first_prototype' template is supported.")
    node_id = recommendation.get("node_id")
    if not isinstance(node_id, str) or not node_id or len(node_id) > 64:
        raise GenerationError("node_id must be a non-empty string of at most 64 characters.")
    parameters = recommendation.get("parameters", {})
    if not isinstance(parameters, dict):
        raise GenerationError("parameters must be an object.")
    allowed_parameters = {
        "environment_interval_ms",
        "distance_interval_ms",
        "distance_sensor_enabled",
        "health_report_interval_ms",
    }
    unknown_parameters = set(parameters) - allowed_parameters
    if unknown_parameters:
        raise GenerationError(f"Unknown template parameters: {sorted(unknown_parameters)}.")

    profile = copy.deepcopy(json.loads(template_path.read_text(encoding="utf-8")))
    profile["node_id"] = node_id
    devices = {item["device_id"]: item for item in profile["devices"]}
    if "environment_interval_ms" in parameters:
        devices["environment_sensor"]["behaviour"]["interval_ms"] = _bounded_integer(
            parameters["environment_interval_ms"], "environment_interval_ms", 100, 3_600_000
        )
    if "distance_interval_ms" in parameters:
        devices["distance_sensor"]["behaviour"]["interval_ms"] = _bounded_integer(
            parameters["distance_interval_ms"], "distance_interval_ms", 100, 3_600_000
        )
    if "health_report_interval_ms" in parameters:
        profile["runtime"]["health_report_interval_ms"] = _bounded_integer(
            parameters["health_report_interval_ms"], "health_report_interval_ms", 100, 3_600_000
        )
    if "distance_sensor_enabled" in parameters:
        enabled = parameters["distance_sensor_enabled"]
        if not isinstance(enabled, bool):
            raise GenerationError("distance_sensor_enabled must be a boolean.")
        devices["distance_sensor"]["enabled"] = enabled
    result = validate_profile(profile)
    if not result.valid:
        raise GenerationError(f"Generated profile is invalid: {result.errors[0].message}")
    return profile
