import json
import unittest
from pathlib import Path

from tools.configurator.workflow import new_candidate, review_candidate


ROOT = Path(__file__).resolve().parents[2]


class WorkflowTests(unittest.TestCase):
    def test_candidate_keeps_its_own_profile_copy(self):
        profile = json.loads((ROOT / "configs/nodes/first-prototype.json").read_text())
        candidate = new_candidate(profile)
        profile["node_id"] = "changed-after-creation"
        self.assertEqual("prototype-node-01", candidate["profile"]["node_id"])

    def test_approval_records_profile_digest(self):
        profile = json.loads((ROOT / "configs/nodes/first-prototype.json").read_text())
        approved = review_candidate(new_candidate(profile), "approved", "member5")
        self.assertRegex(approved["review"]["profile_sha256"], r"^[0-9a-f]{64}$")


if __name__ == "__main__":
    unittest.main()
