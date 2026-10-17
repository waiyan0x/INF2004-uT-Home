"""Discovery-to-draft checks with a fake USB client; no board access."""
from copy import deepcopy
from dataclasses import replace
import json
import unittest

from tools.config_transfer.discovery import Discovery
from tools.config_transfer.telemetry import Snapshot
from tools.config_transfer.client import TransferError
from tools.configurator.workflow import Workflow
from tools.config_validator.planner import ROOT, load_document
from test_configurator import FakeClient, Port


class DiscoveryClient(FakeClient):
    def __init__(self, stream):
        super().__init__(stream)
        self.caps = replace(self.caps, features=2)
        self.capture = Discovery(10, 0, 0, "complete", 1, 123, 1000, 5, "a" * 64, (
            dict(address=118, register=0, status="not_responding", value=None),
            dict(address=119, register=0, status="responding", value=80)))
        self.timestamp = 1010
        self.snapshot_result = 0
        self.probes = 0

    def discover_i2c(self, *, wiring_confirmed):
        assert wiring_confirmed is True
        self.probes += 1
        if self.fail:
            raise TransferError("reply lost; outcome unknown")
        return self.capture, self.caps

    def telemetry(self):
        state = {key: getattr(self.current, key) for key in
                 ("active_revision", "saved_revision", "active_crc", "saved_crc", "flags")}
        return Snapshot(11, self.snapshot_result, self.timestamp, state,
                        dict(failed_workers=0, service_error=0), dict(total_events=0), (), ())


