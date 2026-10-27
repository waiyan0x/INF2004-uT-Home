"""Protocol features, frozen groups, auxiliary observations and host-only model."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch
import zlib

from ai import workload
from ai.export import export_c
from ai.model import context
from ai.protocol_model import predict, recommend, train, validate_model
from ai.protocol_capture import collect
from ai.protocol_study import dataset, observe_peers, prepare_study, validate_study
from ai.provenance import make_identity, make_receipt
from ai.recording import load_recording, record_run
from ai.tests.test_recording import Board, Clock
from ai.timing import canonical, digest, export_profile, prepare
from tools.config_validator.planner import Catalogue, ConfigurationError, ROOT, build_plan, load_document


class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.profile = load_document(ROOT / "configs/nodes/first-prototype.json")
        cls.profile["devices"] = cls.profile["devices"][:3]
        cls.profile["devices"][1]["interface"]["cs_gpio"] = 4
        cls.profile["queues"] = [q for q in cls.profile["queues"] if q["id"] != "distance.front"]
        cls.profile["devices"][1]["behaviour"]["sources"].remove("distance.front")
        cls.profile["devices"][0]["behaviour"].update(interval_ms=500, timeout_ms=100)
        cls.spec = load_document(ROOT / "ai/examples/protocol-study.json")
        cls.spec.update(warmup_ms=0, duration_ms=20000, cadence_ms=1000)
        clock = Clock()
        board = Board(clock)
        board.idle = True
        cls.identity = make_identity(board, dict(vid=0x303a, pid=0x1001, serial_number="TEST-ONLY"),
                                     dict(elf_sha256="a"*64, binary_sha256="b"*64, evidence_sha256="c"*64))
        cls.study = prepare_study(cls.profile, cls.identity["manifest"], cls.spec)

    def experiment(self, family="all-current"):
        return deepcopy(next(c["experiment"] for c in self.study["cases"] if c["experiment"]["scenario"]["family_id"] == family))

    def test_features_describe_protocol_work_not_baud_as_continuous_traffic(self):
        f = dict(zip(workload.FEATURES, workload.features(self.experiment(), 500)))
        self.assertEqual(f["i2c_requests_millihz"], 2000)
        self.assertEqual(f["spi_refresh_millihz"], 4000)
        self.assertEqual(f["uart_declared_bytes_per_second"], 300)
        self.assertEqual(f["uart_min_baud"], 256000)
        self.assertEqual(f["higher_priority_workers"], 1)
        self.assertEqual(f["uart_min_rx_buffer_bytes"], 512)
        self.assertNotIn("device_id", f)

    def test_display_speed_and_refresh_change_features(self):
        a = workload.features(self.experiment("all-current"), 500)
        b = workload.features(self.experiment("all-fast"), 500)
        self.assertNotEqual(a, b)
        self.assertEqual(a[:6], b[:6])  # The old six inputs could not distinguish these loads.

    def test_family_group_ignores_goal_current_period_and_cosmetic_names(self):
        a = self.experiment()
        b = deepcopy(a)
        b["scenario"]["goal"]["limit_ms"] = 1234
        b["profile"]["node_id"] = "another-name"
        b["profile"]["devices"][0]["behaviour"]["interval_ms"] = 200
        b["profile"]["devices"][0]["device_id"] = b["target"]["instance_id"] = "renamed"
        self.assertEqual(workload.family_key(a), workload.family_key(b))
        self.assertNotEqual(workload.family_key(a), workload.family_key(self.experiment("all-fast")))

    def test_duplicate_workloads_and_changed_split_are_rejected(self):
        spec = deepcopy(self.spec)
        spec["families"][1] = dict(spec["families"][0], id="same-load")
        with self.assertRaisesRegex(ConfigurationError, "Duplicate workload"):
            prepare_study(self.profile, self.identity["manifest"], spec)
        study = deepcopy(self.study)
        study["split"]["test"].append(study["split"]["train"].pop())
        with self.assertRaises(ConfigurationError):
            validate_study(study)
        groups = list(self.study["split"].values())
        self.assertTrue(all(not set(a) & set(b) for i,a in enumerate(groups) for b in groups[i+1:]))

    def test_unknown_workload_preserves_normal_configuration_admission(self):
        profile = load_document(ROOT / "configs/nodes/distance-only.json")
        scenario = self.experiment()["scenario"]
        scenario["instance_id"] = profile["devices"][0]["device_id"]
        experiment = prepare(profile, self.identity["manifest"], scenario)
        build_plan(profile, self.identity["manifest"])
        result = workload.coverage(experiment, 500)
        self.assertEqual(result["status"], "abstain")
        self.assertEqual(result["fallback"]["action"], "keep_current_profile")
        self.assertFalse(result["fallback"]["goal_guaranteed"])
        self.assertEqual(recommend(None, experiment)["status"], "abstain")

    def record(self, root, interval, rep, *, radar_quality="fresh", consume=True):
        experiment = self.experiment()
        clock, board = Clock(), None
        board = Board(clock)
        raw = json.dumps(export_profile(experiment, interval), ensure_ascii=True, separators=(",", ":")).encode()
        crc = zlib.crc32(raw)
        reply = dict(command=1, request_id=1, revision=3, status=1, active_revision=3, saved_revision=3,
                     active_crc=crc, saved_crc=crc, flags=95)
        board.config = {k: reply[k] for k in ("active_revision", "saved_revision", "active_crc", "saved_crc")}
        board.config["flags"] = 80
        receipt = make_receipt(experiment, interval, self.identity, dict(sha256=hashlib.sha256(raw).hexdigest()), reply)
        def mutate(data, count):
            data["samples"][0].update(name="environment_sensor", age_ms=interval-1)
            data["samples"].append(dict(name="presence_sensor", source_index=2, queue=1, type=2,
                                        quality=radar_quality, age_ms=20, value=dict(targets=[])))
            data["queues"] = tuple(dict(index=i, consumed=count if consume else 0) for i in range(3))
        board.mutate = mutate
        folder = root / f"run-{interval}-{rep}"
        result = record_run(board, experiment, interval, rep, self.identity, receipt, folder,
                            cadence_ms=1000, clock=clock.time, sleep=clock.sleep)
        return folder, result

    def test_peer_fault_is_negative_but_zero_fifo_consumption_is_normal_for_display(self):
        for quality, consume in (("fresh", True), ("fault", True), ("fresh", False)):
            with tempfile.TemporaryDirectory() as root:
                folder, target = self.record(Path(root), 500, 0, radar_quality=quality, consume=consume)
                experiment, run = load_recording(folder)
                self.assertTrue(target["passes"])
                peers = observe_peers(folder, experiment, run)
                self.assertEqual(peers["passes"], quality == "fresh")
                self.assertFalse(peers["consumers"][0]["completion_measured"])
                self.assertFalse(peers["consumers"][0]["visible_latency_measured"])

    def test_dataset_replays_real_journals_and_excludes_incomplete_families(self):
        with tempfile.TemporaryDirectory() as root:
            paths = []
            for interval in (200, 500, 1000, 2000):
                for rep in range(2):
                    paths.append(self.record(Path(root), interval, rep)[0])
            data = dataset(self.study, paths)
            self.assertEqual([r["label"] for r in data["rows"]], [1, 1, 0, 0])
            self.assertEqual(len(data["excluded"]), 11)
            with self.assertRaisesRegex(ConfigurationError, "Duplicate"):
                dataset(self.study, paths + paths[:1])
            with self.assertRaisesRegex(ConfigurationError, "Complete every"):
                train(self.study, paths)

    def fixture_dataset(self):
        # Test-only synthetic rows, injected at the verified-dataset boundary;
        # never written to disk or passed off as a measured training artifact.
        rows = []
        for case in self.study["cases"]:
            e = case["experiment"]
            for t in e["trials"]:
                rows.append(dict(family_id=e["scenario"]["family_id"], interval_ms=t["interval_ms"],
                                 features=workload.features(e, t["interval_ms"]), label=int(t["interval_ms"] <= 800)))
        return dict(rows=rows, excluded=[], feature_names=workload.FEATURES)

    def test_training_never_uses_test_labels_and_old_c_export_refuses_new_contract(self):
        data = self.fixture_dataset()
        with patch("ai.protocol_model.dataset", return_value=data):
            model, report, _ = train(self.study, [])
        changed = deepcopy(data)
        for row in changed["rows"]:
            if row["family_id"] in self.study["split"]["test"]:
                row["label"] = 1 - row["label"]
        with patch("ai.protocol_model.dataset", return_value=changed):
            other, _, _ = train(self.study, [])
        self.assertEqual(model["nodes"], other["nodes"])
        self.assertEqual(model["domain"], other["domain"])
        self.assertEqual(report["results"]["test"]["tree"]["violations"], 0)
        self.assertFalse(model["deployment_allowed"])
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaises(ConfigurationError):
                export_c(model, Path(root) / "unsupported")
            self.assertFalse((Path(root) / "unsupported").exists())

    def test_model_abstains_on_context_or_domain_change_and_rejects_cycles(self):
        with patch("ai.protocol_model.dataset", return_value=self.fixture_dataset()):
            model, _, _ = train(self.study, [])
        experiment = self.experiment()
        self.assertEqual(recommend(model, experiment)["interval_ms"], 500)
        profile = deepcopy(experiment["profile"])
        profile["devices"][1]["behaviour"]["timeout_ms"] = 20
        changed = prepare(profile, experiment["build_manifest"], experiment["scenario"])
        self.assertEqual(workload.features(changed, 500), workload.features(experiment, 500))
        self.assertEqual(recommend(model, changed)["status"], "abstain")
        model["contexts"] = ["f" * 64]
        self.assertEqual(recommend(model, experiment)["status"], "abstain")
        vector = workload.features(experiment, 500)
        vector[-1] = 65536
        self.assertEqual(predict(model, vector), -1)
        model["nodes"][0]["left"] = 0
        with self.assertRaises(ConfigurationError):
            validate_model(model)

    def test_collection_restores_after_interruption_and_does_not_retry(self):
        raw = json.dumps(self.profile, ensure_ascii=True, separators=(",", ":")).encode()
        crc = zlib.crc32(raw)
        workflow = Mock()
        workflow.client = Board(Clock())
        def dispatch(action, body):
            if action == "connect":
                return dict(state=dict(active_crc=crc, saved_crc=crc, flags=80))
            if action == "review":
                return dict(review_id="restore")
            if action == "apply":
                return dict(status=1, active_crc=crc, saved_crc=crc)
            raise AssertionError(action)
        workflow.dispatch.side_effect = dispatch
        with tempfile.TemporaryDirectory() as root:
            with patch("ai.protocol_capture.apply_trial", return_value={}) as apply_mock:
                with patch("ai.protocol_capture.record_run", side_effect=OSError("test_disconnect")) as capture_mock:
                    result = collect(workflow, "TEST-ONLY", self.study, self.identity, self.profile, Path(root)/"capture", ["all-current"])
            self.assertEqual(result["error"], "test_disconnect")
            self.assertTrue(result["restored"])
            self.assertEqual(apply_mock.call_count, 1)
            self.assertEqual(capture_mock.call_count, 1)
            workflow.close.assert_called_once()

    def test_changed_baseline_never_applies_a_trial(self):
        workflow = Mock()
        workflow.dispatch.return_value = dict(state=dict(active_crc=0, saved_crc=0, flags=80))
        with tempfile.TemporaryDirectory() as root:
            with patch("ai.protocol_capture.apply_trial") as apply_mock:
                result = collect(workflow, "TEST-ONLY", self.study, self.identity, self.profile, Path(root)/"capture", ["all-current"])
            self.assertIn("does not match", result["error"])
            apply_mock.assert_not_called()
            self.assertEqual(workflow.dispatch.call_count, 1)


if __name__ == "__main__":
    unittest.main()
