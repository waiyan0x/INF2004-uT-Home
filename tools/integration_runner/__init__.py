"""Repeatable host-side scenario evaluation."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from tools.configurator.validator import validate_profile


def run_scenario(scenario_path: Path) -> dict[str, Any]:
    """Validate scenario inputs and describe manual hardware steps explicitly."""
    scenario = json.loads(scenario_path.read_text(encoding="utf-8"))
    results = []
    passed = True
    for node in scenario.get("nodes", []):
        profile_path = (scenario_path.parent / node["profile"]).resolve()
        validation = validate_profile(json.loads(profile_path.read_text(encoding="utf-8")))
        status = "passed" if validation.valid else "failed"
        passed = passed and validation.valid
        results.append({"step": f"profile:{node['node_id']}", "status": status, "errors": validation.to_dict()["errors"]})
    for step in scenario.get("demonstration_steps", []):
        results.append({
            "step": step["id"],
            "action": step["action"],
            "expected": step["expected"],
            "status": "manual_pending",
            "reason": "Requires connected prototype hardware and an agreed firmware transport.",
        })
    return {"scenario_id": scenario.get("scenario_id"), "host_checks_passed": passed, "results": results}
