"""Candidate review and approval state transitions."""

from __future__ import annotations

import copy
from datetime import datetime, timezone
from typing import Any

from .serialization import profile_sha256
from .validator import validate_profile


class WorkflowError(ValueError):
    """A requested review transition is unsafe or invalid."""


def new_candidate(profile: dict[str, Any], source: str = "manual") -> dict[str, Any]:
    """Wrap a profile in explicit review state without approving it."""
    if not validate_profile(profile).valid:
        raise WorkflowError("Only a valid profile can become a candidate.")
    return {"format_version": "1.0.0", "status": "candidate", "source": source, "profile": copy.deepcopy(profile), "review": None}


def review_candidate(candidate: dict[str, Any], decision: str, reviewer: str, note: str = "") -> dict[str, Any]:
    """Approve a valid candidate or reject it with an audit record."""
    if not isinstance(candidate, dict) or candidate.get("status") != "candidate" or not isinstance(candidate.get("profile"), dict):
        raise WorkflowError("Only a candidate profile can be reviewed.")
    if decision not in {"approved", "rejected"}:
        raise WorkflowError("Decision must be 'approved' or 'rejected'.")
    if not isinstance(reviewer, str) or not reviewer.strip():
        raise WorkflowError("Reviewer is required.")
    if not isinstance(note, str):
        raise WorkflowError("Review note must be text.")
    if decision == "approved":
        result = validate_profile(candidate["profile"])
        if not result.valid:
            raise WorkflowError(f"Invalid candidate cannot be approved: {result.errors[0].code}.")
    reviewed = copy.deepcopy(candidate)
    reviewed["status"] = decision
    reviewed["review"] = {
        "decision": decision,
        "reviewer": reviewer,
        "note": note,
        "reviewed_at": datetime.now(timezone.utc).isoformat(),
        "profile_sha256": profile_sha256(reviewed["profile"]),
    }
    return reviewed


def approved_profile(artifact: object) -> dict[str, Any]:
    """Return the profile only if its approval still matches its content."""
    if not isinstance(artifact, dict) or artifact.get("status") != "approved":
        raise WorkflowError("An approved review artifact is required.")
    profile = artifact.get("profile")
    review = artifact.get("review")
    if not isinstance(profile, dict) or not isinstance(review, dict) or review.get("decision") != "approved":
        raise WorkflowError("Approval record is missing or inconsistent.")
    result = validate_profile(profile)
    if not result.valid:
        raise WorkflowError(f"Approved profile is invalid: {result.errors[0].code}.")
    if review.get("profile_sha256") != profile_sha256(profile):
        raise WorkflowError("Profile changed after approval; review it again.")
    return profile
