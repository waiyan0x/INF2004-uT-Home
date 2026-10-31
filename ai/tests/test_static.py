"""Static execution handoff, measurement integrity and eighteen-input C export."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from ai import measurements, protocol_export, workload
from ai.protocol_capture import collect
from ai.static_context import REQUIRED_CHECKS, bind_experiment, check_capture, qualification_reasons, validate_context
from ai.tests import test_protocol
from ai.timing import digest
from tools.config_validator.planner import ConfigurationError, ROOT


def context_fixture():
    # Test-only values; these never become a bench qualification record.
    return dict(contract="uthome.execution-context", version="0.1.0", firmware_elf_sha256="a" * 64,
        workers=dict(slots=4, stack_bytes=[8192]*4, reserved_stack_bytes=32768, created_at_startup=True),
        parser=dict(input_bytes=8192, workspace_bytes=32768),
        ai=dict(max_candidates=4, writable_bytes=512, incremental_stack_bytes=512,
                code_rodata_bytes=4096, stack_accounting="inside_controller_stack"),
        qualification=dict(checks={name: "passed" for name in REQUIRED_CHECKS}, startup_application_heap_calls=0,
                           runtime_application_heap_calls=0, post_startup_kernel_creations=0, apply_cycles=20,
                           evidence_sha256={"test-only.txt": hashlib.sha256(b"test only").hexdigest()}))


def model_fixture():
    return dict(contract="uthome.protocol-tree", version="0.2.0", feature_names=workload.FEATURES,
                domain=deepcopy(workload.LIMITS), contexts=["a"*64],
                nodes=[dict(feature=17, threshold=512, left=1, right=2, score=500),
                       dict(feature=-1, threshold=0, left=0, right=0, score=899),
                       dict(feature=-1, threshold=0, left=0, right=0, score=900)],
                threshold_per_mille=900, study_sha256="b"*64, dataset_sha256="c"*64, origin="hardware",
                deployment_allowed=False,
                training=dict(library="scikit-learn", version="1.8.0", seed=42, max_depth=4, min_samples_leaf=3))


class StaticTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_protocol.ProtocolTests.setUpClass()
        cls.study = deepcopy(test_protocol.ProtocolTests.study)
        cls.identity = deepcopy(test_protocol.ProtocolTests.identity)
        cls.experiment = deepcopy(cls.study["cases"][0]["experiment"])

    def test_bound_workloads_are_not_reserved_task_count(self):
        binding = bind_experiment(self.experiment, context_fixture())
        self.assertEqual(binding["active_workloads"], sum(d["enabled"] for d in self.experiment["profile"]["devices"]))
        self.assertEqual(binding["reserved_workers"], 4)
        self.assertEqual(binding["reserved_stack_bytes"], 32768)
        self.assertEqual(binding["experiment_sha256"], digest(self.experiment))
        self.assertNotEqual(binding["active_workloads"], binding["reserved_workers"])

    def test_idle_stacks_and_actual_slot_capacities_cannot_be_ignored(self):
        context = context_fixture()
        context["workers"]["reserved_stack_bytes"] = 8192
        with self.assertRaises(ConfigurationError):
            validate_context(context)
        context["workers"].update(stack_bytes=[1024]*4, reserved_stack_bytes=4096)
        with self.assertRaisesRegex(ConfigurationError, "stack capacity"):
            bind_experiment(self.experiment, context)

    def test_qualification_requires_zero_counters_same_image_and_complete_evidence(self):
        context = context_fixture()
        self.assertEqual(qualification_reasons(context, "a"*64), [])
        for field in ("startup_application_heap_calls", "runtime_application_heap_calls", "post_startup_kernel_creations"):
            for value in (None, 1):
                bad = deepcopy(context)
                bad["qualification"][field] = value
                self.assertIn(field + "_not_zero", qualification_reasons(bad, "a"*64))
        self.assertIn("running_firmware_not_verified", qualification_reasons(context, "b"*64))
        context["qualification"]["checks"]["discovery_ownership"] = "not_measured"
        self.assertIn("discovery_ownership_not_qualified", qualification_reasons(context, "a"*64))

    def test_hash_checked_evidence_and_binding_reject_drift(self):
        binding = bind_experiment(self.experiment, context_fixture())
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "test-only.txt"
            path.write_bytes(b"test only")
            self.assertEqual(check_capture(binding, "a"*64, folder), binding)
            path.write_bytes(b"changed")
            with self.assertRaisesRegex(ConfigurationError, "hash mismatch"):
                check_capture(binding, "a"*64, folder)
            binding["active_workloads"] += 1
            with self.assertRaisesRegex(ConfigurationError, "binding changed"):
                check_capture(binding, "a"*64, folder)

    def test_evidence_paths_cannot_escape_retained_folder(self):
        from ai.static_context import check_evidence
        context = context_fixture()
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root/"test-only.txt").write_bytes(b"test only")
            (root/"retained").mkdir()
            context["qualification"]["evidence_sha256"] = {
                "../test-only.txt": hashlib.sha256(b"test only").hexdigest()}
            with self.assertRaisesRegex(ConfigurationError, "escaped"):
                check_evidence(context, root/"retained")

    def test_unqualified_capture_never_opens_serial_or_creates_output(self):
        from unittest.mock import Mock
        context = context_fixture()
        context["qualification"]["checks"]["runtime_heap"] = "failed"
        workflow = Mock()
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "capture"
            with self.assertRaisesRegex(ConfigurationError, "not qualified"):
                collect(workflow, "TEST", self.study, self.identity, self.study["base_profile"], output,
                        [self.experiment["scenario"]["family_id"]], execution_context=context, qualification_folder=folder)
            self.assertFalse(output.exists())
            workflow.dispatch.assert_not_called()

    def test_protocol_export_preserves_eighteenth_feature_and_threshold_boundary(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "export"
            model = model_fixture()
            result = protocol_export.export_c(model, output)
            self.assertFalse(result["deployment_allowed"])
            self.assertEqual(result["feature_bytes"], 72)
            parity = protocol_export.verify_c(model, output)
            self.assertEqual(parity["mismatches"], 0)
            self.assertGreater(parity["vectors"], 1000)
            model["nodes"][1]["score"] = 900
            with self.assertRaisesRegex(ValueError, "another model"):
                protocol_export.verify_c(model, output)

    def test_protocol_export_rejects_cycle_and_deployment_claim(self):
        for mutate in (lambda m: m["nodes"][0].update(left=0), lambda m: m.update(deployment_allowed=True)):
            model = model_fixture()
            mutate(model)
            with tempfile.TemporaryDirectory() as folder:
                output = Path(folder) / "export"
                with self.assertRaises(ConfigurationError):
                    protocol_export.export_c(model, output)
                self.assertFalse(output.exists())

    def test_largest_supported_tree_and_all_feature_domains_match_c(self):
        model = model_fixture()
        nodes = []
        def branch(depth, low, high):
            index = len(nodes)
            nodes.append(dict(feature=-1, threshold=0, left=0, right=0, score=900 if low % 2 else 899))
            if depth < 4:
                middle = (low + high) // 2
                left = branch(depth + 1, low, middle)
                right = branch(depth + 1, middle + 1, high)
                nodes[index] = dict(feature=17, threshold=middle, left=left, right=right, score=500)
            return index
        branch(0, 0, 65536)
        model["nodes"] = nodes
        self.assertEqual(len(nodes), 31)
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "export"
            report = protocol_export.export_c(model, output)
            self.assertEqual(report["constant_data_bytes"], 764)
            self.assertEqual(protocol_export.verify_c(model, output)["mismatches"], 0)

    def test_qualified_collector_binds_sidecar_and_still_restores_on_disconnect(self):
        import zlib
        from unittest.mock import Mock, patch
        from ai.tests.test_recording import Board, Clock
        original = self.study["base_profile"]
        crc = zlib.crc32(json.dumps(original, ensure_ascii=True, separators=(",", ":")).encode())
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
        with tempfile.TemporaryDirectory() as folder:
            (Path(folder) / "test-only.txt").write_bytes(b"test only")
            with patch("ai.protocol_capture.apply_trial", return_value={}) as apply_mock, \
                    patch("ai.protocol_capture.record_run", side_effect=OSError("test_disconnect")):
                result = collect(workflow, "TEST", self.study, self.identity, original, Path(folder)/"capture", ["all-current"],
                                 execution_context=context_fixture(), qualification_folder=folder)
            self.assertEqual(result["error"], "test_disconnect")
            self.assertTrue(result["restored"])
            self.assertEqual(apply_mock.call_count, 1)
            saved = json.loads((Path(folder)/"capture/static-bindings.json").read_text())
            self.assertEqual(saved["all-current"]["execution_context_sha256"], digest(context_fixture()))

    def test_measurement_accumulator_c99_warnings_are_errors(self):
        compiler = shutil.which("gcc")
        self.assertIsNotNone(compiler, "GCC is required")
        with tempfile.TemporaryDirectory() as folder:
            executable = Path(folder) / "costtest.exe"
            subprocess.run([compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2",
                            "-I", str(ROOT / "firmware/src/runtime"), str(ROOT / "tests/ai/costmain.c"),
                            str(ROOT / "firmware/src/runtime/workcost.c"), "-o", str(executable)], check=True, capture_output=True)
            subprocess.run([str(executable)], check=True, capture_output=True, timeout=10)


class MeasurementTests(unittest.TestCase):
    def snapshot(self, steps=1, timestamp=1000):
        stats = dict.fromkeys(measurements.FIELDS, 0)
        stats.update(steps=steps, elapsed_us=100*steps, max_elapsed_us=100, transfer_bytes=30*steps,
                     transfers=steps, bus_wait_us=10*steps, bus_active_us=60*steps)
        return dict(contract="uthome.workload-measurement", version="0.1.0", firmware_elf_sha256="a"*64,
                    profile_sha256="b"*64, generation=1, timestamp_us=timestamp,
                    workers=[dict(slot=0, source_index=2, available=True, stats=stats)])

    def test_window_uses_counter_deltas_and_keeps_maximum_scope_explicit(self):
        result = measurements.summarize_window(self.snapshot(), self.snapshot(3, 2000))
        self.assertEqual(result["workers"][0]["steps"], 2)
        self.assertEqual(result["workers"][0]["transfer_bytes"], 60)
        self.assertFalse(result["cpu_time_measured"])
        self.assertFalse(result["model_features"])

    def test_unavailable_saturated_reset_rebound_or_changed_context_rejected(self):
        changes = [lambda v: v["workers"][0].update(available=False, stats=None),
                   lambda v: v["workers"][0]["stats"].update(flags=1),
                   lambda v: v.update(generation=2),
                   lambda v: v["workers"][0].update(source_index=1),
                   lambda v: v["workers"][0]["stats"].update(transfer_bytes=0)]
        for change in changes:
            after = self.snapshot(3, 2000)
            change(after)
            with self.assertRaises(ConfigurationError):
                measurements.summarize_window(self.snapshot(), after)

    def test_duplicate_slot_missing_fields_and_unknown_flags_rejected(self):
        value = self.snapshot()
        value["workers"].append(deepcopy(value["workers"][0]))
        with self.assertRaises(ConfigurationError):
            measurements.validate_snapshot(value)
        value = self.snapshot()
        value["workers"][0]["stats"].pop("elapsed_us")
        with self.assertRaises(ConfigurationError):
            measurements.validate_snapshot(value)


if __name__ == "__main__":
    unittest.main()
