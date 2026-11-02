"""Validate bounded workcost snapshots supplied by the runtime observer.

Telemetry version 3 retains its format; command 9 supplies costs. Never infer missing
transfer/step counters from baud, USB round-trip time or sample freshness.
Snapshots are calibration evidence, not automatically added model features.
"""
from ai.timing import digest
from tools.config_validator.planner import require


FIELDS = ("steps", "elapsed_us", "max_elapsed_us", "max_lateness_us", "bus_wait_us",
          "bus_active_us", "transfer_bytes", "transfers", "timeouts", "errors", "flags")
SUMS = tuple(f for f in FIELDS if f not in ("max_elapsed_us", "max_lateness_us", "flags"))


def validate_snapshot(value):
    require(isinstance(value, dict) and set(value) == {
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
    return value


def summarize_window(before, after):
    """Return per-source cumulative deltas, refusing resets and incomplete data."""
    validate_snapshot(before)
    validate_snapshot(after)
    require(all(before[k] == after[k] for k in ("firmware_elf_sha256", "profile_sha256", "generation")),
            "Measurement context changed")
    require(after["timestamp_us"] > before["timestamp_us"], "Measurement time did not advance")
    before_workers = {r["slot"]: r for r in before["workers"]}
    after_workers = {r["slot"]: r for r in after["workers"]}
    require(before_workers.keys() == after_workers.keys(), "Worker bindings changed")
    results = []
    for slot in sorted(before_workers):
        before_worker, after_worker = before_workers[slot], after_workers[slot]
        require(before_worker["source_index"] == after_worker["source_index"], "Worker source changed")
        require(before_worker["available"] and after_worker["available"], "Workload counters unavailable")
        before_stats, after_stats = before_worker["stats"], after_worker["stats"]
        require(before_stats["flags"] == after_stats["flags"] == 0, "Saturated counters cannot label a calibration window")
        require(all(after_stats[f] >= before_stats[f] for f in FIELDS), "Measurement counter reset")
        row = {f: after_stats[f] - before_stats[f] for f in SUMS}
        require(row["steps"] > 0, "No completed worker steps")
        row.update(slot=slot, source_index=after_worker["source_index"],
                   max_elapsed_us_since_reset=after_stats["max_elapsed_us"],
                   max_lateness_us_since_reset=after_stats["max_lateness_us"])
        results.append(row)
    return dict(contract="uthome.workload-window", version="0.1.0", before_sha256=digest(before),
                after_sha256=digest(after), duration_us=after["timestamp_us"] - before["timestamp_us"], workers=results,
                cpu_time_measured=False, electrical_wire_time_measured=False, model_features=False)
