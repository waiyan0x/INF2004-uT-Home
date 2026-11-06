"""Failure-path checks for the kernel qualification tool; no SDK or board required."""
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
from tools.target import qualify_kernel as qualification


def build_with_flags(kernel, sdk, work, *, compat_patch=None, resource_patch=None, **flags):
    """Exercise the CLI flag resolver and resolved build API together."""
    return qualification.build(kernel, sdk, work, selection=qualification.resolve_mode(**flags),
                               compat_patch=compat_patch, resource_patch=resource_patch)


def setUpModule():
    """Reject nonconforming C before the suite can compile project sources."""
    subprocess.run(
        [sys.executable, str(ROOT / "tools/c_standard/check.py"), "--root", str(ROOT)],
        cwd=ROOT, check=True,
    )


class QualificationTests(unittest.TestCase):
    def test_station_memory_defaults_are_wifi_service_only(self):
        station = ROOT / "tests/target/runtime_port_board/wifi-station.defaults"
        required = {
            "CONFIG_LWIP_IPV6=n",
            "CONFIG_LWIP_DHCPS=n",
            "CONFIG_ESP_WIFI_SOFTAP_SUPPORT=n",
            "CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT=n",
        }
        self.assertEqual(
            required,
            {line for line in station.read_text().splitlines()
             if line.startswith("CONFIG_")},
        )
        project = (ROOT / "tests/target/runtime_port_board/CMakeLists.txt").read_text()
        self.assertEqual(project.count('"wifi-station.defaults"'), 1)
        self.assertIn(
            'if(UTHOME_NETWORK_WIFI)\n'
            '        list(APPEND SDKCONFIG_DEFAULTS "wifi.defaults" '
            '"wifi-station.defaults")',
            project,
        )
        diagnostic = (ROOT / "tests/target/runtime_port_board/wifi.defaults").read_text()
        shared = (ROOT / "tests/target/runtime_port_board/network.defaults").read_text()
        ble = (ROOT / "tests/target/runtime_port_board/ble.defaults").read_text()
        for setting in required:
            name = setting.partition("=")[0]
            self.assertNotIn(name, diagnostic)
            self.assertNotIn(name, shared)
            self.assertNotIn(name, ble)

        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            defaults = work / "source/examples/tasks/sdkconfig.defaults"
            defaults.parent.mkdir(parents=True)
            defaults.write_bytes(b'CONFIG_IDF_TARGET="esp32c3"\n')
            staged, _ = qualification.stage_runtime_port(work)
            self.assertEqual(
                station.read_bytes(),
                (staged / "wifi-station.defaults").read_bytes(),
            )

    def test_wifi_settings_reject_each_missing_isolation_requirement(self):
        settings = ["CONFIG_FREERTOS_HZ=1000", "# CONFIG_BT_ENABLED is not set",
                    "# CONFIG_ESP_WIFI_NVS_ENABLED is not set",
                    "# CONFIG_ESP_WIFI_IRAM_OPT is not set",
                    "# CONFIG_ESP_WIFI_RX_IRAM_OPT is not set",
                    "# CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE is not set"]
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            (work / "sdkconfig").write_text("\n".join(settings))
            self.assertIn("diagnostic only", qualification.verify_wifi_probe(work))
            for missing in settings:
                with self.subTest(missing=missing):
                    (work / "sdkconfig").write_text("\n".join(s for s in settings if s != missing))
                    with self.assertRaisesRegex(qualification.QualificationError, "isolation settings"):
                        qualification.verify_wifi_probe(work)

    def test_configuration_layout_requires_the_reviewed_digest(self):
        with patch.object(qualification, "sha256", return_value=qualification.CONFIGURATION_TABLE_SHA256) as digest:
            qualification.verify_configuration_layout(Path("isolated"))
            digest.assert_called_once_with(Path("isolated/build/partition_table/partition-table.bin"))
        with patch.object(qualification, "sha256", return_value="0" * 64):
            with self.assertRaisesRegex(qualification.QualificationError, "partition table differs"):
                qualification.verify_configuration_layout(Path("isolated"))

    def test_managed_cjson_requires_lock_version_and_all_source_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            project = Path(folder)
            with self.assertRaisesRegex(qualification.QualificationError, "Missing managed-component"):
                qualification.verify_managed_cjson(project)
            lock = project / "dependencies.lock"
            lock.write_text("dependencies: {espressif/cjson: {version: wrong}}")
            with self.assertRaisesRegex(qualification.QualificationError, "version"):
                qualification.verify_managed_cjson(project)
            lock.write_text("dependencies: {espressif/cjson: {version: 1.7.19~2, component_hash: retained}}")
            managed = project / "managed_components/espressif__cjson/cJSON"
            managed.mkdir(parents=True)
            for name in ("cJSON.c", "cJSON.h", "LICENSE"):
                (managed / name).write_bytes((ROOT / "firmware/third_party/cjson" / name).read_bytes())
            result = qualification.verify_managed_cjson(project)
            self.assertEqual("retained", result["component_hash"])
            self.assertEqual(qualification.sha256(lock), result["lock_sha256"])
            self.assertEqual({"cJSON.c", "cJSON.h", "LICENSE"}, set(result["source_sha256"]))
            for name in result["source_sha256"]:
                with self.subTest(name=name):
                    path = managed / name
                    original = path.read_bytes()
                    path.write_bytes(original + b"changed")
                    with self.assertRaisesRegex(qualification.QualificationError, "bytes differ"):
                        qualification.verify_managed_cjson(project)
                    path.write_bytes(original)

    def test_services_reject_diagnostic_combinations_before_setup(self):
        for diagnostic in ('model_load_probe', 'senlux_probe', 'senlux_app_probe',
                           'wifi_probe', 'ble_probe', 'model_probe', 'native_uart_fixture'):
            with self.subTest(diagnostic=diagnostic), tempfile.TemporaryDirectory() as folder:
                work = Path(folder) / 'fresh'
                with patch.object(qualification, 'inspect') as inspect:
                    with self.assertRaises(qualification.QualificationError):
                        build_with_flags(Path('kernel'), Path('sdk'), work,
                            runtime_port=True, configuration_app=True, static_net_queues=True,
                            network_services='ble', **{diagnostic: True})
                    inspect.assert_not_called()
                self.assertFalse(work.exists())

    def test_diagnostic_mode_rejects_paths_and_application_before_side_effects(self):
        cases = ({"static_net_queues": True},
                 {"static_net_queues": True, "runtime_port": True, "native_uart_fixture": True},
                 {"upstream_example": "../escape"},
                 {"upstream_example": "ble_gatt", "runtime_port": True},
                 {"upstream_example": "udp_pingpong_c3", "configuration_app": True})
        for options in cases:
            with self.subTest(options=options), tempfile.TemporaryDirectory() as folder:
                work = Path(folder) / "fresh"
                with patch.object(qualification, "inspect") as inspect:
                    with self.assertRaises(qualification.QualificationError):
                        build_with_flags(Path("kernel"), Path("sdk"), work, **options)
                    inspect.assert_not_called()
                self.assertFalse(work.exists())

    def test_diagnostic_failure_records_scope_without_claiming_board_acceptance(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder) / "fresh"
            with patch.object(qualification, "inspect", return_value={}), \
                 patch.object(qualification, "run", side_effect=qualification.QualificationError("archive failed")):
                with self.assertRaisesRegex(qualification.QualificationError, "archive failed"):
                    build_with_flags(Path("kernel"), Path("sdk"), work, upstream_example="ble_gatt")
            evidence = json.loads((work / "evidence.json").read_text())
            self.assertEqual(evidence["example"], "examples/ble_gatt")
            self.assertTrue(evidence["diagnostic_only"])
            self.assertEqual(evidence["board_status"], "not_tested")
            self.assertEqual(evidence["build_status"], "failed")

    def test_lf_patch_applies_to_crlf_kernel_file_on_any_host(self):
        # Reproduces the Linux CI failure: pinned kernel file CRLF, patch LF,
        # and no core.autocrlf in the user's Git configuration.
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            source = work / "source"
            source.mkdir()
            (source / "config.h").write_bytes(b"#define A 1\r\n#define B 1\r\n#define C 1\r\n")
            patch_file = work / "change.patch"
            patch_file.write_bytes(b"--- a/config.h\n+++ b/config.h\n@@ -1,3 +1,3 @@\n"
                                   b" #define A 1\n-#define B 1\n+#define B 0\n #define C 1\n")
            empty = work / "gitconfig"
            empty.write_bytes(b"")
            isolated = {"GIT_CONFIG_GLOBAL": str(empty), "GIT_CONFIG_NOSYSTEM": "1"}
            with patch.dict(qualification.os.environ, isolated):
                plain = subprocess.run(["git", "apply", "--check", str(patch_file)], cwd=source,
                                       capture_output=True)
                self.assertNotEqual(plain.returncode, 0, "scenario must reproduce the CI failure")
                qualification.apply_patch(patch_file, source)
            self.assertIn(b"#define B 0", (source / "config.h").read_bytes())

    def test_application_requires_runtime_mode_before_building(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder) / "uncreated"
            with patch.object(qualification, "check_c_standard") as gate:
                with self.assertRaisesRegex(qualification.QualificationError, "requires --runtime-port"):
                    build_with_flags(Path("kernel"), Path("sdk"), work,
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
                    build_with_flags(root / "kernel", root / "sdk", work,
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
                    build_with_flags(Path("kernel"), Path("sdk"), work)
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
                    build_with_flags(Path("kernel"), Path("sdk"), work)
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
                    build_with_flags(root / "kernel", root / "sdk", base / "build")
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
                    build_with_flags(Path("kernel"), Path("sdk"), work)
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

    def test_wifi_probe_rejects_nonisolated_modes_before_any_build_work(self):
        for options in ({}, {"runtime_port": True},
                        {"runtime_port": True, "static_net_queues": True, "configuration_app": True},
                        {"runtime_port": True, "static_net_queues": True, "native_uart_fixture": True}):
            with self.subTest(options=options), patch.object(qualification, "inspect") as inspect:
                with self.assertRaisesRegex(qualification.QualificationError, "Wi-Fi probe"):
                    build_with_flags(Path("kernel"), Path("sdk"), Path("unused"), wifi_probe=True, **options)
                inspect.assert_not_called()

    def test_senlux_probe_requires_isolation_before_any_build_work(self):
        for options in ({}, {"runtime_port": True},
                        {"runtime_port": True, "static_net_queues": True, "configuration_app": True},
                        {"runtime_port": True, "static_net_queues": True, "wifi_probe": True},
                        {"runtime_port": True, "static_net_queues": True, "model_probe": True}):
            with self.subTest(options=options), patch.object(qualification, "inspect") as inspect:
                with self.assertRaisesRegex(qualification.QualificationError, "SEN0390 probe"):
                    build_with_flags(Path("kernel"), Path("sdk"), Path("unused"), senlux_probe=True, **options)
                inspect.assert_not_called()

    def test_senlux_application_probe_requires_explicit_isolated_mode(self):
        for options in ({}, {"runtime_port": True, "static_net_queues": True},
                        {"runtime_port": True, "configuration_app": True},
                        {"runtime_port": True, "static_net_queues": True, "configuration_app": True,
                         "senlux_probe": True},
                        {"runtime_port": True, "static_net_queues": True, "configuration_app": True,
                         "model_load_probe": True}):
            with self.subTest(options=options), patch.object(qualification, "inspect") as inspect:
                with self.assertRaisesRegex(qualification.QualificationError, "SEN0390 application probe"):
                    build_with_flags(Path("kernel"), Path("sdk"), Path("unused"),
                                        senlux_app_probe=True, **options)
                inspect.assert_not_called()

    def test_senlux_application_probe_checks_compiler_mode(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            (work / "build").mkdir()
            binder = work / "project/config/cfgbind.c"
            database = work / "build/compile_commands.json"
            def set_command(command):
                database.write_text(json.dumps([{"file": str(binder), "command": command}]))
            set_command("cc -DUTHOME_SENLUX_APP_PROBE=0 cfgbind.c")
            self.assertFalse(qualification.verify_senlux_app_probe(work, False)["enabled"])
            with self.assertRaises(qualification.QualificationError):
                qualification.verify_senlux_app_probe(work, True)
            set_command("cc -DUTHOME_SENLUX_APP_PROBE=1 cfgbind.c")
            self.assertTrue(qualification.verify_senlux_app_probe(work, True)["diagnostic_only"])
            set_command("cc -DUTHOME_SENLUX_APP_PROBE=1 -DUTHOME_SENLUX_APP_PROBE=0 cfgbind.c")
            with self.assertRaises(qualification.QualificationError):
                qualification.verify_senlux_app_probe(work, True)


    def test_loaded_model_rejects_wrong_build_modes_before_side_effects(self):
        for options in ({}, {'runtime_port': True, 'configuration_app': True},
                        {'runtime_port': True, 'static_net_queues': True},
                        {'runtime_port': True, 'configuration_app': True, 'static_net_queues': True, 'model_probe': True},
                        {'runtime_port': True, 'configuration_app': True, 'static_net_queues': True, 'wifi_probe': True}):
            with self.subTest(options=options), patch.object(qualification, 'inspect') as inspect:
                with self.assertRaisesRegex(qualification.QualificationError, 'Loaded model probe'):
                    build_with_flags(Path('kernel'), Path('sdk'), Path('unused'), model_load_probe=True, **options)
                inspect.assert_not_called()

    def test_loaded_symbols_cannot_leak_or_initialize_radios(self):
        expected = {'ailite_predict', 'aivects_count', 'aivects_get', 'aiload_observe'}
        self.assertFalse(qualification.check_model_load_symbols(set(), False)['deployment_allowed'])
        self.assertEqual(qualification.check_model_load_symbols(expected, True)['linked_symbols'], sorted(expected))
        for name in expected:
            with self.assertRaises(qualification.QualificationError):
                qualification.check_model_load_symbols({name}, False)
            with self.assertRaises(qualification.QualificationError):
                qualification.check_model_load_symbols(expected - {name}, True)
        for name in ['esp_wifi_init', 'esp_wifi_start', 'nimble_port_init']:
            with self.assertRaises(qualification.QualificationError):
                qualification.check_model_load_symbols(expected | {name}, True)

    def test_model_probe_requires_isolated_static_mode(self):
        for options in ({}, {'runtime_port': True},
                        {'runtime_port': True, 'static_net_queues': True, 'configuration_app': True},
                        {'runtime_port': True, 'static_net_queues': True, 'wifi_probe': True},
                        {'runtime_port': True, 'static_net_queues': True, 'ble_probe': True},
                        {'runtime_port': True, 'static_net_queues': True, 'native_uart_fixture': True}):
            with self.subTest(options=options), patch.object(qualification, 'inspect') as inspect:
                with self.assertRaisesRegex(qualification.QualificationError, 'Model probe'):
                    build_with_flags(Path('kernel'),Path('sdk'),Path('unused'),model_probe=True,**options)
                inspect.assert_not_called()

    def test_model_symbols_cannot_leak_into_normal_images_or_activate_radios(self):
        expected = {'aifrozen_predict', 'aicases_count', 'aicases_get', 'aiprobe_run'}
        self.assertFalse(qualification.check_model_symbols(set(), False)['deployment_allowed'])
        self.assertEqual(qualification.check_model_symbols(expected, True)['linked_symbols'], sorted(expected))
        for name in expected:
            with self.subTest(name=name), self.assertRaises(qualification.QualificationError):
                qualification.check_model_symbols(expected-{name}, True)
            with self.subTest(leaked=name), self.assertRaises(qualification.QualificationError):
                qualification.check_model_symbols({name}, False)
        for name in ('esp_wifi_init','esp_wifi_start','nimble_port_init'):
            with self.subTest(radio=name), self.assertRaises(qualification.QualificationError):
                qualification.check_model_symbols(expected|{name}, True)

    def test_queue_link_proof_rejects_missing_wrong_and_flash_aliases(self):
        aliases = qualification.queue_aliases(
            ROOT/'tests/target/runtime_port_board/main/netqueue.cmake')
        symbols = {}
        for owned in sorted(set(aliases.values())):
            symbols[owned] = 0x40381000 + len(symbols) * 32
        for original, owned in aliases.items():
            symbols['__wrap_' + original] = symbols[owned]
        self.assertEqual(len(qualification.check_queue_symbols(symbols, aliases)), 24)
        for name in ('c3nq_send_isr', '__wrap_xQueueGenericSendFromISR'):
            for value in (0, 0x42010000, 0x40380004):
                damaged = dict(symbols, **{name: value})
                with self.subTest(name=name, value=value), self.assertRaises(qualification.QualificationError):
                    qualification.check_queue_symbols(damaged, aliases)

    def test_startup_trace_link_is_exact_and_wifi_only(self):
        symbols = {}
        pairs = qualification.STARTUP_SYMBOLS
        for original, wrapper, native in pairs:
            symbols[wrapper] = 0x42010000 + len(symbols) * 32
            symbols["__wrap_" + original] = symbols[wrapper]
            symbols[native] = 0x42020000 + len(symbols) * 32
        self.assertTrue(qualification.check_startup_symbols(symbols, True)["enabled"])
        for name in ("__wrap_CreateMLock", "c3start_native_device"):
            damaged = dict(symbols)
            damaged[name] = 0
            with self.subTest(name=name), self.assertRaises(qualification.QualificationError):
                qualification.check_startup_symbols(damaged, True)
        with self.assertRaises(qualification.QualificationError):
            qualification.check_startup_symbols({"c3start_device": 1}, False)
        self.assertFalse(qualification.check_startup_symbols({}, False)["enabled"])

    def test_startup_link_verifies_elf_aliases_and_rejects_misdirection(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            (work / "build").mkdir()
            (work / "build/uthome_runtime_port.elf").write_bytes(b"fixture")
            symbols = {}
            for index, (original, wrapper, native) in enumerate(qualification.STARTUP_SYMBOLS):
                symbols[wrapper] = 0x42010000 + index * 32
                symbols["__wrap_" + original] = symbols[wrapper]
                symbols[native] = 0x42020000 + index * 32
            nm = "\n".join(f"{address:08x} T {name}" for name, address in sorted(symbols.items()))
            with patch.object(qualification, "run", return_value=nm):
                self.assertTrue(qualification.verify_startup_link(work, True)["enabled"])
                damaged = dict(symbols)
                damaged["c3start_native_device"] = 0
                with self.assertRaises(qualification.QualificationError):
                    qualification.check_startup_symbols(damaged, True)
                with self.assertRaises(qualification.QualificationError):
                    qualification.verify_startup_link(work, False)

    def test_startup_trace_definitions_are_scoped_to_wifi_sources(self):
        project = Path("/project")
        commands = {Path("/test" + suffix).resolve(): " ".join(
            f"-D{symbol}={value}" for symbol, value in renames.items())
            for suffix, renames in qualification.STARTUP_RENAMES.items()}
        commands[(project / "hal/c3start.c").resolve()] = "gcc -c c3start.c"
        self.assertTrue(qualification.verify_startup_scope(commands, project, True)["enabled"])
        no_trace = {path: "" for path in commands if path.name != "c3start.c"}
        self.assertFalse(qualification.verify_startup_scope(no_trace, project, False)["enabled"])
        leaked = dict(no_trace)
        leaked[Path("/unrelated.c").resolve()] = "-DCreateMLock=c3start_native_mlock"
        with self.assertRaises(qualification.QualificationError):
            qualification.verify_startup_scope(leaked, project, False)

    def test_ble_probe_requires_isolated_static_mode(self):
        for options in ({}, {"runtime_port": True},
                        {"runtime_port": True, "static_net_queues": True, "configuration_app": True},
                        {"runtime_port": True, "static_net_queues": True, "native_uart_fixture": True},
                        {"runtime_port": True, "static_net_queues": True, "wifi_probe": True}):
            with self.subTest(options=options), patch.object(qualification, "inspect") as inspect:
                with self.assertRaises(qualification.QualificationError):
                    build_with_flags(Path("kernel"), Path("sdk"), Path("unused"), ble_probe=True, **options)
                inspect.assert_not_called()

    def test_ble_probe_checks_linked_activation_and_real_settings(self):
        settings = ["CONFIG_FREERTOS_HZ=1000", "CONFIG_BT_ENABLED=y", "CONFIG_BT_NIMBLE_ENABLED=y",
                    "# CONFIG_BT_NIMBLE_NVS_PERSIST is not set",
                    "# CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE is not set",
                    "# CONFIG_BT_NIMBLE_ROLE_PERIPHERAL is not set",
                    "# CONFIG_BT_NIMBLE_ROLE_CENTRAL is not set",
                    "# CONFIG_BT_NIMBLE_ROLE_OBSERVER is not set",
                    "CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y", "CONFIG_BT_BLE_CCA_MODE_HW=y",
                    "CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y"]
        names = ["c3ble_probe", "c3bport_enter", "c3bport_leave", "c3bport_delay", "c3bport_exit",
                 "c3bport_capture", "c3bport_seal", "nimble_port_init"]
        nm = '\n'.join('42010000 T ' + name for name in names)
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            (work/'build').mkdir()
            (work/'build/uthome_runtime_port.elf').write_bytes(b'fixture')
            config = work/'sdkconfig'
            # ESP_WIFI_ENABLED is an invisible capability default, not a user
            # selection. Linked activation functions must still be absent.
            config.write_text('\n'.join(settings + ['CONFIG_ESP_WIFI_ENABLED=y']))
            with patch.object(qualification, 'run', return_value=nm):
                result = qualification.verify_ble_probe(work)
                self.assertFalse(result['wifi_activation_linked'])
                for setting in settings:
                    config.write_text('\n'.join(s for s in settings if s != setting))
                    with self.subTest(setting=setting), self.assertRaises(qualification.QualificationError):
                        qualification.verify_ble_probe(work)
            config.write_text('\n'.join(settings))
            for forbidden in ('esp_wifi_init', 'esp_wifi_start', 'esp_wifi_connect', 'c3wifi_probe',
                              'shim_ble_patch_modules_funcs', 'c3ble_native_patch', 'ke_event_schedule_safe'):
                with patch.object(qualification, 'run', return_value=nm + '\n42020000 T ' + forbidden):
                    with self.subTest(forbidden=forbidden), self.assertRaises(qualification.QualificationError):
                        qualification.verify_ble_probe(work)
            for missing in names:
                damaged = '\n'.join(line for line in nm.splitlines() if line.split()[-1] != missing)
                with patch.object(qualification, 'run', return_value=damaged):
                    with self.subTest(missing=missing), self.assertRaises(qualification.QualificationError):
                        qualification.verify_ble_probe(work)

    def test_gatt_probe_requires_explicit_isolated_ble_mode(self):
        for options in ({}, {"runtime_port": True, "static_net_queues": True},
                        {"runtime_port": True, "static_net_queues": True, "wifi_probe": True},
                        {"runtime_port": True, "static_net_queues": True, "model_probe": True},
                        {"runtime_port": True, "static_net_queues": True, "ble_probe": True,
                         "configuration_app": True}):
            with self.subTest(options=options), patch.object(qualification, "inspect") as inspect:
                with self.assertRaises(qualification.QualificationError):
                    build_with_flags(Path("kernel"), Path("sdk"), Path("unused"), ble_gatt_probe=True, **options)
                inspect.assert_not_called()

    def test_gatt_probe_requires_one_connection_and_linked_service(self):
        settings = ["CONFIG_FREERTOS_HZ=1000", "CONFIG_BT_ENABLED=y", "CONFIG_BT_NIMBLE_ENABLED=y",
                    "# CONFIG_BT_NIMBLE_NVS_PERSIST is not set",
                    "# CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE is not set",
                    "CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y", "CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1",
                    "# CONFIG_BT_NIMBLE_ROLE_CENTRAL is not set",
                    "# CONFIG_BT_NIMBLE_ROLE_OBSERVER is not set",
                    "CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y", "CONFIG_BT_BLE_CCA_MODE_HW=y",
                    "CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y"]
        names = ["c3ble_probe", "c3bport_enter", "c3bport_leave", "c3bport_delay", "c3bport_exit",
                 "c3bport_capture", "c3bport_seal", "nimble_port_init", "c3gatt_init",
                 "c3gatt_reads", "ble_gatts_add_svcs"]
        nm = '\n'.join('42010000 T ' + name for name in names)
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            (work/'build').mkdir()
            (work/'build/uthome_runtime_port.elf').write_bytes(b'fixture')
            config = work/'sdkconfig'
            config.write_text('\n'.join(settings))
            with patch.object(qualification, 'run', return_value=nm):
                self.assertTrue(qualification.verify_ble_probe(work, True)['read_only_gatt_diagnostic'])
                with self.assertRaises(qualification.QualificationError):
                    qualification.verify_ble_probe(work, True, True)
                config.write_text('\n'.join(settings+['# CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC is not set']))
                static = qualification.verify_ble_probe(work, True, True)
                self.assertTrue(static['static_host_experiment'])
                self.assertFalse(static['whole_system_static_qualified'])
                config.write_text('\n'.join(settings+['CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC=y']))
                with self.assertRaises(qualification.QualificationError):
                    qualification.verify_ble_probe(work, True, True)
                for setting in settings:
                    config.write_text('\n'.join(s for s in settings if s != setting))
                    with self.subTest(setting=setting), self.assertRaises(qualification.QualificationError):
                        qualification.verify_ble_probe(work, True)
            config.write_text('\n'.join(settings))
            for missing in names:
                damaged = '\n'.join(line for line in nm.splitlines() if line.split()[-1] != missing)
                with patch.object(qualification, 'run', return_value=damaged):
                    with self.subTest(missing=missing), self.assertRaises(qualification.QualificationError):
                        qualification.verify_ble_probe(work, True)
            settings = [s.replace('CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y',
                                 '# CONFIG_BT_NIMBLE_ROLE_PERIPHERAL is not set') for s in settings]
            config.write_text('\n'.join(settings))
            with patch.object(qualification, 'run', return_value=nm):
                with self.assertRaises(qualification.QualificationError):
                    qualification.verify_ble_probe(work)

    def test_static_ble_host_is_an_isolated_diagnostic_only(self):
        for options in ({}, {'runtime_port':True,'static_net_queues':True},
                        {'runtime_port':True,'static_net_queues':True,'wifi_probe':True},
                        {'runtime_port':True,'static_net_queues':True,'ble_probe':True,
                         'configuration_app':True}):
            with self.subTest(options=options), patch.object(qualification, 'inspect') as inspect:
                with self.assertRaises(qualification.QualificationError):
                    build_with_flags(Path('kernel'),Path('sdk'),Path('unused'),ble_static_host=True,**options)
                inspect.assert_not_called()

    def test_event_and_startup_link_proof_rejects_misdirection(self):
        path = ROOT/'tests/target/runtime_port_board/main/netqueue.cmake'
        import re
        pairs = re.findall(r'^\s*(\w+)=(c3event_\w+)\s*$', path.read_text(), re.MULTILINE)
        symbols = {'c3ewait_available': 0x40381000}
        for original, owned in pairs:
            symbols.setdefault(owned, 0x40382000 + len(symbols) * 32)
            symbols['__wrap_' + original] = symbols[owned]
        startup = [('__wrap_esp_startup_start_app', 'c3boot_start'),
                   ('knl_main', 'c3boot_native_main'),
                   ('knl_lowmem_top', 'gp_c3boot_begin'),
                   ('knl_lowmem_limit', 'gp_c3boot_end')]
        for original, owned in startup:
            symbols[original] = symbols[owned] = 0x42040000 + len(symbols) * 32
        self.assertEqual(len(qualification.check_event_symbols(symbols, path)), 11)
        for value in (0, 0x42010000):
            with self.subTest(cache_check=value), self.assertRaises(qualification.QualificationError):
                qualification.check_event_symbols(dict(symbols, c3ewait_available=value), path)
        for name in ['c3event_set_isr', '__wrap_xEventGroupWaitBits', *[pair[1] for pair in startup]]:
            for value in (0, 0x42010000, 0x40380100):
                with self.subTest(name=name, value=value), self.assertRaises(qualification.QualificationError):
                    qualification.check_event_symbols(dict(symbols, **{name: value}), path)

    def test_station_static_task_component_graph_is_shared_and_acyclic(self):
        observer = ROOT / "tools/target/patches/freertos-task-observer.patch"
        selection = qualification.resolve_mode(
            runtime_port=True,
            configuration_app=True,
            static_net_queues=True,
            network_services="wifi",
        )
        self.assertFalse(qualification.static_task_selection(selection, None))
        self.assertTrue(qualification.static_task_selection(selection, observer))
        combined = qualification.resolve_mode(
            runtime_port=True,
            configuration_app=True,
            static_net_queues=True,
            network_services="both",
        )
        with self.assertRaisesRegex(qualification.QualificationError,
                                    "single-radio static task pool"):
            qualification.static_task_selection(combined, observer)

        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            defaults = work / "source/examples/tasks/sdkconfig.defaults"
            defaults.parent.mkdir(parents=True)
            defaults.write_bytes(b'CONFIG_IDF_TARGET="esp32c3"\n')
            project, _ = qualification.stage_runtime_port(work)
            graph = qualification.verify_static_task_project(project)
            self.assertTrue(graph["acyclic"])
            self.assertEqual("uthome_task", graph["component"])
            self.assertEqual(["mtkernel"], graph["private_requires"])

            shim = work / "kernel/components/freertos_shim"
            shim.mkdir(parents=True)
            vendor = ROOT / "firmware/third_party/esp32c3-mtk3/components/freertos_shim"
            for name in ("CMakeLists.txt", "shim_task.c"):
                (shim / name).write_bytes((vendor / name).read_bytes())
            qualification.apply_patch(observer, work / "kernel")
            source = qualification.verify_static_task_source(
                shim / "shim_task.c", True)
            self.assertEqual("uthome_task", source["component_dependency"])

    def test_wifi_station_radio_commands_preserve_queue_header_isolation(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            commands = {
                (work / "project/hal" / name).resolve():
                    "cc -I/sdk/include -DUTHOME_NETWORK_WIFI=1 " + name
                for name in ("c3ble.c", "c3gatt.c", "c3gsvc.c")
            }
            result = qualification.verify_radio_include_isolation(commands, work)
            self.assertEqual(
                ["c3ble.c", "c3gatt.c", "c3gsvc.c"],
                result["isolated_sources"],
            )
            leaked = dict(commands)
            gatt = (work / "project/hal/c3gatt.c").resolve()
            leaked[gatt] += " -I" + (work / "source/include").as_posix()
            with self.assertRaisesRegex(qualification.QualificationError,
                                        "Native sys/queue.h namespace leaked"):
                qualification.verify_radio_include_isolation(leaked, work)

    def test_station_static_link_requires_operations_not_probe_snapshot(self):
        symbols = {
            "c3task_init", "c3task_stack_acquire", "c3task_stack_release",
            "c3task_reaper_bind", "c3task_reaper_task",
            "c3task_stack_commit", "c3task_seal", "g_reaper_stack",
            "shim_task_reap_one",
        }
        output = "\n".join(
            f"4200{index:04x} T {name}"
            for index, name in enumerate(sorted(symbols), 1)
        )
        with patch.object(qualification, "run", return_value=output):
            station = qualification.verify_static_task_link(
                Path("station"), True, False)
            self.assertFalse(station["observation_snapshot"])
            with self.assertRaisesRegex(qualification.QualificationError,
                                        "c3task_resource_snapshot"):
                qualification.verify_static_task_link(Path("probe"), True, True)
        output += "\n4200ffff T c3task_resource_snapshot"
        with patch.object(qualification, "run", return_value=output):
            probe = qualification.verify_static_task_link(
                Path("probe"), True, True)
            self.assertTrue(probe["observation_snapshot"])

    def test_runtime_sources_are_snapshotted_and_identified_by_hash(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            defaults = work / "source/examples/tasks/sdkconfig.defaults"
            defaults.parent.mkdir(parents=True)
            defaults.write_bytes(b"CONFIG_IDF_TARGET=\"esp32c3\"\n")
            project, sources = qualification.stage_runtime_port(work)
            generated_default = "firmware/third_party/esp32c3-mtk3/examples/tasks/sdkconfig.defaults"
            self.assertEqual(len(sources), len(qualification.RUNTIME_SOURCE_FILES) + 1)
            self.assertSetEqual(set(sources) - {generated_default}, set(qualification.RUNTIME_SOURCE_FILES))
            self.assertIn(generated_default, sources)
            for name in ('recinst.c', 'recinst.h'):
                self.assertIn(f'firmware/drivers/generic_i2c/{name}', sources)
            for name in ('stackmon', 'workcost', 'workmeter'):
                self.assertIn(f'firmware/src/runtime/{name}.c', sources)
                self.assertIn(f'firmware/src/runtime/{name}.h', sources)
            for name in ("firmware/hal/ports/esp32c3/c3task.c",
                         "firmware/hal/ports/esp32c3/c3task.h",
                         "firmware/hal/ports/esp32c3/c3start.c",
                         "firmware/hal/ports/esp32c3/c3start.h"):
                self.assertIn(name, sources)
            for name in ("hal/c3umem.c", "hal/c3umem.h", "main/uartcheck.c", "main/uartcheck.h"):
                self.assertTrue((project / name).is_file())
            for name in ("hal/c3task.c", "hal/c3task.h", "hal/c3start.c", "hal/c3start.h"):
                self.assertTrue((project / name).is_file())
            self.assertTrue((project / "components/uthome_task/CMakeLists.txt").is_file())
            self.assertTrue(qualification.verify_static_task_project(project)["acyclic"])
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
