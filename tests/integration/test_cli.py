import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class CliTests(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run(
            [sys.executable, "-m", "tools.configurator", *map(str, args)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_validate_exit_codes(self):
        valid = self.run_cli("validate", ROOT / "configs/nodes/first-prototype.json")
        invalid = self.run_cli("validate", ROOT / "configs/invalid/pin-conflict.json")
        self.assertEqual(0, valid.returncode)
        self.assertEqual(2, invalid.returncode)
        self.assertIn("PIN_CONFLICT", invalid.stdout)

    def test_generate_approve_and_bundle(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            candidate = root / "candidate.json"
            approved = root / "approved.json"
            bundle = root / "bundle"
            generated = self.run_cli("generate", ROOT / "configs/recommendations/first-prototype.json", "--output", candidate)
            self.assertEqual(0, generated.returncode, generated.stderr)
            self.assertEqual("candidate", json.loads(candidate.read_text())["status"])
            reviewed = self.run_cli("approve", candidate, "--reviewer", "member5", "--output", approved)
            self.assertEqual(0, reviewed.returncode, reviewed.stderr)
            packaged = self.run_cli("bundle", approved, "--output", bundle)
            self.assertEqual(0, packaged.returncode, packaged.stderr)
            self.assertTrue((bundle / "manifest.json").is_file())

    def test_invalid_candidate_cannot_be_approved(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            profile = json.loads((ROOT / "configs/invalid/pin-conflict.json").read_text())
            candidate = root / "candidate.json"
            candidate.write_text(json.dumps({"format_version": "1.0.0", "status": "candidate", "source": "test", "profile": profile, "review": None}))
            result = self.run_cli("approve", candidate, "--reviewer", "member5", "--output", root / "approved.json")
            self.assertEqual(2, result.returncode)
            self.assertFalse((root / "approved.json").exists())


if __name__ == "__main__":
    unittest.main()
