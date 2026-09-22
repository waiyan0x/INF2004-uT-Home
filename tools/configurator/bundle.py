"""Create and verify deployment bundles without claiming target activation."""

from __future__ import annotations

import hashlib
import json
import shutil
from pathlib import Path
from typing import Any

from .validator import validate_profile


class BundleError(ValueError):
    """An approved artifact cannot be packaged safely."""


def canonical_json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode("utf-8")


def _file_entry(path: Path) -> dict[str, object]:
    content = path.read_bytes()
    return {"filename": path.name, "size_bytes": len(content), "sha256": hashlib.sha256(content).hexdigest()}


def create_bundle(approved: dict[str, Any], output_dir: Path, model_path: Path | None = None) -> dict[str, Any]:
    """Write a checksummed profile/model bundle and return its manifest."""
    if approved.get("status") != "approved" or not isinstance(approved.get("profile"), dict):
        raise BundleError("A reviewed artifact with status 'approved' is required.")
    result = validate_profile(approved["profile"])
    if not result.valid:
        raise BundleError(f"Approved profile is no longer valid: {result.errors[0].code}.")
    if output_dir.exists() and any(output_dir.iterdir()):
        raise BundleError("Output directory must be empty.")
    output_dir.mkdir(parents=True, exist_ok=True)
    profile_path = output_dir / "profile.json"
    profile_path.write_bytes(canonical_json_bytes(approved["profile"]))
    artifacts = [_file_entry(profile_path)]
    if model_path is not None:
        if not model_path.is_file():
            raise BundleError("Model artifact does not exist.")
        destination = output_dir / "model.bin"
        shutil.copyfile(model_path, destination)
        artifacts.append(_file_entry(destination))
    manifest = {
        "bundle_format_version": "1.0.0",
        "deployment_state": "packaged",
        "profile_schema_version": approved["profile"]["schema_version"],
        "node_id": approved["profile"]["node_id"],
        "artifacts": artifacts,
    }
    (output_dir / "manifest.json").write_bytes(canonical_json_bytes(manifest))
    return manifest


def deploy_to_filesystem(bundle_dir: Path, destination: Path) -> dict[str, str]:
    """Copy a verified local bundle to a filesystem adapter destination."""
    manifest_path = bundle_dir / "manifest.json"
    if not manifest_path.is_file():
        raise BundleError("Bundle manifest is missing.")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for entry in manifest.get("artifacts", []):
        artifact = bundle_dir / entry["filename"]
        actual = _file_entry(artifact)
        if actual != entry:
            raise BundleError(f"Artifact verification failed: {entry['filename']}.")
    destination.mkdir(parents=True, exist_ok=True)
    for path in bundle_dir.iterdir():
        if path.is_file():
            shutil.copyfile(path, destination / path.name)
    return {"state": "transferred", "adapter": "filesystem", "destination": str(destination)}
