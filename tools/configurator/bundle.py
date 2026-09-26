"""Verified deployment bundles and a local filesystem transfer adapter."""

from __future__ import annotations

import hashlib
import json
import shutil
import tempfile
from pathlib import Path
from typing import Any

from .serialization import canonical_json_bytes, profile_sha256
from .validator import validate_profile
from .workflow import WorkflowError, approved_profile


class BundleError(ValueError):
    """A bundle cannot be created or transferred safely."""


def _file_entry(path: Path) -> dict[str, object]:
    content = path.read_bytes()
    return {"filename": path.name, "size_bytes": len(content), "sha256": hashlib.sha256(content).hexdigest()}


def _staging_directory(destination: Path) -> Path:
    destination.parent.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=".ut-home-stage-", dir=destination.parent))


def create_bundle(approved: dict[str, Any], output_dir: Path, model_path: Path | None = None) -> dict[str, Any]:
    """Create a complete bundle only after confirming profile approval."""
    try:
        profile = approved_profile(approved)
    except WorkflowError as error:
        raise BundleError(str(error)) from error
    if output_dir.exists() or output_dir.is_symlink():
        raise BundleError("Output directory already exists; choose a new path.")
    if model_path is not None and (not model_path.is_file() or model_path.is_symlink()):
        raise BundleError("Model artifact must be an existing regular file.")

    stage = _staging_directory(output_dir)
    try:
        (stage / "profile.json").write_bytes(canonical_json_bytes(profile))
        artifacts = [_file_entry(stage / "profile.json")]
        if model_path is not None:
            shutil.copyfile(model_path, stage / "model.bin")
            artifacts.append(_file_entry(stage / "model.bin"))
        manifest = {
            "bundle_format_version": "1.1.0",
            "deployment_state": "packaged",
            "profile_schema_version": profile["schema_version"],
            "node_id": profile["node_id"],
            "approved_profile_sha256": profile_sha256(profile),
            "artifacts": artifacts,
        }
        (stage / "manifest.json").write_bytes(canonical_json_bytes(manifest))
        stage.rename(output_dir)
        return manifest
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def verify_bundle(bundle_dir: Path) -> dict[str, Any]:
    """Verify manifest structure, file set, hashes, and profile contract."""
    manifest_path = bundle_dir / "manifest.json"
    if not bundle_dir.is_dir() or manifest_path.is_symlink() or not manifest_path.is_file():
        raise BundleError("Bundle manifest is missing.")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise BundleError("Bundle manifest is not valid JSON.") from error
    if not isinstance(manifest, dict) or manifest.get("bundle_format_version") != "1.1.0":
        raise BundleError("Unsupported bundle format.")
    entries = manifest.get("artifacts")
    if not isinstance(entries, list) or len(entries) not in (1, 2):
        raise BundleError("Bundle must contain a profile and at most one model.")
    expected_names = {"profile.json"} | ({"model.bin"} if len(entries) == 2 else set())
    if any(not isinstance(entry, dict) or not isinstance(entry.get("filename"), str) for entry in entries):
        raise BundleError("Bundle has a malformed artifact entry.")
    if {entry["filename"] for entry in entries} != expected_names:
        raise BundleError("Bundle has an unexpected artifact name.")
    if {path.name for path in bundle_dir.iterdir()} != expected_names | {"manifest.json"}:
        raise BundleError("Bundle contains missing or extra files.")
    for entry in entries:
        path = bundle_dir / entry["filename"]
        if path.is_symlink() or not path.is_file() or _file_entry(path) != entry:
            raise BundleError(f"Artifact verification failed: {entry['filename']}.")
    try:
        profile = json.loads((bundle_dir / "profile.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise BundleError("Bundle profile is not valid JSON.") from error
    if not validate_profile(profile).valid:
        raise BundleError("Bundle profile is invalid.")
    if (
        manifest.get("deployment_state") != "packaged"
        or manifest.get("node_id") != profile["node_id"]
        or manifest.get("profile_schema_version") != profile["schema_version"]
        or manifest.get("approved_profile_sha256") != profile_sha256(profile)
    ):
        raise BundleError("Bundle metadata does not match its profile.")
    return manifest


def deploy_to_filesystem(bundle_dir: Path, destination: Path) -> dict[str, str]:
    """Transfer a verified bundle to a new local directory for host tests."""
    manifest = verify_bundle(bundle_dir)
    if destination.exists() or destination.is_symlink():
        raise BundleError("Transfer destination already exists; choose a new path.")
    stage = _staging_directory(destination)
    try:
        for name in ("manifest.json", *(entry["filename"] for entry in manifest["artifacts"])):
            shutil.copyfile(bundle_dir / name, stage / name)
        verify_bundle(stage)
        stage.rename(destination)
        return {"state": "transferred", "adapter": "filesystem", "destination": str(destination)}
    finally:
        if stage.exists():
            shutil.rmtree(stage)
