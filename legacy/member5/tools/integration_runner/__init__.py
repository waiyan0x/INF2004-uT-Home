"""Host checks and separately attributed manual demonstration evidence."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from tools.configurator.validator import validate_profile


class ScenarioError(ValueError):
    """A scenario or its evidence file has an invalid contract."""


def _read_object(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ScenarioError(f"{path.name} must contain a JSON object.")
    return value


def _load_evidence(path: Path, scenario_id: str, step_ids: set[str]) -> dict[str, dict[str, str]]:
    evidence = _read_object(path)
    if evidence.get("scenario_id") != scenario_id or not isinstance(evidence.get("steps"), list):
        raise ScenarioError("Evidence must name this scenario and contain a steps list.")
    reported: dict[str, dict[str, str]] = {}
    for item in evidence["steps"]:
        if not isinstance(item, dict) or not isinstance(item.get("id"), str):
            raise ScenarioError("Evidence step must have a string id.")
        step_id = item["id"]
        if step_id not in step_ids or step_id in reported:
            raise ScenarioError(f"Unknown or duplicate evidence step: {step_id!r}.")
        if item.get("status") not in {"passed", "failed"}:
            raise ScenarioError(f"Evidence step {step_id!r} must be passed or failed.")
        observed = item.get("observed")
        if not isinstance(observed, str) or not observed.strip() or len(observed) > 1000:
            raise ScenarioError(f"Evidence step {step_id!r} needs a brief observation.")
        reported[step_id] = {"status": item["status"], "observed": observed}
    return reported


def run_scenario(scenario_path: Path, evidence_path: Path | None = None) -> dict[str, Any]:
    """Run host checks and label any manual evidence as user reported."""
    scenario = _read_object(scenario_path)
    scenario_id = scenario.get("scenario_id")
    nodes = scenario.get("nodes")
    steps = scenario.get("demonstration_steps")
    if not isinstance(scenario_id, str) or not isinstance(nodes, list) or not isinstance(steps, list):
        raise ScenarioError("Scenario needs an id, nodes list, and demonstration_steps list.")
    if len(nodes) > 8 or len(steps) > 32:
        raise ScenarioError("Scenario exceeds the bounded node or step count.")
    step_ids = [step.get("id") for step in steps if isinstance(step, dict)]
    if len(step_ids) != len(steps) or any(not isinstance(step_id, str) for step_id in step_ids) or len(set(step_ids)) != len(step_ids):
        raise ScenarioError("Demonstration step ids must be unique strings.")
    evidence = _load_evidence(evidence_path, scenario_id, set(step_ids)) if evidence_path else {}

    results: list[dict[str, Any]] = []
    host_passed = True
    for node in nodes:
        if not isinstance(node, dict) or not isinstance(node.get("node_id"), str) or not isinstance(node.get("profile"), str):
            raise ScenarioError("Each node needs a node_id and profile path.")
        profile_path = (scenario_path.parent / node["profile"]).resolve()
        validation = validate_profile(_read_object(profile_path))
        host_passed = host_passed and validation.valid
        results.append({
            "step": f"profile:{node['node_id']}",
            "status": "passed" if validation.valid else "failed",
            "errors": validation.to_dict()["errors"],
        })
    manual_complete = True
    for step in steps:
        if not all(isinstance(step.get(key), str) for key in ("id", "action", "expected")):
            raise ScenarioError("Each demonstration step needs id, action, and expected text.")
        report = evidence.get(step["id"])
        status = f"manual_reported_{report['status']}" if report else "manual_pending"
        manual_complete = manual_complete and status == "manual_reported_passed"
        results.append({
            "step": step["id"],
            "action": step["action"],
            "expected": step["expected"],
            "status": status,
            "reason": "Reported in supplied evidence; not independently verified." if report else "Requires connected prototype hardware and recorded observations.",
            "observed": report["observed"] if report else None,
        })
    return {
        "scenario_id": scenario_id,
        "host_checks_passed": host_passed,
        "demonstration_complete": host_passed and manual_complete,
        "results": results,
    }
