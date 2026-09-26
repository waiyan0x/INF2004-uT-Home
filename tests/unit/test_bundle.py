import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tools.configurator.bundle import BundleError, create_bundle, deploy_to_filesystem
from tools.configurator.workflow import new_candidate, review_candidate


ROOT = Path(__file__).resolve().parents[2]


class BundleTests(unittest.TestCase):
    def setUp(self):
        profile = json.loads((ROOT / "configs/nodes/first-prototype.json").read_text())
        self.candidate = new_candidate(profile)
        self.approved = review_candidate(self.candidate, "approved", "member5")

    def test_candidate_cannot_be_bundled(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(BundleError):
                create_bundle(self.candidate, Path(temp) / "bundle")

    def test_bundle_hashes_and_preserves_model(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            model = root / "input.tflite"
            model.write_bytes(b"model-bytes")
            manifest = create_bundle(self.approved, root / "bundle", model)
            for entry in manifest["artifacts"]:
                content = (root / "bundle" / entry["filename"]).read_bytes()
                self.assertEqual(hashlib.sha256(content).hexdigest(), entry["sha256"])
            self.assertEqual(b"model-bytes", (root / "bundle/model.bin").read_bytes())

    def test_filesystem_adapter_verifies_then_copies(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            create_bundle(self.approved, root / "bundle")
            status = deploy_to_filesystem(root / "bundle", root / "device")
            self.assertEqual("transferred", status["state"])
            self.assertTrue((root / "device/profile.json").is_file())

    def test_profile_changed_after_approval_cannot_be_bundled(self):
        with tempfile.TemporaryDirectory() as temp:
            self.approved["profile"]["devices"][0]["behaviour"]["interval_ms"] = 5000
            with self.assertRaises(BundleError):
                create_bundle(self.approved, Path(temp) / "bundle")

    def test_invalid_model_does_not_leave_partial_bundle(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "bundle"
            with self.assertRaises(BundleError):
                create_bundle(self.approved, output, Path(temp) / "missing.tflite")
            self.assertFalse(output.exists())

    def test_tampered_manifest_cannot_copy_outside_bundle(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            create_bundle(self.approved, root / "bundle")
            manifest_path = root / "bundle/manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["artifacts"][0]["filename"] = "../outside.json"
            manifest_path.write_text(json.dumps(manifest))
            with self.assertRaises(BundleError):
                deploy_to_filesystem(root / "bundle", root / "device")
            self.assertFalse((root / "device").exists())

    def test_corrupted_profile_cannot_be_transferred(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            create_bundle(self.approved, root / "bundle")
            (root / "bundle/profile.json").write_text("{}")
            with self.assertRaises(BundleError):
                deploy_to_filesystem(root / "bundle", root / "device")
            self.assertFalse((root / "device").exists())

    def test_existing_destination_is_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            create_bundle(self.approved, root / "bundle")
            destination = root / "device"
            destination.mkdir()
            (destination / "notes.txt").write_text("keep me")
            with self.assertRaises(BundleError):
                deploy_to_filesystem(root / "bundle", destination)
            self.assertEqual("keep me", (destination / "notes.txt").read_text())

    def test_existing_symlink_destinations_are_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            target = root / "target"
            target.mkdir()
            output_link = root / "bundle-link"
            output_link.symlink_to(target, target_is_directory=True)
            with self.assertRaises(BundleError):
                create_bundle(self.approved, output_link)
            self.assertTrue(output_link.is_symlink())

            create_bundle(self.approved, root / "bundle")
            transfer_link = root / "transfer-link"
            transfer_link.symlink_to(target, target_is_directory=True)
            with self.assertRaises(BundleError):
                deploy_to_filesystem(root / "bundle", transfer_link)
            self.assertTrue(transfer_link.is_symlink())

    def test_malformed_manifest_entry_is_rejected_cleanly(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            create_bundle(self.approved, root / "bundle")
            manifest_path = root / "bundle/manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["artifacts"][0]["filename"] = ["profile.json"]
            manifest_path.write_text(json.dumps(manifest))
            with self.assertRaises(BundleError):
                deploy_to_filesystem(root / "bundle", root / "device")


if __name__ == "__main__":
    unittest.main()
