"""Catalogue-backed node admission and normalized planning; no hardware I/O."""

from __future__ import annotations

from collections import Counter
from copy import deepcopy
from typing import Any

# Preserve the existing planner import surface used by host tools.
from .catalogue import Catalogue as Catalogue
from .documents import (
    ROOT as ROOT,
    ConfigurationError as ConfigurationError,
    check as check,
    load_document as load_document,
    parse_document as parse_document,
    require as require,
    unique_by as unique_by,
)
from .wiring import WiringClaims
from .recipe import validate_recipe_selection


def build_plan(
    document: dict[str, Any],
    manifest: dict[str, Any],
    catalogue: Catalogue | None = None,
) -> dict[str, Any]:
    """Validate a complete candidate without mutating it or any active state.

    The manifest represents the selected build's supported driver IDs. A target
    must independently enforce its compiled registry; this is not provisioning.
    """
    catalogue = catalogue or Catalogue()
    document = deepcopy(document)
    check(document, catalogue.node_schema, "node")
    require(manifest.get("manifest_version") == "1.0.0", "Unsupported build manifest")
    require(
        manifest.get("backend") in {"host-model", "target"}, "Unknown build backend"
    )
    require(
        isinstance(manifest.get("drivers"), list)
        and all(isinstance(item, str) for item in manifest["drivers"]),
        "Build manifest requires driver IDs",
    )
    board_key = (document["board"]["platform"], document["board"]["variant"],
                 document["board"]["capability_version"])
    require(
        board_key in catalogue.boards, f"Unsupported board capabilities: {board_key}"
    )
    board = catalogue.boards[board_key]
    require(
        manifest.get("board") == board["variant"]
        and manifest.get("board_capability_version") == board["capability_version"],
        "Build and board capability versions do not match",
    )
    limits = board["resources"]
    target_limits = (
        board.get("target_limits", {}) if manifest["backend"] == "target" else {}
    )
    runtime = document["runtime"]
    require(
        runtime["max_device_tasks"] <= limits["max_device_tasks"],
        "Task limit exceeds board capacity",
    )
    require(
        runtime["queue_depth"] <= limits["max_queue_depth"],
        "Queue depth exceeds board capacity",
    )
    require(
        len(document["devices"]) <= limits["max_devices"],
        "Too many configured instances",
    )
    require(len(document["queues"]) <= limits["max_queues"], "Too many queues")
    interfaces = unique_by(document["interfaces"], "id", "interface ID")
    queues = unique_by(document["queues"], "id", "queue ID")
    unique_by(document["devices"], "device_id", "device ID")
    unique_by([dev["task"] for dev in document["devices"]], "name", "task name")
    for queue in queues.values():
        require(
            queue["depth"] <= runtime["queue_depth"], "Queue depth exceeds node policy"
        )

    wiring = WiringClaims(board, target_limits)
    wiring.admit_interfaces(interfaces)

    queue_consumers: dict[str, str] = {}
    instance_counts: Counter[str] = Counter()
    driver_limits: dict[str, int] = {}
    tasks = []
    stack_bytes = 0
    active_count = 0
    for device in document["devices"]:
        reference = device["descriptor"]
        key = (reference["id"], reference["version"])
        require(key in catalogue.descriptors, f"Unknown descriptor/version: {key}")
        descriptor = catalogue.descriptors[key]
        driver = descriptor["driver_id"]
        require(device["driver"] == driver, "Driver does not match descriptor")
        require(
            driver in manifest["drivers"],
            f"Driver unavailable in selected build: {driver}",
        )
        check(device, descriptor["instance_schema"], device["device_id"])
        if driver == "i2c_recipe":
            validate_recipe_selection(device["behaviour"])
        instance_counts[driver] += 1
        driver_limits[driver] = min(
            driver_limits.get(driver, 8),
            descriptor["resources"]["max_instances"],
            target_limits.get("driver_instances", {}).get(driver, 8),
        )
        wiring.admit_connection(device, descriptor, interfaces)
        _admit_routes(device, descriptor, queues, queue_consumers)
        task = _plan_task(device, descriptor, limits)
        if device["enabled"]:
            active_count += 1
            stack_bytes += task["stack_size_bytes"]
        tasks.append(task)
    require(
        all(instance_counts[key] <= value for key, value in driver_limits.items()),
        "Driver instance capacity exceeded",
    )
    require(
        active_count <= runtime["max_device_tasks"],
        "Enabled instances exceed task capacity",
    )
    require(
        stack_bytes <= limits["device_stack_budget_bytes"],
        "Device stacks exceed board budget",
    )
    rules = rule_plan(document, catalogue)
    return {
        "plan_version": "1.0.0",
        "node_schema_version": document["schema_version"],
        "node_id": document["node_id"],
        "rules": rules,
        "board": deepcopy(document["board"]),
        "build_id": manifest.get("build_id"),
        "backend": manifest["backend"],
        "devices": tasks,
        "interfaces": deepcopy(document["interfaces"]),
        "queues": deepcopy(document["queues"]),
        "resources": {
            "active_tasks": active_count,
            "device_stack_bytes": stack_bytes,
            "queue_payload_bytes": sum(
                q["depth"] * limits["max_queue_item_bytes"] for q in queues.values()
            ),
        },
    }


