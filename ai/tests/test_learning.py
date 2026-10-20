"""Offline run integrity, leakage controls, experimental model and C parity."""
from copy import deepcopy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from ai.export import export_c, verify_c
from ai.model import context, predict, recommend, split_families, train, validate_model
from ai.offline import save, simulate
from ai.records import assess_run, build_dataset, features, validate_dataset
from ai.timing import digest, prepare
from tools.config_validator import Catalogue, ConfigurationError


ROOT = Path(__file__).resolve().parents[2]


def setUpModule():
    subprocess.run([sys.executable, str(ROOT / "tools/c_standard/check.py")], cwd=ROOT, check=True)


class LearningTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalogue = Catalogue()
        cls.bundle = simulate(families=12)
        cls.dataset = build_dataset(cls.bundle, cls.catalogue)
        cls.model, cls.report = train(cls.dataset, cls.bundle, catalogue=cls.catalogue)

    def case(self):
        return deepcopy(self.bundle["cases"][0])

    def test_simulation_is_deterministic_and_never_claims_hardware(self):
        self.assertEqual(digest(simulate(families=12)), digest(self.bundle))
        self.assertEqual(self.dataset["origin"], "simulated")
        self.assertFalse(self.model["deployment_allowed"])
        self.assertIsNone(self.report["hardware_accuracy"])
        self.assertEqual(len(self.dataset["rows"]), 96)
        self.assertEqual(self.dataset["excluded"], [])

    def test_complete_failures_are_negative_examples_not_silently_dropped(self):
        case = self.case()
        run = case["runs"][0]
        run["samples"][-1].update(quality="fault", metric_ms=None, health_faults=1)
        result = assess_run(case["experiment"], run)
        self.assertTrue(result["usable"])
        self.assertFalse(result["passes"])
        self.assertGreater(result["unhealthy_samples"], 0)
        self.assertEqual(result["health_faults"], 1)

    def test_reset_reconnect_config_change_time_gap_and_truncation_are_unusable(self):
        changes = [lambda run: run["samples"][-1].update(uptime_ms=0),
                   lambda run: run["samples"][-1].update(session_id="new-session"),
                   lambda run: run["samples"][-1].update(profile_sha256="a" * 64),
                   lambda run: run["samples"][1].update(elapsed_ms=1000),
                   lambda run: run["samples"].pop(),
                   lambda run: run["samples"][0].update(health_faults=1)]
        for change in changes:
            case = self.case()
            change(case["runs"][0])
            with self.subTest(change=change):
                self.assertFalse(assess_run(case["experiment"], case["runs"][0])["usable"])
                data = build_dataset(dict(self.bundle, cases=[case]), self.catalogue)
                self.assertFalse(data["rows"])
                self.assertEqual(len(data["excluded"]), 1)

    def test_missing_or_duplicate_repetitions_cannot_bias_labels(self):
        case = self.case()
        case["runs"].pop()
        data = build_dataset(dict(self.bundle, cases=[case]), self.catalogue)
        self.assertEqual(data["excluded"][0]["reasons"], ["missing_repetitions"])
        case = self.case()
        case["runs"].append(deepcopy(case["runs"][0]))
        with self.assertRaisesRegex(ConfigurationError, "Duplicate trial"):
            build_dataset(dict(self.bundle, cases=[case]), self.catalogue)

    def test_provenance_unknown_fields_and_noninteger_observations_are_rejected(self):
        for field, value in (("experiment_sha256", "f" * 64), ("firmware_sha256", "f" * 64),
                             ("origin", "hardware"), ("extra", 1)):
            case = self.case()
            case["runs"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ConfigurationError):
                assess_run(case["experiment"], case["runs"][0])
        case = self.case()
        case["runs"][0]["samples"][0]["metric_ms"] = 1.0
        with self.assertRaises(ConfigurationError):
            assess_run(case["experiment"], case["runs"][0])

    def test_mixed_origins_are_rejected_even_with_complete_declared_provenance(self):
        simulated = self.case()
        hardware = self.case()
        experiment = hardware["experiment"]
        scenario = dict(experiment["scenario"], scenario_id="hardware-declaration-test")
        manifest = dict(experiment["build_manifest"], backend="target")
        hardware["experiment"] = prepare(experiment["profile"], manifest, scenario, self.catalogue)
        for run in hardware["runs"]:
            run.update(experiment_sha256=digest(hardware["experiment"]), origin="hardware",
                       method="telemetry_sample_age", firmware_sha256="a" * 64, identity_evidence_sha256="b" * 64)
        with self.assertRaisesRegex(ConfigurationError, "Never mix"):
            build_dataset(dict(self.bundle, cases=[simulated, hardware]), self.catalogue)

    def test_family_splits_are_disjoint_and_all_sibling_scenarios_stay_together(self):
        split = self.report["split"]
        groups = [set(value) for value in split.values()]
        self.assertFalse(groups[0] & groups[1] or groups[0] & groups[2] or groups[1] & groups[2])
        self.assertEqual(set.union(*groups), {row["family_id"] for row in self.dataset["rows"]})
        counts = {}
        for case in self.dataset["cases"]:
            family = case["scenario"]["family_id"]
            counts[family] = counts.get(family, 0) + 1
        self.assertTrue(all(value == 2 for value in counts.values()))
        with self.assertRaisesRegex(ConfigurationError, "ten independent"):
            split_families(self.dataset["rows"][:4], 42)

    def test_feature_extraction_never_reads_outcome_or_selected_answer(self):
        case = self.case()
        experiment = case["experiment"]
        f = features(experiment, 500, self.catalogue)
        case["runs"][0]["samples"][-1]["metric_ms"] = 999999
        self.assertEqual(features(experiment, 500, self.catalogue), f)
        changed = deepcopy(self.dataset)
        changed["rows"][0]["features"][0] = 1
        with self.assertRaisesRegex(ConfigurationError, "differs"):
            validate_dataset(changed, self.bundle, self.catalogue)

    def test_recommendation_requires_explicit_simulation_and_shared_validation(self):
        experiment = next(case for case in self.dataset["cases"] if context(case) in self.model["contexts"])
        self.assertEqual(recommend(self.model, experiment)["status"], "abstain")
        result = recommend(self.model, experiment, allow_simulated=True)
        self.assertFalse(result["deployment_allowed"])
        if result["profile"] is not None:
            restored = deepcopy(result["profile"])
            target = experiment["target"]
            original = next(d for d in experiment["profile"]["devices"] if d["device_id"] == target["instance_id"])
            edited = next(d for d in restored["devices"] if d["device_id"] == target["instance_id"])
            edited["behaviour"][target["period_field"]] = original["behaviour"][target["period_field"]]
            self.assertEqual(restored, experiment["profile"])
        changed_manifest = dict(experiment["build_manifest"], build_id="new-build")
        changed = prepare(experiment["profile"], changed_manifest, experiment["scenario"], self.catalogue)
        self.assertEqual(recommend(self.model, changed, allow_simulated=True)["reason"], "unseen_catalogue_or_build_context")

    def test_bad_model_cycles_ranges_and_deployment_claims_are_rejected(self):
        variants = []
        for field, value in (("deployment_allowed", True), ("version", "9.0.0"), ("threshold_per_mille", 1)):
            variants.append(dict(self.model, **{field: value}))
        cyclic = deepcopy(self.model)
        cyclic["nodes"][0]["left"] = 0
        variants.append(cyclic)
        malformed = deepcopy(self.model)
        malformed["domain"][0] = [2, 1]
        variants.append(malformed)
        for model in variants:
            with self.assertRaises(ConfigurationError):
                validate_model(model)
        self.assertEqual(predict(self.model, [0xFFFFFFFF] * 6), -1)
        with self.assertRaises(ConfigurationError):
            predict(self.model, [True] * 6)

    def test_model_is_reproducible_and_rules_are_reported_even_when_better(self):
        model, report = train(self.dataset, self.bundle, catalogue=self.catalogue)
        self.assertEqual(model, self.model)
        self.assertEqual(report, self.report)
        for part in ("train", "validation", "test"):
            self.assertEqual(set(report["results"][part]), {"tree", "simple_rule", "descriptor_default", "current_profile", "majority"})

    def test_generated_c_passes_gate_and_matches_python_at_boundaries(self):
        with tempfile.TemporaryDirectory(prefix="uthome-timing-") as folder:
            path = Path(folder) / "export"
            evidence = export_c(self.model, path)
            self.assertFalse(evidence["deployment_allowed"])
            self.assertEqual(evidence["generated_c_findings"], 0)
            parity = verify_c(self.model, path, [row["features"] for row in self.dataset["rows"]])
            self.assertEqual(parity["mismatches"], 0)
            self.assertGreater(parity["vectors"], 1000)
            artifact = Path(folder) / "model.json"
            save(artifact, self.model)
            with self.assertRaises(FileExistsError):
                save(artifact, self.model)


if __name__ == "__main__":
    unittest.main()
