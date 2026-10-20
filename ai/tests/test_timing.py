"""Timing preparation uses platform admission, not the legacy four-device model."""
from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
import io
import json
from pathlib import Path
import tempfile
import unittest

from jsonschema import Draft202012Validator

from ai.timing import SCHEMA, export_profile, main, prepare, validate
from tools.config_validator import Catalogue, ConfigurationError, build_plan, load_document


ROOT = Path(__file__).resolve().parents[2]


class TimingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalogue = Catalogue(ROOT)
        Draft202012Validator.check_schema(load_document(SCHEMA))

    def setUp(self):
        self.profile = load_document(ROOT / "configs/nodes/two-environment-sensors.json")
        self.manifest = load_document(ROOT / "configs/builds/host-reference.json")
        self.scenario = load_document(ROOT / "ai/examples/timing-scenario.json")

    def plan(self):
        return prepare(self.profile, self.manifest, self.scenario, self.catalogue)

    def test_reproducible_plan_preserves_inputs_and_has_no_measured_claims(self):
        before = deepcopy((self.profile, self.manifest, self.scenario))
        experiment = self.plan()
        self.assertEqual(experiment, self.plan())
        self.assertEqual(before, (self.profile, self.manifest, self.scenario))
        self.assertEqual(experiment["measurement_origin"], "none")
        self.assertEqual(experiment["identity_evidence"], "not_collected")
        self.assertIsNone(experiment["actual_firmware_sha256"])
        self.assertIsNone(experiment["training_label"])
        self.assertEqual([row["interval_ms"] for row in experiment["trials"]], [200, 500, 1000, 2000])
        self.assertEqual(experiment["trials"][1]["roles"], ["current_profile", "candidate"])
        self.assertEqual(experiment["trials"][-1]["roles"], ["descriptor_default"])
        self.assertEqual(experiment, validate(experiment, self.catalogue))
        experiment["profile"]["devices"][1]["enabled"] = False
        self.assertTrue(self.profile["devices"][1]["enabled"])

    def test_export_changes_only_named_instance_period_and_passes_admission(self):
        experiment = self.plan()
        candidate = export_profile(experiment, 1000, self.catalogue)
        build_plan(candidate, self.manifest, self.catalogue)
        expected = deepcopy(self.profile)
        expected["devices"][0]["behaviour"]["interval_ms"] = 1000
        self.assertEqual(candidate, expected)
        self.assertEqual(experiment["profile"], self.profile)
        for invalid in (400, True, 1000.0):
            with self.subTest(interval=invalid), self.assertRaises(ConfigurationError):
                export_profile(experiment, invalid, self.catalogue)

    def test_same_driver_instances_can_have_separate_experiments(self):
        self.scenario.update(instance_id="room_2", duration_ms=60000)
        experiment = self.plan()
        candidate = export_profile(experiment, 200, self.catalogue)
        self.assertEqual(candidate["devices"][0], self.profile["devices"][0])
        self.assertEqual(candidate["devices"][1]["behaviour"]["interval_ms"], 200)
        self.assertIn(5000, [row["interval_ms"] for row in experiment["trials"]])

    def test_capabilities_are_resolved_without_a_fixed_device_enum(self):
        catalogue = deepcopy(self.catalogue)
        descriptor = deepcopy(catalogue.descriptors[("bmp388", "1.0.0")])
        descriptor.update(descriptor_id="test_reader", driver_id="test_reader")
        properties = descriptor["instance_schema"]["properties"]
        properties["driver"]["const"] = "test_reader"
        properties["descriptor"]["properties"]["id"]["const"] = "test_reader"
        catalogue.descriptors[("test_reader", "1.0.0")] = descriptor
        self.profile["devices"][0]["driver"] = "test_reader"
        self.profile["devices"][0]["descriptor"]["id"] = "test_reader"
        self.manifest["drivers"].append("test_reader")
        experiment = prepare(self.profile, self.manifest, self.scenario, catalogue)
        self.assertEqual(experiment["target"]["descriptor"]["id"], "test_reader")

    def test_display_uses_refresh_field_and_requires_a_visible_delay_goal(self):
        self.profile = load_document(ROOT / "configs/nodes/first-prototype.json")
        self.scenario.update(instance_id="oled_status", candidate_intervals_ms=[100, 500])
        with self.assertRaisesRegex(ConfigurationError, "Goal metric"):
            self.plan()
        self.scenario["goal"]["metric"] = "visible_update_delay_ms"
        experiment = self.plan()
        self.assertEqual(experiment["target"]["period_field"], "refresh_interval_ms")
        candidate = export_profile(experiment, 500, self.catalogue)
        expected = deepcopy(self.profile)
        expected["devices"][1]["behaviour"]["refresh_interval_ms"] = 500
        self.assertEqual(candidate, expected)

    def test_stream_and_actuator_tuning_are_out_of_scope(self):
        self.profile = load_document(ROOT / "configs/nodes/first-prototype.json")
        self.scenario["instance_id"] = "presence_sensor"
        with self.assertRaisesRegex(ConfigurationError, "Stream"):
            self.plan()
        self.profile = load_document(ROOT / "configs/nodes/distance-controlled-output.json")
        self.scenario["instance_id"] = "output.main"
        with self.assertRaisesRegex(ConfigurationError, "actuator tuning"):
            self.plan()

    def test_rule_outputs_must_be_explicitly_disabled_even_when_tuning_a_sensor(self):
        self.profile = load_document(ROOT / "configs/nodes/distance-controlled-output.json")
        self.scenario["instance_id"] = "front_range"
        with self.assertRaisesRegex(ConfigurationError, "outputs to be disabled"):
            self.plan()
        self.profile["devices"][1]["enabled"] = False
        experiment = self.plan()
        candidate = export_profile(experiment, 500, self.catalogue)
        self.assertEqual(candidate["rules"], self.profile["rules"])
        self.assertFalse(candidate["devices"][1]["enabled"])

    def test_unknown_and_disabled_instances_cannot_be_tuned(self):
        self.scenario["instance_id"] = "absent"
        with self.assertRaisesRegex(ConfigurationError, "Unknown target"):
            self.plan()
        self.scenario["instance_id"] = "room_1"
        self.profile["devices"][0]["enabled"] = False
        with self.assertRaisesRegex(ConfigurationError, "already be enabled"):
            self.plan()

    def test_descriptor_interval_limits_and_exact_integer_tokens_are_enforced(self):
        for interval in (199, 60001, 500.0, True, "500"):
            self.scenario["candidate_intervals_ms"] = [interval]
            self.scenario["duration_ms"] = 600000
            with self.subTest(interval=interval), self.assertRaises(ConfigurationError):
                self.plan()
        self.scenario["candidate_intervals_ms"] = [500]
        self.profile["devices"][0]["behaviour"]["timeout_ms"] = 20.0
        with self.assertRaisesRegex(ConfigurationError, "integer JSON"):
            self.plan()
        self.profile["devices"][0]["behaviour"]["timeout_ms"] = 20
        self.manifest["extra_numeric_metadata"] = 1.5
        with self.assertRaisesRegex(ConfigurationError, "integer JSON"):
            self.plan()

    def test_shared_admission_rejects_unavailable_drivers_and_unrelated_pin_conflicts(self):
        self.manifest["drivers"] = []
        with self.assertRaisesRegex(ConfigurationError, "Driver unavailable"):
            self.plan()
        self.manifest = load_document(ROOT / "configs/builds/host-reference.json")
        self.profile["interfaces"][0]["scl_gpio"] = self.profile["interfaces"][0]["sda_gpio"]
        with self.assertRaises(ConfigurationError):
            self.plan()

    def test_invalid_scenarios_and_uninformative_experiments_are_rejected(self):
        cases = [("contract_version", "9.0.0"), ("candidate_intervals_ms", []),
                 ("candidate_intervals_ms", [500, 500]), ("repetitions", 1),
                 ("duration_ms", 1000), ("warmup_ms", -1), ("family_id", ""),
                 ("conditions", "          "), ("unknown", 1)]
        original = deepcopy(self.scenario)
        for key, value in cases:
            self.scenario = {**original, key: value}
            with self.subTest(key=key), self.assertRaises(ConfigurationError):
                self.plan()
        self.scenario = {**original, "candidate_intervals_ms": [2000]}
        self.profile["devices"][0]["behaviour"]["interval_ms"] = 2000
        with self.assertRaisesRegex(ConfigurationError, "two distinct"):
            self.plan()

    def test_plan_cannot_be_relabelled_as_measurements_or_modified_silently(self):
        experiment = self.plan()
        for key, value in [("stage", "measured"), ("measurement_origin", "hardware"),
                           ("identity_evidence", "verified"), ("training_label", 1000),
                           ("actual_firmware_sha256", "a" * 64), ("extra", [])]:
            changed = {**experiment, key: value}
            with self.subTest(key=key), self.assertRaises(ConfigurationError):
                validate(changed, self.catalogue)
        changed = deepcopy(experiment)
        changed["trials"][0]["profile_sha256"] = "a" * 64
        with self.assertRaises(ConfigurationError):
            validate(changed, self.catalogue)
        changed = deepcopy(experiment)
        changed["build_manifest"]["build_id"] = "different-build"
        with self.assertRaises(ConfigurationError):
            validate(changed, self.catalogue)

    def test_catalogue_drift_including_other_instances_invalidates_saved_plan(self):
        self.profile = load_document(ROOT / "configs/nodes/first-prototype.json")
        self.scenario["instance_id"] = "environment_sensor"
        experiment = self.plan()
        for kind in ("descriptor", "board", "schema"):
            catalogue = deepcopy(self.catalogue)
            if kind == "descriptor":
                catalogue.descriptors[("hc_sr04", "1.0.0")]["defaults"]["priority"] = 13
            elif kind == "board":
                next(iter(catalogue.boards.values()))["status"] = "changed"
            else:
                catalogue.node_schema["description"] = "changed"
            with self.subTest(kind=kind), self.assertRaisesRegex(ConfigurationError, "reviewed sources"):
                validate(experiment, catalogue)

    def test_incomplete_and_legacy_artifacts_are_rejected(self):
        for artifact in ({}, {"contract_version": "0.2.0"},
                         {"contract": "uthome.timing-experiment", "contract_version": "0.1.0"}):
            with self.subTest(artifact=artifact), self.assertRaises(ConfigurationError):
                validate(artifact, self.catalogue)

    def test_cli_round_trip_preserves_existing_files_and_bounds_inputs(self):
        with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            output = Path(folder) / "experiment.json"
            draft = Path(folder) / "candidate.json"
            args = ["prepare", "--profile", str(ROOT / "configs/nodes/two-environment-sensors.json"),
                    "--build-manifest", str(ROOT / "configs/builds/host-reference.json"),
                    "--scenario", str(ROOT / "ai/examples/timing-scenario.json"), "--output", str(output)]
            self.assertEqual(main(args), 0)
            original = output.read_bytes()
            self.assertEqual(main(args), 1)
            self.assertEqual(output.read_bytes(), original)
            self.assertEqual(main(["validate", str(output)]), 0)
            self.assertEqual(main(["export", str(output), "--interval-ms", "1000", "--output", str(draft)]), 0)
            self.assertLessEqual(draft.stat().st_size, 8192)
            candidate = json.loads(draft.read_text(encoding="ascii"))
            build_plan(candidate, self.manifest, self.catalogue)
            self.assertEqual(main(["export", str(output), "--interval-ms", "300", "--output", str(draft)]), 1)
            self.assertEqual(main(["export", str(output), "--interval-ms", "500", "--output", str(output)]), 1)
            self.assertEqual(output.read_bytes(), original)
            bad = Path(folder) / "bad.json"
            bad.write_bytes(b'{"contract_version":"0.1.0","contract_version":"0.2.0"}')
            args[args.index("--scenario") + 1] = str(bad)
            self.assertEqual(main(args), 1)
            bad.write_bytes(b" " * 8193)
            self.assertEqual(main(args), 1)


if __name__ == "__main__":
    unittest.main()
