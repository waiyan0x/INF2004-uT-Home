"""Canonical profile bytes shared by review and deployment artifacts."""

from __future__ import annotations

import hashlib
import json


def canonical_json_bytes(value: object) -> bytes:
    """Encode JSON identically for approval digests and bundle files."""
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode("utf-8")


def profile_sha256(profile: object) -> str:
    """Fingerprint the exact normalized profile approved by a reviewer."""
    return hashlib.sha256(canonical_json_bytes(profile)).hexdigest()
