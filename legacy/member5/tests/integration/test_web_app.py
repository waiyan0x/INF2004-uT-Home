import json
import threading
import unittest
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from tools.configurator.web import make_server


ROOT = Path(__file__).resolve().parents[2]


class WebAppTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = make_server("127.0.0.1", 0)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown(); cls.server.server_close(); cls.thread.join()

    def request(self, path, value=None):
        data = json.dumps(value).encode() if value is not None else None
        request = Request(self.base + path, data=data, headers={"Content-Type": "application/json"} if data else {})
        with urlopen(request) as response:
            return response.status, json.loads(response.read()) if path.startswith("/api") else response.read().decode()

    def test_page_and_sample_are_available(self):
        status, page = self.request("/")
        self.assertEqual(200, status)
        self.assertIn("Configuration Desk", page)

    def test_validate_and_approve_use_same_core(self):
        profile = json.loads((ROOT / "configs/nodes/first-prototype.json").read_text())
        _, result = self.request("/api/validate", profile)
        self.assertTrue(result["valid"])
        _, candidate = self.request("/api/candidate", profile)
        _, approved = self.request("/api/approve", {"candidate": candidate, "reviewer": "member5"})
        self.assertEqual("approved", approved["status"])

    def test_invalid_candidate_api_is_blocked(self):
        profile = json.loads((ROOT / "configs/invalid/pin-conflict.json").read_text())
        with self.assertRaises(HTTPError) as context:
            self.request("/api/candidate", profile)
        self.assertEqual(422, context.exception.code)

    def test_structured_recommendation_generates_valid_profile(self):
        recommendation = json.loads((ROOT / "configs/recommendations/first-prototype.json").read_text())
        _, profile = self.request("/api/generate", recommendation)
        self.assertEqual("prototype-node-01", profile["node_id"])
        _, validation = self.request("/api/validate", profile)
        self.assertTrue(validation["valid"])

    def test_health_api_accepts_imported_snapshot(self):
        snapshot = json.loads((ROOT / "configs/health/first-prototype.json").read_text())
        _, normalised = self.request("/api/health", snapshot)
        self.assertEqual("healthy", normalised["overall"])


if __name__ == "__main__":
    unittest.main()
