"""Configuration mutation boundaries that refactors must preserve."""
from dataclasses import replace
import json
import unittest

from test_configurator import FakeClient, Port
from tools.config_transfer.client import TransferError
from tools.config_transfer.protocol import Command
from tools.configurator.workflow import Workflow
from tools.config_validator.planner import ROOT, load_document


class MutationBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.port = Port()
        self.flow = Workflow(opener=lambda _: self.port, client_factory=FakeClient)
        self.addCleanup(self.flow.close)
        self.flow.dispatch("connect", {"port": "fake"})
        self.client = self.flow.client
        self.profile = json.dumps(load_document(ROOT / "configs/nodes/distance-only.json"))

    def review(self):
        return self.flow.dispatch("review", {"profile": self.profile})

    def test_save_uses_active_revision_and_invalidates_review(self):
        self.client.current = replace(self.client.current, active_revision=7, saved_revision=6)
        review = self.review()
        self.flow.dispatch("save", {})
        self.assertEqual(len(self.client.calls), 1)
        command, request_id, revision = self.client.calls[0]
        self.assertEqual((command, revision), (Command.SAVE, 7))
        self.assertGreater(request_id, 0)
        with self.assertRaisesRegex(ValueError, "Review the current profile again"):
            self.flow.dispatch("apply", {"review_id": review["review_id"]})
        self.assertEqual(len(self.client.calls), 1)

    def test_save_without_active_configuration_does_not_send(self):
        self.client.current = replace(self.client.current, active_revision=0)
        with self.assertRaisesRegex(ValueError, "no active configuration"):
            self.flow.dispatch("save", {})
        self.assertEqual(self.client.calls, [])

    def test_exhausted_revision_and_request_ids_do_not_send(self):
        self.client.current = replace(self.client.current, active_revision=0xFFFFFFFF)
        review = self.review()
        with self.assertRaisesRegex(ValueError, "sequence exhausted"):
            self.flow.dispatch("apply", {"review_id": review["review_id"]})
        self.assertEqual(self.client.calls, [])
        self.client.current = replace(self.client.current, active_revision=1, request_id=0xFFFFFFFE)
        with self.assertRaisesRegex(ValueError, "sequence exhausted"):
            self.flow.dispatch("save", {})
        self.assertEqual(self.client.calls, [])

    def test_lost_save_or_recover_reply_disconnects_without_retry(self):
        for action, flags in (("save", 16), ("recover", 32)):
            with self.subTest(action=action):
                port = Port()
                flow = Workflow(opener=lambda _: port, client_factory=FakeClient)
                self.addCleanup(flow.close)
                flow.dispatch("connect", {"port": "fake"})
                client = flow.client
                client.current = replace(client.current, active_revision=1, flags=flags)
                client.fail = True
                with self.assertRaises(TransferError):
                    flow.dispatch(action, {})
                self.assertEqual(len(client.calls), 1)
                self.assertEqual(client.calls[0][0], Command[action.upper()])
                self.assertTrue(port.closed)
                self.assertIsNone(flow.client)
                self.assertIsNone(flow.pending)


if __name__ == "__main__":
    unittest.main()
