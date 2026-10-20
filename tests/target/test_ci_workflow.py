"""Validate the repository CI contract without claiming a cloud build."""
from pathlib import Path
import unittest
import yaml

ROOT = Path(__file__).resolve().parents[2]


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.workflow = yaml.load((ROOT / ".github/workflows/c-standard.yml").read_text(),
                                  Loader=yaml.BaseLoader)
        self.job = self.workflow["jobs"]["esp32c3"]
        self.commands = "\n".join(step.get("run", "") for step in self.job["steps"])

    def test_builds_both_images_after_host_gate(self):
        self.assertEqual("c-standard", self.job["needs"])
        self.assertEqual(["acceptance", "configuration"], self.job["strategy"]["matrix"]["image"])
        self.assertEqual("read", self.workflow["permissions"]["contents"])
        self.assertIn("workflow_dispatch", self.workflow["on"])

    def test_exact_repository_pins_and_existing_qualification(self):
        self.assertIn("module.SDK_REVISION", self.commands)
        self.assertIn("module.KERNEL_REVISION", self.commands)
        self.assertIn('checkout --detach FETCH_HEAD', self.commands)
        self.assertIn('submodule update --init --recursive', self.commands)
        self.assertIn('tools/target/qualify_kernel.py build', self.commands)
        self.assertIn('--compat-patch firmware/third_party/patches/0002-esp-idf-6.1-build.patch', self.commands)
        self.assertIn('--resource-patch firmware/third_party/patches/0001-uthome-c3-resources.patch', self.commands)
        self.assertNotIn('--skip-format', self.commands)
        self.assertNotIn('continue-on-error', self.job)

    def test_evidence_survives_failure(self):
        step = next(item for item in self.job["steps"] if item.get("uses", "").startswith("actions/upload-artifact@"))
        self.assertEqual("always()", step["if"])
        for evidence in ("*.elf", "*.map", "evidence.json", "compile_commands.json", "build.log"):
            self.assertIn(evidence, step["with"]["path"])

    def test_preserves_all_host_suites(self):
        host = "\n".join(step.get("run", "") for step in self.workflow["jobs"]["c-standard"]["steps"])
        for suite in ("tests/c_standard", "tests/foundation", "tests/runtime", "tests/target"):
            self.assertIn(suite, host)
        self.assertIn("tools/c_standard/check.py", host)
        self.assertTrue(any(step.get("working-directory") == "legacy/member5"
                            for step in self.workflow["jobs"]["c-standard"]["steps"]))


if __name__ == "__main__":
    unittest.main()
