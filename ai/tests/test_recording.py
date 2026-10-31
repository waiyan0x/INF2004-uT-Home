"""Recording contracts exercised with fake telemetry and clocks, never hardware."""
from copy import deepcopy
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch
import zlib

from ai.provenance import (check_receipt, make_identity, make_receipt, retained_build)
from ai.recorder import apply_trial, bundle_recordings, main
from ai.recording import load_recording, record_run
from ai.records import assess_run, build_dataset
from ai.timing import canonical, digest, export_profile, prepare
from tools.config_transfer.capabilities import Capabilities
from tools.config_transfer.discovery import Discovery
from tools.config_transfer.telemetry import Snapshot
from tools.config_validator.planner import ConfigurationError, ROOT, load_document


class Clock:
    def __init__(self):
        self.now = 1.0

    def time(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class Board:
    """Only read methods exist: accidental profile mutations fail these tests."""
    def __init__(self, clock):
        self.clock, self.calls, self.discovery_calls = clock, 0, 0
        self.idle = False
        self.mutate = lambda data, count: None
        self.failure_at = None
        self.delay_at = None
        self.bad_build = False
        self.config = dict(active_revision=1, saved_revision=1, active_crc=42, saved_crc=42, flags=80)
        self.caps = Capabilities(1, False, 8192, 0x4FB, 8, 4, 7, "esp32-c3-supermini", "2.1.0",
                                ("bmp388", "ssd1306_spi", "hlk_ld2450", "hc_sr04", "pwm_output"))

    def telemetry(self):
        self.calls += 1
        if self.calls == self.failure_at:
            raise OSError("test_disconnect")
        self.clock.now += 0.8 if self.calls == self.delay_at else 0.017
        now = 100000 + round(self.clock.now * 1000)
        data = dict(request_id=self.calls, result=0, timestamp_ms=now, configuration=deepcopy(self.config),
                    runtime=dict(state=0 if self.idle else 1, generation=3, retained_instances=0 if self.idle else 1,
                                 failed_workers=0, service_error=0, apply_error=0, recovery_error=0),
                    health=dict(event_counts=[0, 0, 0, 0, self.calls, 0, 0, 0]), queues=(),
                    samples=[dict(name="room_1", source_index=0, queue=0, type=1, quality="fresh", age_ms=80)])
        self.mutate(data, self.calls)
        return Snapshot(**data)

    def discover_i2c(self, *, wiring_confirmed):
        assert wiring_confirmed
        self.discovery_calls += 1
        records = (dict(address=118, register=0, status="responding", value=80),
                   dict(address=119, register=0, status="not_responding", value=None)) if self.idle else ()
        return Discovery(1, self.config["active_revision"], self.config["active_revision"],
                         "complete" if self.idle else "busy", 1, 123, 1000 if self.idle else 0,
                         1 if self.idle else 0, ("f" if self.bad_build else "a") * 64, records), self.caps


class RecordingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="uthome-recorder-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.clock = Clock()
        self.board = Board(self.clock)
        self.board.idle = True
        self.identity = make_identity(self.board, dict(vid=0x303A, pid=0x1001, serial_number="TEST-ONLY"),
                                      dict(elf_sha256="a" * 64, binary_sha256="b" * 64, evidence_sha256="c" * 64))
        self.board.idle = False
        profile = load_document(ROOT / "configs/nodes/two-environment-sensors.json")
        profile["devices"] = profile["devices"][:1]
        profile["queues"] = profile["queues"][:1]
        profile["interfaces"][0]["clock_hz"] = 100000
        scenario = dict(contract_version="0.1.0", scenario_id="recorder-fixture", family_id="fixture-only",
                        instance_id="room_1", conditions="Fake transport fixture, not a measured bench scenario.",
                        goal=dict(metric="observed_sample_age_ms", limit_ms=800),
                        candidate_intervals_ms=[200, 500, 1000], warmup_ms=100, duration_ms=20000, repetitions=2)
        self.experiment = prepare(profile, self.identity["manifest"], scenario)
        self.receipt = self.trial_receipt(500)
        self.board.calls = self.board.discovery_calls = 0

    def trial_receipt(self, interval):
        profile = export_profile(self.experiment, interval)
        raw = json.dumps(profile, ensure_ascii=True, separators=(",", ":")).encode("ascii")
        crc = zlib.crc32(raw)
        reply = dict(command=1, request_id=2, revision=3, status=1, active_revision=3, saved_revision=3,
                     active_crc=crc, saved_crc=crc, flags=95)
        self.board.config = {k: reply[k] for k in ("active_revision", "saved_revision", "active_crc", "saved_crc")}
        self.board.config["flags"] = 80
        return make_receipt(self.experiment, interval, self.identity, dict(sha256=hashlib.sha256(raw).hexdigest()), reply)

    def record(self, name="run", *, interval=500, repetition=0, receipt=None):
        folder = self.root / name
        assessment = record_run(self.board, self.experiment, interval, repetition, self.identity,
                                receipt or self.receipt, folder, clock=self.clock.time, sleep=self.clock.sleep)
        return folder, assessment

    def test_clean_capture_has_actual_overshoot_and_ignores_queue_overwrites(self):
        folder, result = self.record()
        self.assertTrue(result["usable"] and result["passes"])
        experiment, run = load_recording(folder)
        self.assertEqual(run["samples"][0]["elapsed_ms"], 0)
        end = experiment["scenario"]["duration_ms"] + experiment["scenario"]["warmup_ms"]
        self.assertGreater(run["samples"][-1]["elapsed_ms"], end)
        self.assertLessEqual(run["samples"][-1]["elapsed_ms"], end + 400)
        self.assertEqual(result["health_faults"], 0)
        self.assertEqual(self.board.discovery_calls, 2)

    def test_stale_fault_missing_and_goal_failure_remain_negative(self):
        for quality in ("stale", "fault", "missing", "fresh"):
            def mutate(data, count):
                if count >= 4:
                    data["samples"][0].update(quality=quality, age_ms=None if quality == "missing" else 900)
                    data["health"]["event_counts"][2] = 1
            self.board.mutate = mutate
            self.board.calls = 0
            folder, result = self.record(quality)
            self.assertTrue(result["usable"])
            self.assertFalse(result["passes"])
            load_recording(folder)

    def test_all_fault_event_codes_are_counted(self):
        for event in (0, 1, 2, 3, 5, 6, 7):
            self.board.calls = 0
            def mutate(data, count):
                if count > 4:
                    data["health"]["event_counts"][event] = 1
            self.board.mutate = mutate
            _, result = self.record("event-" + str(event))
            self.assertTrue(result["usable"])
            self.assertFalse(result["passes"])
            self.assertEqual(result["health_faults"], 1)

    def test_runtime_failure_does_not_disappear_as_a_dropped_scenario(self):
        self.board.mutate = lambda d, n: d["runtime"].update(failed_workers=int(n > 4))
        folder, result = self.record()
        self.assertTrue(result["usable"])
        self.assertFalse(result["passes"])
        load_recording(folder)

    def test_disconnect_and_large_gap_are_preserved_but_unusable(self):
        for field, reason in (("failure_at", "test_disconnect"), ("delay_at", "sampling_gap")):
            self.board.calls = 0
            setattr(self.board, field, 6)
            folder, result = self.record(field)
            self.assertFalse(result["usable"])
            self.assertIn(reason, result["reason"])
            self.assertTrue((folder / "raw.jsonl").exists())
            load_recording(folder)
            setattr(self.board, field, None)

    def test_reset_profile_generation_and_counter_changes_are_unusable(self):
        variants = [lambda d: d.update(timestamp_ms=1),
                    lambda d: d["configuration"].update(active_crc=123),
                    lambda d: d["runtime"].update(generation=4),
                    lambda d: d["health"]["event_counts"].__setitem__(4, 0),
                    lambda d: d["samples"][0].update(source_index=1)]
        for index, change in enumerate(variants):
            self.board.calls = 0
            self.board.mutate = lambda d, n: change(d) if n == 6 else None
            folder, result = self.record("changed-" + str(index))
            self.assertFalse(result["usable"])
            load_recording(folder)

    def test_bad_build_and_inactive_trial_never_produce_a_labelled_run(self):
        self.board.bad_build = True
        folder, result = self.record("wrong-build")
        self.assertFalse(result["usable"])
        self.assertFalse((folder / "run.json").exists())
        self.assertTrue((folder / "failure.json").exists())
        self.board.bad_build = False
        self.board.config["active_crc"] = 123
        folder, result = self.record("wrong-profile")
        self.assertFalse((folder / "run.json").exists())

    def test_early_disconnects_retain_zero_or_one_observation(self):
        for call, count in ((2, 0), (3, 1)):
            self.board.calls = 0
            self.board.failure_at = call
            folder, result = self.record("early-" + str(call))
            self.assertFalse(result["usable"])
            _, run = load_recording(folder)
            self.assertEqual(len(run["samples"]), count)

    def test_profile_race_during_build_checks_is_retained(self):
        original = self.board.discover_i2c
        for phase in (1, 2):
            self.board.calls = self.board.discovery_calls = 0
            self.receipt = self.trial_receipt(500)
            def raced(**kwargs):
                if self.board.discovery_calls + 1 == phase:
                    self.board.config["active_revision"] += 1
                return original(**kwargs)
            self.board.discover_i2c = raced
            folder, result = self.record("race-" + str(phase))
            self.assertFalse(result["usable"])
            self.assertIn("configuration_changed", result["reason"])
            if phase == 1:
                self.assertTrue((folder / "failure.json").exists())
                self.assertFalse((folder / "run.json").exists())
            else:
                load_recording(folder)

    def test_postflight_change_cannot_make_a_complete_run(self):
        def mutate(data, count):
            if self.board.discovery_calls == 2:
                data["configuration"]["active_crc"] = 42
        self.board.mutate = mutate
        folder, result = self.record()
        self.assertFalse(result["usable"])
        self.assertEqual(result["reason"], "configuration_changed")
        load_recording(folder)

    def test_cli_existing_output_is_untouched_and_rejected_apply_is_retained(self):
        existing = self.root / "existing"
        existing.mkdir()
        with patch("sys.stderr"):
            self.assertEqual(main(["bundle", str(existing), "--output", str(existing)]), 1)
        self.assertEqual(list(existing.iterdir()), [])
        folder = self.root / "rejected"
        folder.mkdir()
        raw = json.dumps(export_profile(self.experiment, 500), ensure_ascii=True, separators=(",", ":")).encode()
        workflow = Mock()
        workflow.client = self.board
        reply = deepcopy(self.receipt["reply"])
        reply["status"] = 0
        workflow.dispatch.side_effect = [dict(sha256=hashlib.sha256(raw).hexdigest(), review_id="test"), reply]
        with self.assertRaisesRegex(ConfigurationError, "not acknowledged"):
            apply_trial(workflow, self.experiment, 500, self.identity, folder)
        self.assertTrue((folder / "request.json").exists())
        self.assertEqual(json.loads((folder / "reply.json").read_text()), reply)
        self.assertFalse((folder / "receipt.json").exists())

    def test_receipt_identity_and_record_tampering_are_rejected(self):
        for field, value in (("active_crc", 42), ("status", 0), ("saved_revision", 1)):
            receipt = deepcopy(self.receipt)
            receipt["reply"][field] = value
            with self.assertRaises(ConfigurationError):
                check_receipt(self.experiment, 500, self.identity, receipt)
        identity = deepcopy(self.identity)
        identity["discovery"]["build_sha256"] = "d" * 64
        with self.assertRaises(ConfigurationError):
            check_receipt(self.experiment, 500, identity, self.receipt)
        folder, _ = self.record()
        run_path = folder / "run.json"
        run = json.loads(run_path.read_text())
        run["samples"][2]["metric_ms"] = 1
        run_path.write_text(json.dumps(run))
        with self.assertRaisesRegex(ConfigurationError, "differs from raw"):
            load_recording(folder)

    def test_bundle_requires_complete_repetitions_and_unique_trials(self):
        paths = []
        for trial in self.experiment["trials"]:
            interval = trial["interval_ms"]
            receipt = self.trial_receipt(interval)
            for rep in range(2):
                folder, _ = self.record(f"trial-{interval}-{rep}", interval=interval, repetition=rep, receipt=receipt)
                paths.append(folder)
        data = build_dataset(bundle_recordings(paths))
        self.assertEqual(len(data["rows"]), len(self.experiment["trials"]))
        self.assertTrue(all(row["label"] == 1 for row in data["rows"]))
        self.assertFalse(build_dataset(bundle_recordings(paths[:-1]))["rows"])
        with self.assertRaisesRegex(ConfigurationError, "Duplicate"):
            bundle_recordings(paths + paths[:1])

    def test_output_is_never_overwritten_and_raw_tampering_is_detected(self):
        folder, _ = self.record()
        original = (folder / "run.json").read_bytes()
        with self.assertRaises(FileExistsError):
            self.record()
        self.assertEqual(original, (folder / "run.json").read_bytes())
        with (folder / "raw.jsonl").open("ab") as output:
            output.write(b"\n")
        with self.assertRaisesRegex(ConfigurationError, "hash mismatch"):
            load_recording(folder)

    def test_identification_refuses_active_board_and_artifact_hash_mismatch(self):
        with self.assertRaisesRegex(ConfigurationError, "empty profile"):
            make_identity(self.board, self.identity["usb"], self.identity["build"])
        folder = self.root / "build-fixture"
        (folder / "build").mkdir(parents=True)
        artifacts = {}
        for name in ("uthome_runtime_port.elf", "uthome_runtime_port.bin"):
            raw = ("fixture-" + name).encode()
            (folder / "build" / name).write_bytes(raw)
            artifacts["build/" + name] = dict(sha256=hashlib.sha256(raw).hexdigest())
        (folder / "evidence.json").write_text(json.dumps(dict(build_status="passed", target="esp32c3",
            configuration_application=True, artifacts=artifacts)))
        self.assertEqual(len(retained_build(folder)["elf_sha256"]), 64)
        (folder / "build/uthome_runtime_port.bin").write_bytes(b"changed")
        with self.assertRaisesRegex(ConfigurationError, "hash mismatch"):
            retained_build(folder)


if __name__ == "__main__":
    unittest.main()
