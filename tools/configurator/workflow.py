"""Candidate review and approval state transitions."""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Any

from .validator import validate_profile


class WorkflowError(ValueError):
    """A requested review transition is unsafe or invalid."""


def new_candidate(profile: dict[str, Any], source: str = "manual") -> dict[str, Any]:
    """Wrap a profile in explicit review state without approving it."""
    return {"format_version": "1.0.0", "status": "candidate", "source": source, "profile": profile, "review": None}


def review_candidate(candidate: dict[str, Any], decision: str, reviewer: str, note: str = "") -> dict[str, Any]:
    """Approve a valid candidate or reject it with an audit record."""
    if candidate.get("status") != "candidate" or not isinstance(candidate.get("profile"), dict):
        raise WorkflowError("Only a candidate profile can be reviewed.")
    if decision not in {"approved", "rejected"}:
        raise WorkflowError("Decision must be 'approved' or 'rejected'.")
    if not reviewer.strip():
        raise WorkflowError("Reviewer is required.")
    if decision == "approved":
        result = validate_profile(candidate["profile"])
        if not result.valid:
            raise WorkflowError(f"Invalid candidate cannot be approved: {result.errors[0].code}.")
    reviewed = dict(candidate)
    reviewed["status"] = decision
    reviewed["review"] = {
        "decision": decision,
        "reviewer": reviewer,
        "note": note,
        "reviewed_at": datetime.now(timezone.utc).isoformat(),
    }
    return reviewed