class DiscoveryUITests(unittest.TestCase):
    def setUp(self):
        self.now = 0
        self.flow = Workflow(opener=lambda _: Port(), client_factory=DiscoveryClient, clock=lambda: self.now)
        self.addCleanup(self.flow.close)
        self.flow.dispatch("connect", {"port": "fake"})
        self.client = self.flow.client
        self.document = load_document(ROOT / "configs/nodes/distance-only.json")
        self.body = dict(profile=json.dumps(self.document), preset_id="bench_i2c", wiring_confirmed=True)

    def detect(self):
        return self.flow.dispatch("discover", self.body)

    def add(self, report, **changes):
        return self.flow.dispatch("add_discovered", self.body | dict(discovery_id=report["discovery_id"], index=1) | changes)

    def test_complete_flow_preserves_draft_and_requires_separate_review_apply(self):
        report = self.detect()
        settings = {row["label"]: row for row in report["devices"][1]["recommendation"]["settings"]}
        self.assertEqual(settings["Protocol"]["value"], "I2C")
        self.assertEqual(settings["Reading interval"]["source"], "bench_observation")
        self.assertEqual(settings["Reading interval"]["value"], "500 ms")
        self.assertEqual(settings["Read timeout"]["value"], "100 ms")
        # Browser-supplied settings are not trusted or used.
        draft = self.add(report, instance={"driver": "evil"})["profile"]
        self.assertEqual(draft["devices"][:-1], self.document["devices"])
        self.assertEqual(draft["queues"][:-1], self.document["queues"])
        self.assertEqual(draft["runtime"], self.document["runtime"])
        self.assertEqual(draft["devices"][-1]["driver"], "bmp388")
        self.assertEqual(self.client.calls, [])
        self.assertIsNone(self.flow.pending)
        with self.assertRaises(ValueError): self.add(report)
        review = self.flow.dispatch("review", {"profile": json.dumps(draft)})
        self.flow.dispatch("apply", {"review_id": review["review_id"]})
        self.assertEqual(len(self.client.calls), 1)

    def test_confirmation_preset_and_feature_checks_happen_before_probe(self):
        for changes in ({"wiring_confirmed": False}, {"wiring_confirmed": "yes"}, {"preset_id": "arbitrary"}):
            with self.assertRaises(ValueError): self.flow.dispatch("discover", self.body | changes)
        self.flow.caps = replace(self.flow.caps, features=0)
        with self.assertRaisesRegex(ValueError, "does not support discovery"): self.detect()
        self.assertEqual(self.client.probes, 0)
        self.assertEqual(self.client.calls, [])

    def test_unknown_no_response_and_unsupported_cannot_add(self):
        report = self.detect()
        with self.assertRaisesRegex(ValueError, "no supported settings"): self.add(report, index=0)
        records = list(self.client.capture.records)
        records[1] = records[1] | {"value": 96}
        self.client.capture = replace(self.client.capture, records=tuple(records))
        report = self.detect()
        self.assertEqual(report["devices"][1]["identity"]["status"], "unknown")
        self.assertEqual(report["devices"][1]["recommendation"]["settings"], {})
        with self.assertRaises(ValueError): self.add(report)
        self.client.capture = replace(self.client.capture, records=tuple(records[:-1] + [records[1] | {"value": 80}]))
        self.client.caps = self.flow.caps = replace(self.flow.caps, drivers=("hc_sr04",))
        report = self.detect()
        self.assertEqual(report["devices"][1]["capabilities"]["support"], "unavailable")
        with self.assertRaises(ValueError): self.add(report)
        self.assertEqual(self.client.calls, [])

    def test_busy_fault_incomplete_and_stale_have_no_add_token_or_mutations(self):
        for result in ("busy", "fault", "incomplete", "stale", "invalid"):
            with self.subTest(result=result):
                self.client.capture = replace(self.client.capture, result=result)
                report = self.detect()
                self.assertIsNone(report["discovery_id"])
                self.assertTrue(all(not d["recommendation"]["settings"] for d in report["devices"]))
                with self.assertRaises(ValueError): self.add(report)
        self.assertEqual(self.client.calls, [])

    def test_draft_wiring_token_and_endpoint_changes_consume_detection(self):
        for changes in ({"profile": self.body["profile"] + " "}, {"wiring_confirmed": False},
                        {"preset_id": "other"}, {"discovery_id": "old"}, {"index": -1}, {"index": True}):
            report = self.detect()
            with self.assertRaises(ValueError): self.add(report, **changes)
            with self.assertRaises(ValueError): self.add(report)

    def test_expiry_revision_capabilities_restart_and_bad_snapshot_reject_add(self):
        def revision(): self.client.current = replace(self.client.current, active_revision=9)
        def capabilities(): self.client.caps = replace(self.client.caps, drivers=("hc_sr04",))
        def expire(): self.now = 60
        def restart(): self.client.timestamp = 10
        def failed_snapshot(): self.client.snapshot_result = 1
        for change in (revision, capabilities, expire, restart, failed_snapshot):
            with self.subTest(change=change.__name__):
                self.client.current = replace(self.client.current, active_revision=0)
                self.client.caps = self.flow.caps
                self.now = 0; self.client.timestamp = 1010; self.client.snapshot_result = 0
                report = self.detect(); change()
                with self.assertRaises(ValueError): self.add(report)
        self.assertEqual(self.client.calls, [])

    def test_poll_invalidates_observation_and_reconnect_loses_tokens(self):
        report = self.detect()
        self.assertEqual(self.flow.dispatch("telemetry", {})["discovery_id"], report["discovery_id"])
        self.now = 60
        self.assertIsNone(self.flow.dispatch("telemetry", {})["discovery_id"])
        self.now = 0; report = self.detect()
        self.flow.dispatch("disconnect", {})
        self.flow.dispatch("connect", {"port": "fake"})
        with self.assertRaises(ValueError): self.add(report)

    def test_probe_timeout_closes_session_without_retry_or_mutation(self):
        self.client.fail = True
        with self.assertRaises(TransferError): self.detect()
        self.assertIsNone(self.flow.client)
        self.assertTrue(self.client.stream.closed)
        self.assertEqual(self.client.probes, 1)
        self.assertEqual(self.client.calls, [])

    def test_review_and_new_detection_invalidate_old_evidence(self):
        report = self.detect(); self.detect()
        with self.assertRaises(ValueError): self.add(report)
        report = self.detect()
        self.flow.dispatch("review", {"profile": self.body["profile"]})
        with self.assertRaises(ValueError): self.add(report)
        self.assertIsNone(self.flow.pending)

    def test_existing_bus_reused_but_endpoint_or_conflicting_bus_not_overwritten(self):
        draft = self.add(self.detect())["profile"]
        original = deepcopy(draft)
        self.body["profile"] = json.dumps(draft)
        with self.assertRaisesRegex(ValueError, "already in the draft"): self.add(self.detect())
        draft["devices"].pop(); draft["queues"].pop()
        draft["interfaces"][0]["id"] = "existing_bus"
        self.body["profile"] = json.dumps(draft)
        added = self.add(self.detect())["profile"]
        self.assertEqual(len(added["interfaces"]), 1)
        self.assertEqual(added["devices"][-1]["interface"]["ref"], "existing_bus")
        draft["interfaces"][0]["clock_hz"] = 400000
        self.body["profile"] = json.dumps(draft)
        with self.assertRaisesRegex(ValueError, "different I2C pins or speed"): self.add(self.detect())
        self.assertEqual(original["devices"][0], self.document["devices"][0])
        self.assertEqual(self.client.calls, [])

    def test_whole_draft_admission_rejects_pin_capacity_and_id_conflicts(self):
        for change in ("pin", "capacity", "id", "invalid_json"):
            with self.subTest(change=change):
                draft = deepcopy(self.document)
                if change == "pin": draft["devices"][0]["interface"]["trigger_gpio"] = 0
                if change == "capacity": draft["runtime"]["max_device_tasks"] = 1
                if change == "id": draft["devices"][0]["device_id"] = "detected_i2c0_77"
                self.body["profile"] = "{" if change == "invalid_json" else json.dumps(draft)
                with self.assertRaises(ValueError): self.add(self.detect())
        self.assertEqual(self.client.calls, [])

    def test_other_protocol_interfaces_remain_unchanged(self):
        spi = dict(id="display_bus", protocol="spi", host=2, mosi_gpio=6, sclk_gpio=7, clock_hz=4000000)
        self.document["interfaces"].append(spi)
        self.body["profile"] = json.dumps(self.document)
        draft = self.add(self.detect())["profile"]
        self.assertEqual(draft["interfaces"][0], spi)
        self.assertEqual(len(draft["interfaces"]), 2)
        self.assertEqual(self.client.calls, [])

    def test_recovery_requires_reported_recovery_state(self):
        with self.assertRaisesRegex(ValueError, "only available"): self.flow.dispatch("recover", {})
        self.assertEqual(self.client.calls, [])
        self.client.current = replace(self.client.current, flags=48)
        self.flow.dispatch("recover", {})
        self.assertEqual(len(self.client.calls), 1)


if __name__ == "__main__":
    unittest.main()
