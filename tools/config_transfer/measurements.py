"""Validate workload measurements received from the runtime observer."""
from typing import cast

from tools.config_transfer.payloads import WorkloadMeasurement
from tools.config_transfer.workcost import FIELDS
from tools.config_validator.planner import ConfigurationError, require


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
