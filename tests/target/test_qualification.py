"""Failure-path checks for the kernel qualification tool; no SDK or board required."""
import importlib.util
import json
from pathlib import Path, PureWindowsPath
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("qualify_kernel", ROOT / "tools/target/qualify_kernel.py")
qualification = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qualification)


def setUpModule():
    """Reject nonconforming C before the suite can compile project sources."""
    subprocess.run(
        [sys.executable, str(ROOT / "tools/c_standard/check.py"), "--root", str(ROOT)],
        cwd=ROOT, check=True,
    )


class QualificationTests(unittest.TestCase):
    def test_application_requires_runtime_mode_before_building(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder) / "uncreated"
            with patch.object(qualification, "check_c_standard") as gate:
                with self.assertRaisesRegex(qualification.QualificationError, "requires --runtime-port"):
                    qualification.build(Path("kernel"), Path("sdk"), work,
                                        configuration_app=True)
                gate.assert_not_called()
            self.assertFalse(work.exists())

    def test_resource_patch_failure_is_recorded_before_compiling(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            work = root / "build"
            resource = root / "resources.patch"
            resource.write_bytes(b"reviewed resource patch bytes\n")

            def fake_command(arguments, cwd=None):
                if "archive" in arguments:
                    (work / "kernel-source.zip").write_bytes(b"archive fixture")
                if "--check" in arguments:
                    self.assertEqual(cwd, work / "source")
                    raise qualification.QualificationError("resource patch rejected")
                return ""

            with patch.object(qualification, "inspect", return_value={}), \
                 patch.object(qualification, "run", side_effect=fake_command), \
                 patch.object(qualification, "unpack_source"), \
                 patch.object(qualification.subprocess, "Popen") as compiler:
                with self.assertRaisesRegex(qualification.QualificationError, "resource patch rejected"):
                    qualification.build(root / "kernel", root / "sdk", work,
                                        resource_patch=resource)
                compiler.assert_not_called()
            evidence = json.loads((work / "evidence.json").read_text())
            self.assertEqual(evidence["build_status"], "failed")
            self.assertEqual(evidence["resource_patch"]["sha256"], qualification.sha256(resource))
            self.assertEqual((work / "kernel-resources.patch").read_bytes(), resource.read_bytes())
            self.assertFalse((root / "kernel").exists())

    def test_compiler_profile_rejects_late_sdk_overrides(self):
        good = "gcc -std=gnu23 -Wall -Wextra -Werror -Wno-error=extra -std=c99 -Werror=extra"
        qualification.check_compiler_flags(good, "c99")
        qualification.check_compiler_flags(good + " -std=c11", "c11")
        qualification.check_compiler_flags(good + " -std=gnu99", "gnu99")
        for changed in (good + " -std=gnu23", good + " -Wno-error",
                        good + " -Wno-error=unused-variable", good + " -Wno-extra",
                        good + " -w", good.replace("-Wall", "")):
            with self.subTest(command=changed):
                with self.assertRaises(qualification.QualificationError):
                    qualification.check_compiler_flags(changed, "c99")

    def setUp(self):
        # Module setup has already run the real gate. Unit cases isolate build
        # failure ordering without repeatedly invoking its external formatter.
        self.standard = patch.object(qualification, "check_c_standard",
                                     return_value={"status": "passed"})
        self.check_standard = self.standard.start()
        self.addCleanup(self.standard.stop)

    def test_nonzero_checker_exit_is_a_build_error(self):
        self.standard.stop()
        failure = subprocess.CompletedProcess([], 1, "format mismatch", "policy error")
        with patch.object(qualification.subprocess, "run", return_value=failure) as checker:
            with self.assertRaisesRegex(qualification.QualificationError, "format mismatch"):
                qualification.check_c_standard()
        arguments = checker.call_args.args[0]
        self.assertIn(str(ROOT / "tools/c_standard/check.py"), arguments)
        self.assertNotIn("--skip-format", arguments)

    def test_standard_failure_prevents_sdk_and_workspace_changes(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder) / "fresh"
            self.check_standard.side_effect = qualification.QualificationError("standard failed")
            with patch.object(qualification, "inspect") as inspect:
                with self.assertRaisesRegex(qualification.QualificationError, "standard failed"):
                    qualification.build(Path("kernel"), Path("sdk"), work)
                inspect.assert_not_called()
            self.assertFalse(work.exists())

    def test_wrong_revision_and_dirty_checkout_are_rejected(self):
        with patch.object(qualification, "run", return_value="unexpected"):
            with self.assertRaisesRegex(qualification.QualificationError, "Unexpected revision"):
                qualification.pinned_checkout(Path("kernel"), qualification.KERNEL_REVISION)
        with patch.object(qualification, "run", side_effect=[qualification.KERNEL_REVISION, " M main.c"]):
            with self.assertRaisesRegex(qualification.QualificationError, "pristine"):
                qualification.pinned_checkout(Path("kernel"), qualification.KERNEL_REVISION)

    def test_existing_work_is_preserved_before_any_command(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            marker = work / "user-file"
            marker.write_text("keep")
            with patch.object(qualification, "inspect") as inspect:
                with self.assertRaisesRegex(qualification.QualificationError, "already exists"):
                    qualification.build(Path("kernel"), Path("sdk"), work)
                inspect.assert_not_called()
            self.assertEqual(marker.read_text(), "keep")

    def test_spaces_in_sdk_and_work_are_rejected(self):
        for path in (Path("C:/contains spaces/project"), Path("build\tpath")):
            with self.subTest(path=path), self.assertRaises(qualification.QualificationError):
                qualification.no_spaces(path)

    def test_build_cannot_write_inside_dependency_checkouts(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for base in (root / "kernel", root / "sdk"):
                with self.subTest(base=base), self.assertRaisesRegex(qualification.QualificationError, "outside"):
                    qualification.build(root / "kernel", root / "sdk", base / "build")
            self.assertEqual(list(root.iterdir()), [])

    def test_windows_sdk_path_is_cmake_safe_without_mutating_parent_environment(self):
        with patch.dict(qualification.os.environ, {"IDF_PATH": "original"}):
            environment = qualification.build_environment(PureWindowsPath("C:/Users/user/esp-idf"))
            self.assertEqual(environment["IDF_PATH"], "C:/Users/user/esp-idf")
            self.assertEqual(environment["PYTHONUTF8"], "1")
            self.assertEqual(qualification.os.environ["IDF_PATH"], "original")

    def test_missing_compiler_has_no_side_effects(self):
        with patch.object(qualification.shutil, "which", return_value=None):
            with self.assertRaisesRegex(qualification.QualificationError, "Activate ESP-IDF"):
                qualification.inspect(Path("kernel"), Path("sdk"))

    def test_archive_rejects_escapes_and_links_before_extracting(self):
        for name, symlink in (("../escape", False), ("/absolute", False),
                              ("C:/absolute", False), ("..\\escape", False), ("link", True)):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                archive = root / "source.zip"
                with zipfile.ZipFile(archive, "w") as output:
                    output.writestr("safe.txt", "safe")
                    entry = zipfile.ZipInfo(name)
                    if symlink:
                        entry.create_system = 3
                        entry.external_attr = (stat.S_IFLNK | 0o777) << 16
                    output.writestr(entry, "target")
                with self.assertRaises(qualification.QualificationError):
                    qualification.unpack_source(archive, root / "source")
                self.assertFalse((root / "source").exists())

    def test_failed_staging_records_failure_and_never_claims_board_pass(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder) / "fresh"
            with patch.object(qualification, "inspect", return_value={"kernel_revision": "fixture"}), \
                 patch.object(qualification, "run", side_effect=qualification.QualificationError("archive failed")):
                with self.assertRaisesRegex(qualification.QualificationError, "archive failed"):
                    qualification.build(Path("kernel"), Path("sdk"), work)
            evidence = json.loads((work / "evidence.json").read_text())
            self.assertEqual(evidence["build_status"], "failed")
            self.assertEqual(evidence["board_status"], "not_tested")
            self.assertEqual(evidence["artifacts"], {})

    def test_clean_archive_preserves_source_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with zipfile.ZipFile(root / "source.zip", "w") as output:
                output.writestr("examples/tasks/main/main.c", b"int main(void) {return 0;}\n")
            qualification.unpack_source(root / "source.zip", root / "source")
            self.assertEqual((root / "source/examples/tasks/main/main.c").read_bytes(),
                             b"int main(void) {return 0;}\n")

    def test_target_rebuild_rejects_changed_missing_or_incomplete_inputs(self):
        cases = ("staged_changed", "staged_missing", "original_changed",
                 "original_missing", "manifest_missing", "manifest_incomplete")
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as folder:
                folder = Path(folder)
                repository = folder / "repository"
                work = folder / "work"
                for source in qualification.RUNTIME_SOURCE_FILES:
                    target = repository / source
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes((ROOT / source).read_bytes())
                defaults = work / "source/examples/tasks/sdkconfig.defaults"
                defaults.parent.mkdir(parents=True)
                defaults.write_bytes(b'CONFIG_IDF_TARGET="esp32c3"\n')
                with patch.object(qualification, "ROOT", repository):
                    project, _ = qualification.stage_runtime_port(work)
                qualification.verify_runtime_snapshot(project, repository)
                if case.startswith("manifest"):
                    path = project / "source-manifest.json"
                    if case == "manifest_missing":
                        path.unlink()
                    else:
                        manifest = json.loads(path.read_text())
                        del manifest["sources"]["firmware/src/runtime/tkport.c"]
                        path.write_text(json.dumps(manifest))
                else:
                    path = (project / "runtime/tkport.c" if case.startswith("staged")
                            else repository / "firmware/src/runtime/tkport.c")
                    if case.endswith("missing"):
                        path.unlink()
                    else:
                        path.write_bytes(path.read_bytes() + b"/* Changed after qualification. */\n")
                with self.assertRaises(qualification.QualificationError):
                    qualification.verify_runtime_snapshot(project, repository)

    def test_snapshot_cli_requires_no_sdk_activation(self):
        with patch.object(qualification, "verify_runtime_snapshot") as verify, \
             patch.dict(qualification.os.environ, {}, clear=True), \
             patch("builtins.print"):
            self.assertEqual(qualification.main(["verify-snapshot", "--project-dir", "project"]), 0)
            verify.assert_called_once_with(Path("project"))

    def test_runtime_sources_are_snapshotted_and_identified_by_hash(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            defaults = work / "source/examples/tasks/sdkconfig.defaults"
            defaults.parent.mkdir(parents=True)
            defaults.write_bytes(b"CONFIG_IDF_TARGET=\"esp32c3\"\n")
            project, sources = qualification.stage_runtime_port(work)
            self.assertEqual(len(sources), 126)
            for name in ("hal/c3umem.c", "hal/c3umem.h", "main/uartcheck.c", "main/uartcheck.h"):
                self.assertTrue((project / name).is_file())
            qualification.verify_runtime_snapshot(project)
            for source, entry in sources.items():
                target = work / entry["staged_path"]
                self.assertEqual(qualification.sha256(target), entry["sha256"])
                original = defaults if source.endswith("sdkconfig.defaults") else ROOT / source
                self.assertEqual(target.read_bytes(), original.read_bytes())
            self.assertIn("UTHOME_RUNTIME_UTKERNEL=1",
                          (project / "main/CMakeLists.txt").read_text())
            self.assertNotIn("fake_kernel", (project / "main/CMakeLists.txt").read_text())
            self.assertIn("uthome_c_standard", (project / "main/CMakeLists.txt").read_text())
            self.assertIn("verify-snapshot", (project / "main/CMakeLists.txt").read_text())
            self.assertTrue((project / "main/main.h").is_file())
            self.assertTrue((project / "runtime/tkport.h").is_file())


if __name__ == "__main__":
    unittest.main()
