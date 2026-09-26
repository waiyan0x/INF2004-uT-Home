"""Bounded validation and normalization for device health snapshots."""

from __future__ import annotations

from math import isfinite
from typing import Any


class MonitoringError(ValueError):
    """A health snapshot is malformed or exceeds safety bounds."""


ALLOWED_OVERALL = {"healthy", "degraded", "fault", "offline"}
ALLOWED_TASK = {"running", "stopped", "blocked", "fault"}

# Measurement channels a node may report. "humidity" is accepted by the contract
# but no currently supported driver produces it: the BMP388 reports pressure and
# temperature only. It becomes reachable if a humidity-capable sensor such as the
# BME280 is added to SUPPORTED_DRIVERS in validator.py.
ALLOWED_METRICS = {"temperature", "pressure", "humidity", "distance", "presence"}
MAX_READINGS = 32
MAX_UNIT_LENGTH = 8


def _bounded_text(value: object, maximum: int) -> bool:
    return isinstance(value, str) and 0 < len(value) <= maximum


def _bounded_integer(value: object, minimum: int, maximum: int) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and minimum <= value <= maximum


def validate_snapshot(snapshot: object) -> dict[str, Any]:
    """Return a normalized bounded snapshot or raise MonitoringError."""
    if not isinstance(snapshot, dict):
        raise MonitoringError("Snapshot must be an object.")
    required = {"schema_version", "node_id", "overall", "uptime_ms", "configuration_state", "tasks", "queues", "devices", "counters", "readings"}
    missing = required - set(snapshot)
    if missing:
        raise MonitoringError(f"Missing snapshot fields: {sorted(missing)}.")
    if snapshot["schema_version"] != "1.0.0" or snapshot["overall"] not in ALLOWED_OVERALL:
        raise MonitoringError("Unsupported snapshot version or overall state.")
    if not _bounded_text(snapshot["node_id"], 64):
        raise MonitoringError("Invalid node_id.")
    if not _bounded_text(snapshot["configuration_state"], 32):
        raise MonitoringError("Invalid configuration_state.")
    if not _bounded_integer(snapshot["uptime_ms"], 0, 2**63 - 1):
        raise MonitoringError("Invalid uptime_ms.")
    limits = {"tasks": 32, "queues": 64, "devices": 32, "counters": 128, "readings": MAX_READINGS}
    for field, maximum in limits.items():
        if not isinstance(snapshot[field], list) or len(snapshot[field]) > maximum:
            raise MonitoringError(f"{field} must be a list with at most {maximum} entries.")
    for index, task in enumerate(snapshot["tasks"]):
        if not isinstance(task, dict) or task.get("state") not in ALLOWED_TASK or not _bounded_text(task.get("name"), 64):
            raise MonitoringError(f"Invalid task at index {index}.")
    for index, queue in enumerate(snapshot["queues"]):
        if (
            not isinstance(queue, dict)
            or not _bounded_text(queue.get("id"), 64)
            or not _bounded_integer(queue.get("depth"), 1, 256)
            or not _bounded_integer(queue.get("used"), 0, queue.get("depth") if isinstance(queue.get("depth"), int) else 0)
        ):
            raise MonitoringError(f"Invalid queue at index {index}.")
    for index, device in enumerate(snapshot["devices"]):
        if (
            not isinstance(device, dict)
            or not _bounded_text(device.get("device_id"), 64)
            or not _bounded_text(device.get("state"), 32)
            or (device.get("error") is not None and not _bounded_text(device.get("error"), 512))
        ):
            raise MonitoringError(f"Invalid device at index {index}.")
    for index, counter in enumerate(snapshot["counters"]):
        if (
            not isinstance(counter, dict)
            or not _bounded_text(counter.get("name"), 64)
            or not _bounded_integer(counter.get("value"), 0, 2**63 - 1)
        ):
            raise MonitoringError(f"Invalid counter at index {index}.")
    for index, reading in enumerate(snapshot["readings"]):
        _validate_reading(reading, index)
    return dict(snapshot)


def _validate_reading(reading: object, index: int) -> None:
    """Reject a measurement that is malformed, unsupported, or not finite."""
    if not isinstance(reading, dict):
        raise MonitoringError(f"Reading at index {index} must be an object.")
    if reading.get("metric") not in ALLOWED_METRICS:
        raise MonitoringError(f"Unsupported metric at index {index}: {reading.get('metric')!r}.")
    if not isinstance(reading.get("device_id"), str) or len(reading["device_id"]) > 64:
        raise MonitoringError(f"Invalid device_id in reading at index {index}.")
    value = reading.get("value")
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not isfinite(value):
        raise MonitoringError(f"Reading at index {index} must carry a finite numeric value.")
    if not isinstance(reading.get("unit"), str) or len(reading["unit"]) > MAX_UNIT_LENGTH:
        raise MonitoringError(f"Invalid unit in reading at index {index}.")
