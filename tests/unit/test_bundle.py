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


if __name__ == "__main__":
    unittest.main()
