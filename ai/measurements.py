"""Validate bounded workcost snapshots supplied by the runtime observer.

Telemetry version 3 retains its format; command 9 supplies costs. Never infer missing
transfer/step counters from baud, USB round-trip time or sample freshness.
Snapshots are calibration evidence, not automatically added model features.
"""
from typing import Literal, TypedDict, cast

from ai.timing import digest
from tools.config_transfer.payloads import WorkerMeasurement, WorkloadMeasurement, WorkStats
from tools.config_transfer.workcost import FIELDS
from tools.config_validator.planner import ConfigurationError, require


class WorkerWindow(TypedDict):
    steps: int
    elapsed_us: int
    bus_wait_us: int
    bus_active_us: int
    transfer_bytes: int
    transfers: int
    timeouts: int
    errors: int
    slot: int
    source_index: int
    max_elapsed_us_since_reset: int
    max_lateness_us_since_reset: int


class WorkloadWindow(TypedDict):
    contract: Literal["uthome.workload-window"]
    version: Literal["0.1.0"]
    before_sha256: str
    after_sha256: str
    duration_us: int
    workers: list[WorkerWindow]
    cpu_time_measured: Literal[False]
    electrical_wire_time_measured: Literal[False]
    model_features: Literal[False]


def validate_snapshot(value: object) -> WorkloadMeasurement:
    """Check an untrusted decoded JSON value before exposing its typed fields."""
    if not isinstance(value, dict):
        raise ConfigurationError("Unsupported workload measurement")
    require(set(value) == {
        "contract", "version", "firmware_elf_sha256", "profile_sha256", "generation", "timestamp_us", "workers"
    } and value["contract"] == "uthome.workload-measurement" and value["version"] == "0.1.0",
        "Unsupported workload measurement")
    for key in ("firmware_elf_sha256", "profile_sha256"):
        require(isinstance(value[key], str) and len(value[key]) == 64
                and all(c in "0123456789abcdef" for c in value[key]), "Missing measurement identity")
    for key, limit in (("generation", 0xFFFFFFFF), ("timestamp_us", 0xFFFFFFFFFFFFFFFF)):
        require(type(value[key]) is int and 0 <= value[key] <= limit, "Invalid measurement clock/generation")
    rows = value["workers"]
    require(isinstance(rows, list) and len(rows) <= 4, "Measurements exceed four worker slots")
    slots = set()
    for row in rows:
        require(isinstance(row, dict) and set(row) == {"slot", "source_index", "available", "stats"}, "Invalid worker row")
        require(type(row["slot"]) is int and 0 <= row["slot"] < 4 and row["slot"] not in slots, "Duplicate/invalid slot")
        slots.add(row["slot"])
        require(type(row["source_index"]) is int and 0 <= row["source_index"] < 8, "Invalid source index")
        require(type(row["available"]) is bool, "Measurement availability is explicit")
        if not row["available"]:
            require(row["stats"] is None, "Unavailable measurements cannot carry fabricated zeros")
            continue
        stats = row["stats"]
        require(isinstance(stats, dict) and set(stats) == set(FIELDS)
                and all(type(v) is int and 0 <= v <= 0xFFFFFFFF for v in stats.values()), "Invalid counters")
        require(stats["flags"] in (0, 1), "Unknown measurement flags")
        require(stats["timeouts"] <= stats["errors"] and stats["max_elapsed_us"] <= stats["elapsed_us"], "Inconsistent counters")
        if stats["flags"] == 0:
            require(stats["bus_wait_us"] + stats["bus_active_us"] <= stats["elapsed_us"], "Overlapping bus timings")
    require(len({r["source_index"] for r in rows}) == len(rows), "Source bound more than once")
    # Every key, scalar, worker and optional stats object was checked above.
    return cast(WorkloadMeasurement, value)


def available_stats(worker: WorkerMeasurement) -> WorkStats:
    """Refuse unavailable counters rather than substituting fabricated zeros."""
    stats = worker["stats"]
    if not worker["available"] or stats is None:
        raise ConfigurationError("Workload counters unavailable")
    return stats


def summarize_window(before: WorkloadMeasurement, after: WorkloadMeasurement) -> WorkloadWindow:
    """Return per-source cumulative deltas, refusing resets and incomplete data."""
    validate_snapshot(before)
    validate_snapshot(after)
    require((before["firmware_elf_sha256"], before["profile_sha256"], before["generation"]) ==
            (after["firmware_elf_sha256"], after["profile_sha256"], after["generation"]),
            "Measurement context changed")
    require(after["timestamp_us"] > before["timestamp_us"], "Measurement time did not advance")
    before_workers = {r["slot"]: r for r in before["workers"]}
    after_workers = {r["slot"]: r for r in after["workers"]}
    require(before_workers.keys() == after_workers.keys(), "Worker bindings changed")
    results: list[WorkerWindow] = []
    for slot in sorted(before_workers):
        before_worker, after_worker = before_workers[slot], after_workers[slot]
        require(before_worker["source_index"] == after_worker["source_index"], "Worker source changed")
        require(before_worker["available"] and after_worker["available"], "Workload counters unavailable")
        before_stats, after_stats = available_stats(before_worker), available_stats(after_worker)
        require(before_stats["flags"] == after_stats["flags"] == 0, "Saturated counters cannot label a calibration window")
        require(all(after_stats[f] >= before_stats[f] for f in FIELDS), "Measurement counter reset")
        source_index = after_worker["source_index"]
        if source_index is None:
            raise ConfigurationError("Invalid source index")
        row: WorkerWindow = dict(
            steps=after_stats["steps"] - before_stats["steps"],
            elapsed_us=after_stats["elapsed_us"] - before_stats["elapsed_us"],
            bus_wait_us=after_stats["bus_wait_us"] - before_stats["bus_wait_us"],
            bus_active_us=after_stats["bus_active_us"] - before_stats["bus_active_us"],
            transfer_bytes=after_stats["transfer_bytes"] - before_stats["transfer_bytes"],
            transfers=after_stats["transfers"] - before_stats["transfers"],
            timeouts=after_stats["timeouts"] - before_stats["timeouts"],
            errors=after_stats["errors"] - before_stats["errors"],
            slot=slot, source_index=source_index,
            max_elapsed_us_since_reset=after_stats["max_elapsed_us"],
            max_lateness_us_since_reset=after_stats["max_lateness_us"])
        require(row["steps"] > 0, "No completed worker steps")
        results.append(row)
    return dict(contract="uthome.workload-window", version="0.1.0", before_sha256=digest(before),
                after_sha256=digest(after), duration_us=after["timestamp_us"] - before["timestamp_us"], workers=results,
                cpu_time_measured=False, electrical_wire_time_measured=False, model_features=False)