def rule_plan(document: dict[str, Any], catalogue: Catalogue) -> list[dict[str, Any]]:
    """Resolve typed source identity and a single command owner per output."""
    devices = {d["device_id"]: (i, d) for i, d in enumerate(document["devices"])}
    metric_types = {
        "distance_mm": "distance_sample",
        "temperature_centi_c": "environment_sample",
        "pressure_pa": "environment_sample",
        "target_count": "target_report",
    }
    outputs = {
        name
        for name, (_, dev) in devices.items()
        if dev["behaviour"].get("profile") == "rule_output"
    }
    rules = document.get("rules", [])
    require(
        not outputs or document["schema_version"] == "2.1.0",
        "Rule outputs require schema 2.1.0",
    )
    used = set()
    result = []
    for rule in rules:
        require(
            rule["source"] in devices and rule["target"] in outputs,
            "Unknown rule source or output",
        )
        require(rule["target"] not in used, "An output can have only one rule")
        used.add(rule["target"])
        source_index, source = devices[rule["source"]]
        target_index, target = devices[rule["target"]]
        descriptor = catalogue.descriptors[
            (source["descriptor"]["id"], source["descriptor"]["version"])
        ]
        require(
            descriptor["routing"]["output_type"] == metric_types[rule["metric"]],
            "Rule metric/source type mismatch",
        )
        require(
            not target["enabled"] or source["enabled"],
            "An enabled output needs an enabled source",
        )
        result.append(dict(rule, source_index=source_index, target_index=target_index))
    require(
        all(name in used or not devices[name][1]["enabled"] for name in outputs),
        "Every enabled output requires one rule",
    )
    return result


def _admit_routes(device: dict[str, Any], descriptor: dict[str, Any],
                  queues: dict[str, dict[str, Any]], queue_consumers: dict[str, str]) -> None:
    """Check typed routes and reserve each enabled FIFO consumer."""
    behaviour = device["behaviour"]
    routing = descriptor["routing"]
    if routing["output_type"] is not None:
        destination = behaviour.get("destination")
        require(destination in queues, "Unknown destination queue")
        require(
            queues[destination]["item_type"] == routing["output_type"],
            "Queue sample type mismatch",
        )
    sources = behaviour.get("sources", [])
    require(len(sources) == len(set(sources)), "Duplicate source queue")
    for source in sources:
        require(source in queues, "Unknown source queue")
        require(
            queues[source]["item_type"] in routing["input_types"],
            "Unsupported consumer sample type",
        )
        if device["enabled"]:
            require(
                source not in queue_consumers,
                f"Queue {source} has more than one enabled consumer",
            )
            queue_consumers[source] = device["device_id"]


def _plan_task(device: dict[str, Any], descriptor: dict[str, Any],
               limits: dict[str, Any]) -> dict[str, Any]:
    """Validate and normalize one task independently of aggregate budgets."""
    behaviour = device["behaviour"]
    task = device["task"]
    require(
        limits["min_device_priority"]
        <= task["priority"]
        <= limits["max_device_priority"],
        "Priority is reserved for a platform service",
    )
    require(
        task["stack_size_bytes"] >= descriptor["resources"]["min_stack_bytes"]
        and task["stack_size_bytes"] % 256 == 0,
        "Invalid driver stack allocation",
    )
    execution = descriptor["execution"]
    period = (
        0 if execution["period_field"] is None else behaviour[execution["period_field"]]
    )
    timeout = behaviour[execution["timeout_field"]] * (
        1000 if execution["timeout_unit"] == "ms" else 1
    )
    require(
        (execution["kind"] == "stream" and period == 0)
        or (execution["kind"] != "stream" and type(period) is int and period > 0),
        "Invalid execution period",
    )
    require(
        type(timeout) is int and 0 < timeout <= 0xFFFFFFFF, "Invalid normalized timeout"
    )
    return {
        "device_id": device["device_id"],
        "driver_id": descriptor["driver_id"],
        "descriptor": deepcopy(device["descriptor"]),
        "enabled": device["enabled"],
        "execution_kind": execution["kind"],
        "period_ms": int(period),
        "timeout_us": int(timeout),
        "priority": int(task["priority"]),
        "stack_size_bytes": int(task["stack_size_bytes"]),
        "configuration": deepcopy(device),
    }
