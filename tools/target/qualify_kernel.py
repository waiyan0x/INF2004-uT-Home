"""Build the pinned kernel example and record evidence; never flash.

Run from an activated, isolated ESP-IDF terminal. This tool does not install an
SDK, change Git state or qualify a physical board. An explicit compatibility
patch affects only the isolated source copy; the dependency remains pristine.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
KERNEL_REVISION = "ebef6428bea65e8fb7c1e0f935c5ecec925b3ca2"
SDK_REVISION = "fff9895c82d744c7237be8847347bdd1b07c6643"
COMPILER_PACKAGE = "esp-15.2.0_20251204"

RUNTIME_SOURCE_FILES = {
    "tests/target/runtime_port_board/main/uartcheck.c": "main/uartcheck.c",
    "tests/target/runtime_port_board/main/uartcheck.h": "main/uartcheck.h",
    "firmware/hal/ports/esp32c3/c3umem.c": "hal/c3umem.c",
    "firmware/hal/ports/esp32c3/c3umem.h": "hal/c3umem.h",
    "firmware/src/discovery/discuart.c": "discovery/discuart.c",
    "firmware/src/discovery/discuart.h": "discovery/discuart.h",
    "firmware/src/discovery/disci2c.c": "discovery/disci2c.c",
    "firmware/src/discovery/disci2c.h": "discovery/disci2c.h",
    "firmware/src/discovery/cfgdisc.c": "discovery/cfgdisc.c",
    "firmware/src/discovery/cfgdisc.h": "discovery/cfgdisc.h",
    "firmware/src/services/oledview.c": "services/oledview.c",
    "firmware/src/services/oledview.h": "services/oledview.h",
    "firmware/src/services/telemetry.c": "services/telemetry.c",
    "firmware/src/services/telemetry.h": "services/telemetry.h",

    "firmware/src/services/statusmon.c": "services/statusmon.c",
    "firmware/src/services/statusmon.h": "services/statusmon.h",

    "firmware/src/services/sampcache.c": "services/sampcache.c",
    "firmware/src/services/sampcache.h": "services/sampcache.h",

    "firmware/src/config/cfgpump.c": "config/cfgpump.c",
    "firmware/src/config/cfgpump.h": "config/cfgpump.h",
    "firmware/hal/ports/esp32c3/c3usb.c": "hal/c3usb.c",
    "firmware/hal/ports/esp32c3/c3usb.h": "hal/c3usb.h",
    "firmware/hal/ports/esp32c3/c3store.c": "hal/c3store.c",
    "firmware/hal/ports/esp32c3/c3store.h": "hal/c3store.h",
    "firmware/app/c3app.c": "app/c3app.c",
    "firmware/app/c3app.h": "app/c3app.h",
    "tests/target/runtime_port_board/configuration.defaults": "configuration.defaults",
    "tests/target/runtime_port_board/configuration.csv": "configuration.csv",
    "firmware/src/config/jsonscan.c": "config/jsonscan.c",
    "firmware/src/config/jsonscan.h": "config/jsonscan.h",
    "firmware/src/config/cfgjson.c": "config/cfgjson.c",
    "firmware/src/config/cfgjson.h": "config/cfgjson.h",
    "firmware/src/config/cfgplan.c": "config/cfgplan.c",
    "firmware/src/config/cfgplan.h": "config/cfgplan.h",
    "firmware/src/config/cfgdev.c": "config/cfgdev.c",
    "firmware/src/config/cfgdev.h": "config/cfgdev.h",
    "firmware/src/config/cfgrule.c": "config/cfgrule.c",
    "firmware/src/config/cfgrule.h": "config/cfgrule.h",
    "firmware/src/automation/rulecalc.c": "automation/rulecalc.c",
    "firmware/src/automation/rulecalc.h": "automation/rulecalc.h",
    "firmware/src/config/cfgbind.c": "config/cfgbind.c",
    "firmware/src/config/cfgbind.h": "config/cfgbind.h",
    "firmware/hal/ports/esp32c3/c3bind.c": "hal/c3bind.c",
    "firmware/hal/ports/esp32c3/c3bind.h": "hal/c3bind.h",
    "firmware/third_party/cjson/cJSON.c": "third_party/cjson/cJSON.c",
    "firmware/third_party/cjson/cJSON.h": "third_party/cjson/cJSON.h",
    "firmware/third_party/cjson/LICENSE": "third_party/cjson/LICENSE",
    "firmware/third_party/cjson/SOURCE.json": "third_party/cjson/SOURCE.json",
    "tests/target/runtime_port_board/main/idf_component.yml": "main/idf_component.yml",
    "tests/target/runtime_port_board/parser.defaults": "parser.defaults",

    "firmware/src/config/cfgbytes.c": "config/cfgbytes.c",
    "firmware/src/config/cfgbytes.h": "config/cfgbytes.h",
    "firmware/src/config/cfgwire.c": "config/cfgwire.c",
    "firmware/src/config/cfgwire.h": "config/cfgwire.h",
    "firmware/src/config/cfgstore.c": "config/cfgstore.c",
    "firmware/src/config/cfgstore.h": "config/cfgstore.h",
    "firmware/src/config/cfgctrl.c": "config/cfgctrl.c",
    "firmware/src/config/cfgctrl.h": "config/cfgctrl.h",
    "firmware/hal/include/pulseio.h": "hal/include/pulseio.h",
    "firmware/hal/pulseio.c": "hal/pulseio.c",
    "firmware/hal/include/pwmio.h": "hal/include/pwmio.h",
    "firmware/hal/pwmio.c": "hal/pwmio.c",
    "firmware/hal/include/digiin.h": "hal/include/digiin.h",
    "firmware/hal/ports/esp32c3/c3pulse.c": "hal/c3pulse.c",
    "firmware/hal/ports/esp32c3/c3pulse.h": "hal/c3pulse.h",
    "firmware/hal/ports/esp32c3/c3pwm.c": "hal/c3pwm.c",
    "firmware/hal/ports/esp32c3/c3pwm.h": "hal/c3pwm.h",
    "firmware/drivers/hc_sr04/hc_sr04.c": "drivers/hc_sr04.c",
    "firmware/drivers/hc_sr04/hc_sr04.h": "drivers/hc_sr04.h",
    "firmware/drivers/hc_sr04/hcinst.c": "drivers/hcinst.c",
    "firmware/drivers/hc_sr04/hcinst.h": "drivers/hcinst.h",
    "firmware/drivers/pwm_output/pwminst.c": "drivers/pwminst.c",
    "firmware/drivers/pwm_output/pwminst.h": "drivers/pwminst.h",
    "firmware/hal/include/uartio.h": "hal/include/uartio.h",
    "firmware/hal/uartio.c": "hal/uartio.c",
    "firmware/hal/ports/esp32c3/c3uart.h": "hal/c3uart.h",
    "firmware/hal/ports/esp32c3/c3uart.c": "hal/c3uart.c",
    "firmware/drivers/hlk_ld2450/ldparse.c": "drivers/ldparse.c",
    "firmware/drivers/hlk_ld2450/ldparse.h": "drivers/ldparse.h",
    "firmware/drivers/hlk_ld2450/ld2450.c": "drivers/ld2450.c",
    "firmware/drivers/hlk_ld2450/ld2450.h": "drivers/ld2450.h",
    "firmware/drivers/hlk_ld2450/ldinst.c": "drivers/ldinst.c",
    "firmware/drivers/hlk_ld2450/ldinst.h": "drivers/ldinst.h",

    "tests/target/runtime_port_board/CMakeLists.txt": "CMakeLists.txt",
    "tests/target/runtime_port_board/main/CMakeLists.txt": "main/CMakeLists.txt",
    "tests/target/runtime_port_board/main/main.c": "main/main.c",
    "tests/target/runtime_port_board/main/main.h": "main/main.h",
    "firmware/src/runtime/tkport.h": "runtime/tkport.h",
    "firmware/src/runtime/tkport.c": "runtime/tkport.c",
    "firmware/src/runtime/rt_port.h": "runtime/rt_port.h",
    "firmware/src/runtime/workport.h": "runtime/workport.h",
    "firmware/include/uthome/runtime.h": "include/uthome/runtime.h",
    "firmware/include/uthome/component.h": "include/uthome/component.h",
    "firmware/src/runtime/activate.c": "runtime/activate.c",
    "firmware/src/runtime/activate.h": "runtime/activate.h",
    "firmware/src/runtime/shared.c": "runtime/shared.c",
    "firmware/src/runtime/shared.h": "runtime/shared.h",
    "firmware/src/runtime/component.c": "runtime/component.c",
    "firmware/src/messaging/msgbus.c": "messaging/msgbus.c",
    "firmware/src/health/health.c": "health/health.c",
    "firmware/src/health/watchdog.c": "health/watchdog.c",
    "firmware/include/uthome/msgbus.h": "include/uthome/msgbus.h",
    "firmware/include/uthome/health.h": "include/uthome/health.h",
    "firmware/include/uthome/watchdog.h": "include/uthome/watchdog.h",
    "firmware/include/uthome/sample.h": "include/uthome/sample.h",
    "firmware/include/uthome/drvreg.h": "include/uthome/drvreg.h",
    "firmware/src/config/drvreg.c": "config/drvreg.c",
    "firmware/hal/include/i2cbus.h": "hal/include/i2cbus.h",
    "firmware/hal/i2cbus.c": "hal/i2cbus.c",
    "firmware/hal/include/i2cown.h": "hal/include/i2cown.h",
    "firmware/hal/i2cown.c": "hal/i2cown.c",
    "firmware/hal/include/pinpool.h": "hal/include/pinpool.h",
    "firmware/hal/pinpool.c": "hal/pinpool.c",
    "firmware/hal/include/digiout.h": "hal/include/digiout.h",
    "firmware/hal/include/spibus.h": "hal/include/spibus.h",
    "firmware/hal/spibus.c": "hal/spibus.c",
    "firmware/hal/include/spiown.h": "hal/include/spiown.h",
    "firmware/hal/spiown.c": "hal/spiown.c",
    "firmware/hal/ports/esp32c3/c3pin.h": "hal/c3pin.h",
    "firmware/hal/ports/esp32c3/c3pin.c": "hal/c3pin.c",
    "firmware/hal/ports/esp32c3/c3spi.h": "hal/c3spi.h",
    "firmware/hal/ports/esp32c3/c3spi.c": "hal/c3spi.c",
    "firmware/drivers/ssd1306_spi/ssd1306.h": "drivers/ssd1306.h",
    "firmware/drivers/ssd1306_spi/ssd1306.c": "drivers/ssd1306.c",
    "firmware/drivers/ssd1306_spi/oledinst.h": "drivers/oledinst.h",
    "firmware/drivers/ssd1306_spi/oledinst.c": "drivers/oledinst.c",
    "firmware/hal/ports/esp32c3/c3i2c.h": "hal/c3i2c.h",
    "firmware/hal/ports/esp32c3/c3i2c.c": "hal/c3i2c.c",
    "firmware/drivers/bmp388/bmp388.h": "drivers/bmp388.h",
    "firmware/drivers/bmp388/bmp388.c": "drivers/bmp388.c",
    "firmware/drivers/bmp388/bmpinst.h": "drivers/bmpinst.h",
    "firmware/drivers/bmp388/bmpinst.c": "drivers/bmpinst.c",
    "firmware/third_party/bmp3/bmp3.h": "third_party/bmp3/bmp3.h",
    "firmware/third_party/bmp3/bmp3.c": "third_party/bmp3/bmp3.c",
    "firmware/third_party/bmp3/bmp3_defs.h": "third_party/bmp3/bmp3_defs.h",
}


class QualificationError(RuntimeError):
    pass


def check_compiler_flags(command, expected_standard):
    """Check effective language and warning-error options, not CMake intent."""
    standards = re.findall(r"(?:^|\s)-std=([^\s]+)", command)
    if not standards or standards[-1] != expected_standard:
        raise QualificationError(f"Effective C standard must be {expected_standard}")
    flags = re.findall(r"(?:^|\s)(-W[^\s]+|-w)(?=\s|$)", command)
    if "-w" in flags:
        raise QualificationError("Compiler warnings are disabled")
    for enabled, disabled in (("-Wall", "-Wno-all"), ("-Wextra", "-Wno-extra"),
                              ("-Werror", "-Wno-error")):
        relevant = [flag for flag in flags if flag in (enabled, disabled)]
        if not relevant or relevant[-1] != enabled:
            raise QualificationError(f"Effective compiler profile requires {enabled}")
    overrides = {}
    for flag in flags:
        if flag.startswith("-Wno-error="):
            overrides[flag.split("=", 1)[1]] = False
        elif flag.startswith("-Werror="):
            overrides[flag.split("=", 1)[1]] = True
    if not all(overrides.values()):
        raise QualificationError("SDK warning-error exemptions were not overridden")


def verify_uart_storage(commands, source_root):
    """Fail closed if scoped native allocator substitutions are absent/leak."""
    expected = {
        (source_root / "device/ser/ser.c").resolve(): "serial",
        (source_root / "device/common/drvif/msdrvif.c").resolve(): "record",
    }
    for path, role in expected.items():
        command = commands.get(path)
        if command is None:
            raise QualificationError(f"Missing native UART compiler command: {path}")
        for original, suffix in (("Kmalloc", "alloc"), ("Kfree", "free")):
            values = re.findall(r'(?:^|\s)-D\s*"?' + original + r'=([A-Za-z_]\w*)"?', command)
            if values != [f"c3umem_{role}_{suffix}"]:
                raise QualificationError(f"Invalid UART storage substitution: {path}: {original}")
    for path, command in commands.items():
        if path not in expected and re.search(r'-D\s*"?K(?:malloc|free)=c3umem_', command):
            raise QualificationError(f"UART storage substitution leaked to {path}")
    return {str(path): role for path, role in expected.items()}


def verify_uart_fault_hook(commands, project):
    """Keep the read-failure hook confined to the acceptance image."""
    main = commands.get((project / "main/main.c").resolve(), "")
    modes = re.findall(r'(?:^|\s)-D\s*"?UTHOME_CONFIGURATION_APP=([01])"?', main)
    if len(modes) != 1:
        raise QualificationError("Missing or ambiguous application build mode")
    application = modes[0] == "1"
    uart = commands.get((project / "hal/c3uart.c").resolve(), "")
    hooks = re.findall(r'(?:^|\s)-D\s*"?tk_srea_dev=([A-Za-z_]\w*)"?', uart)
    if hooks != ([] if application else ["uartcheck_read"]):
        raise QualificationError("Invalid UART fault-injection build scope")
    test_present = (project / "main/uartcheck.c").resolve() in commands
    if test_present == application:
        raise QualificationError("UART acceptance source does not match build mode")
    return application


def verify_compile_profiles(work):
    """Verify every snapshotted owned C file's generated compiler command."""
    database = work / "build/compile_commands.json"
    entries = json.loads(database.read_text(encoding="utf-8"))
    commands = {Path(entry["file"]).resolve(): entry["command"] for entry in entries}
    policy = json.loads((ROOT / "tools/c_standard/policy.json").read_text(encoding="utf-8"))
    application = verify_uart_fault_hook(commands, work / "project")
    profiles = {}
    for source, destination in RUNTIME_SOURCE_FILES.items():
        if not source.endswith(".c") or "/third_party/" in source:
            continue
        expected = "c11" if source in policy["atomic_profile_paths"] else "c99"
        # Pinned SDK LL headers require GNU typeof; owned adapter syntax is C99.
        if source in {"firmware/hal/ports/esp32c3/c3i2c.c",
                      "firmware/hal/ports/esp32c3/c3pin.c",
                      "firmware/hal/ports/esp32c3/c3spi.c",
                      "firmware/hal/ports/esp32c3/c3pulse.c",
                      "firmware/hal/ports/esp32c3/c3pwm.c"}:
            expected = "gnu99"
        command = commands.get((work / "project" / destination).resolve())
        if destination == "main/uartcheck.c" and application:
            continue
        if command is None:
            raise QualificationError(f"Missing compiler command for {source}")
        check_compiler_flags(command, expected)
        profiles[source] = expected
    storage = verify_uart_storage(commands, work / "source")
    return {"status": "passed", "profiles": profiles, "uart_storage": storage,
            "compile_commands_sha256": sha256(database)}


def run(arguments, cwd=None):
    result = subprocess.run([str(arg) for arg in arguments], cwd=cwd,
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode:
        raise QualificationError(f"Command failed ({result.returncode}): {arguments[0]}\n"
                                 + result.stdout + result.stderr)
    return result.stdout.strip()


def check_c_standard():
    """Run the mandatory repository gate before creating a build workspace."""
    command = [sys.executable, str(ROOT / "tools/c_standard/check.py"),
               "--root", str(ROOT)]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    if result.returncode:
        raise QualificationError("C coding-standard gate failed before build:\n"
                                 + result.stdout + result.stderr)
    return {"status": "passed", "command": command, "output": result.stdout.strip()}


def pinned_checkout(path, expected):
    revision = run(["git", "-C", path, "rev-parse", "HEAD"])
    if revision != expected:
        raise QualificationError(f"Unexpected revision at {path}: {revision}; expected {expected}")
    dirty = run(["git", "-C", path, "status", "--porcelain", "--untracked-files=normal"])
    if dirty:
        raise QualificationError(f"Checkout must be pristine: {path}\n{dirty}")
    return revision


def no_spaces(path):
    if any(char.isspace() for char in str(path)):
        raise QualificationError(f"ESP-IDF build and SDK paths must contain no whitespace: {path}")


def inspect(kernel, sdk):
    no_spaces(sdk)
    compiler = shutil.which("riscv32-esp-elf-gcc")
    if not compiler:
        raise QualificationError("Activate ESP-IDF first: riscv32-esp-elf-gcc is missing from PATH")
    if not (sdk / "tools/idf.py").is_file():
        raise QualificationError(f"ESP-IDF is missing at {sdk}")
    versions = {"kernel_revision": pinned_checkout(kernel, KERNEL_REVISION),
                "sdk_revision": pinned_checkout(sdk, SDK_REVISION)}
    submodules = run(["git", "-C", sdk, "submodule", "status", "--recursive"])
    if any(line and line[0] in "-+U" for line in submodules.splitlines()):
        raise QualificationError("SDK submodules are missing or differ from their pinned revisions")
    compiler_version = run([compiler, "--version"]).splitlines()[0]
    if COMPILER_PACKAGE not in compiler_version:
        raise QualificationError(f"Expected compiler package {COMPILER_PACKAGE}, found {compiler_version}")
    if run([compiler, "-dumpmachine"]) != "riscv32-esp-elf":
        raise QualificationError("Compiler does not target riscv32-esp-elf")
    run([sys.executable, sdk / "tools/idf_tools.py", "--idf-path", sdk, "check"])
    versions.update(compiler=compiler_version,
                    idf_version=run([sys.executable, sdk / "tools/idf.py", "--version"]),
                    python=sys.version.split()[0],
                    cmake=run(["cmake", "--version"]).splitlines()[0],
                    ninja=run(["ninja", "--version"]))
    return versions


def apply_patch(patch, source):
    """Check, then apply, one staged patch identically on every host.

    The pinned kernel stores config/config_device.h with CRLF endings, while the
    reviewed patches are LF. Windows Git (core.autocrlf=true) matched them, which
    is how the recorded builds succeeded; Linux CI (unset) rejected the same
    bytes. Pin the setting so every host reproduces the recorded behaviour.
    """
    command = ["git", "-c", "core.autocrlf=true", "apply"]
    run(command + ["--check", patch], cwd=source)
    run(command + [patch], cwd=source)


def unpack_source(archive_path, destination):
    """Reject escapes and symlinks before extracting a Git source archive."""
    with zipfile.ZipFile(archive_path) as archive:
        for entry in archive.infolist():
            name = PurePosixPath(entry.filename)
            if name.is_absolute() or ".." in name.parts or "\\" in entry.filename or ":" in entry.filename:
                raise QualificationError(f"Unsafe source archive entry: {entry.filename}")
            if stat.S_ISLNK(entry.external_attr >> 16):
                raise QualificationError(f"Unexpected source symlink: {entry.filename}")
        archive.extractall(destination)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build_environment(sdk):
    environment = os.environ.copy()
    # Upstream CMake interpolates this environment path into CMake strings.
    # Backslashes such as \U are escapes there, so normalize just this process.
    environment["IDF_PATH"] = sdk.as_posix()
    environment["PYTHONUNBUFFERED"] = "1"
    environment["PYTHONUTF8"] = "1"
    return environment


def verify_runtime_snapshot(project, root=None):
    """Reject changed originals or copied build inputs before target compilation."""
    root = ROOT if root is None else root
    project = project.resolve()
    root = root.resolve()
    manifest_path = project / "source-manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise QualificationError(f"Missing or invalid source manifest: {manifest_path}") from exc
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        raise QualificationError("Unsupported source manifest schema; create a fresh qualification build")
    sources = manifest.get("sources")
    if not isinstance(sources, dict) or set(sources) != set(RUNTIME_SOURCE_FILES):
        raise QualificationError("Source manifest does not contain the exact required project inputs")
    for source, destination in RUNTIME_SOURCE_FILES.items():
        entry = sources[source]
        if (not isinstance(entry, dict) or entry.get("staged_path") != destination
                or not isinstance(entry.get("sha256"), str)
                or len(entry["sha256"]) != 64
                or any(char not in "0123456789abcdef" for char in entry["sha256"])):
            raise QualificationError(f"Invalid source manifest entry: {source}")
        for label, base, relative in (("original", root, source),
                                      ("staged", project, destination)):
            path = base / relative
            if not path.resolve().is_relative_to(base) or not path.is_file():
                raise QualificationError(f"Missing or escaped {label} project input: {path}")
            if sha256(path) != entry["sha256"]:
                raise QualificationError(
                    f"Changed {label} project input: {path}; create a fresh qualification build")
    return manifest_path


def stage_runtime_port(work):
    """Snapshot project-owned adapter/tests beside the isolated kernel source."""
    project = work / "project"
    sources = {}
    for source, destination in RUNTIME_SOURCE_FILES.items():
        target = project / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((ROOT / source).read_bytes())
        sources[source] = {"staged_path": f"project/{destination}", "sha256": sha256(target)}
    manifest = {"schema_version": 1, "sources": {
        source: {"staged_path": destination, "sha256": sources[source]["sha256"]}
        for source, destination in RUNTIME_SOURCE_FILES.items()}}
    (project / "source-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    defaults = project / "sdkconfig.defaults"
    defaults.write_bytes((work / "source/examples/tasks/sdkconfig.defaults").read_bytes())
    sources["firmware/third_party/esp32c3-mtk3/examples/tasks/sdkconfig.defaults"] = {
        "staged_path": "project/sdkconfig.defaults", "sha256": sha256(work / "source/examples/tasks/sdkconfig.defaults")}
    return project, sources


def build(kernel, sdk, work, compat_patch=None, runtime_port=False, resource_patch=None, configuration_app=False):
    if configuration_app and not runtime_port:
        raise QualificationError("configuration application requires --runtime-port")
    no_spaces(work)
    if kernel == work or kernel in work.parents or sdk == work or sdk in work.parents:
        raise QualificationError("Build output must be outside the kernel and SDK checkouts")
    # Never clean, overwrite or reuse an existing build directory.
    if work.exists():
        raise QualificationError(f"Use a new work directory; destination already exists: {work}")
    standard = check_c_standard()
    versions = inspect(kernel, sdk)
    work.mkdir(parents=True, exist_ok=False)
    evidence = {"schema_version": "1.0.0", "created_utc": datetime.now(timezone.utc).isoformat(),
                "example": "tests/target/runtime_port_board" if runtime_port else "examples/tasks",
                "configuration_application": configuration_app,
                "board_kit": "iote_esp32c3_mini", "target": "esp32c3",
                "versions": versions, "build_status": "in_progress", "board_status": "not_tested",
                "application_runtime_status": "not_tested", "artifacts": {},
                "coding_standard": standard}
    report = work / "evidence.json"
    report.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    try:
        archive = work / "kernel-source.zip"
        run(["git", "-C", kernel, "archive", "--format=zip", f"--output={archive}", KERNEL_REVISION])
        unpack_source(archive, work / "source")
        evidence["kernel_archive_sha256"] = sha256(archive)
        evidence["compatibility_patch"] = None
        if compat_patch is not None:
            staged_patch = work / "kernel-compatibility.patch"
            staged_patch.write_bytes(compat_patch.read_bytes())
            evidence["compatibility_patch"] = {"name": compat_patch.name, "sha256": sha256(staged_patch)}
            apply_patch(staged_patch, work / "source")
        evidence["resource_patch"] = None
        if resource_patch is not None:
            staged_patch = work / "kernel-resources.patch"
            staged_patch.write_bytes(resource_patch.read_bytes())
            evidence["resource_patch"] = {"name": resource_patch.name, "sha256": sha256(staged_patch)}
            apply_patch(staged_patch, work / "source")
        project = work / "source/examples/tasks"
        image_name = "mtk3c3_tasks"
        if runtime_port:
            project, evidence["project_sources"] = stage_runtime_port(work)
            manifest = verify_runtime_snapshot(project)
            evidence["source_manifest_sha256"] = sha256(manifest)
            image_name = "uthome_runtime_port"
        command = [sys.executable, str(sdk / "tools/idf.py"), "-C", str(project), "-B", str(work / "build"),
                   "-DIDF_TARGET=esp32c3", "-DMTK_BOARD=iote_esp32c3_mini",
                   f"-DSDKCONFIG={work / 'sdkconfig'}", "build"]
        if runtime_port:
            # Every direct rebuild also checks the source repository. Paths are
            # separate arguments, including the source checkout with spaces.
            command[-1:-1] = [f"-DUTHOME_CONFIGURATION_APP={int(configuration_app)}",
                              f"-DUTHOME_SOURCE_ROOT={ROOT.as_posix()}",
                              f"-DUTHOME_CHECK_PYTHON={Path(sys.executable).as_posix()}"]
        evidence["command"] = command
        environment = build_environment(sdk)
        evidence["environment_overrides"] = {name: environment[name] for name in
                                              ("IDF_PATH", "PYTHONUNBUFFERED", "PYTHONUTF8")}
        with (work / "build.log").open("w", encoding="utf-8") as log:
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       text=True, encoding="utf-8", errors="replace", env=environment)
            for line in process.stdout:
                log.write(line)
                log.flush()
                print(line, end="", flush=True)
            result = process.wait()
        evidence["build_exit_code"] = result
        if result:
            raise QualificationError(f"Kernel build failed ({result}); inspect {work / 'build.log'}")
        if runtime_port:
            evidence["compiler_profiles"] = verify_compile_profiles(work)

            dependency_lock = project / "dependencies.lock"
            if not dependency_lock.is_file():
                raise QualificationError("Missing managed-component dependency lock")
            import yaml
            lock = yaml.safe_load(dependency_lock.read_text(encoding="utf-8"))
            dependency = lock.get("dependencies", {}).get("espressif/cjson", {})
            if dependency.get("version") != "1.7.19~2":
                raise QualificationError("Unexpected cJSON managed-component version")
            managed = project / "managed_components/espressif__cjson/cJSON"
            hashes = {}
            for name in ("cJSON.c", "cJSON.h", "LICENSE"):
                if sha256(managed / name) != sha256(ROOT / "firmware/third_party/cjson" / name):
                    raise QualificationError(f"Host/target cJSON bytes differ: {name}")
                hashes[name] = sha256(managed / name)
            evidence["managed_cjson"] = {
                "version": dependency["version"], "component_hash": dependency.get("component_hash"),
                "source_sha256": hashes, "lock_sha256": sha256(dependency_lock)}
        # A successful process with incomplete artifacts is not build evidence.
        for relative in (f"build/{image_name}.bin", f"build/{image_name}.elf",
                         f"build/{image_name}.map", "build/compile_commands.json",
                         "build/bootloader/bootloader.bin", "build/partition_table/partition-table.bin",
                         "build/flasher_args.json", "sdkconfig", "build.log"):
            artifact = work / relative
            if not artifact.is_file() or not artifact.stat().st_size:
                raise QualificationError(f"Missing build artifact: {relative}")
            evidence["artifacts"][relative] = {"sha256": sha256(artifact), "size_bytes": artifact.stat().st_size}

        if runtime_port:
            for relative in ("project/dependencies.lock", "project/sdkconfig.defaults"):
                artifact = work / relative
                evidence["artifacts"][relative] = {
                    "sha256": sha256(artifact), "size_bytes": artifact.stat().st_size}
        evidence["build_status"] = "passed"
    except (Exception, KeyboardInterrupt) as exc:
        evidence["build_status"] = "failed"
        evidence["error"] = str(exc) or "Interrupted"
        raise
    finally:
        report.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("inspect", "build", "verify-snapshot"))
    parser.add_argument("--kernel", type=Path, default=ROOT / "firmware/third_party/esp32c3-mtk3")
    parser.add_argument("--sdk", type=Path, default=Path(os.environ["IDF_PATH"]) if "IDF_PATH" in os.environ else None)
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--project-dir", type=Path, help="staged project for verify-snapshot")
    parser.add_argument("--compat-patch", type=Path, help="explicit reviewed build compatibility patch; recorded by hash")
    parser.add_argument("--resource-patch", type=Path, help="explicit reviewed kernel resource patch; isolated and hashed")
    parser.add_argument("--runtime-port", action="store_true",
                        help="build the project task-port acceptance test instead of the upstream example")
    parser.add_argument("--configuration-app", action="store_true",
                        help="run USB configuration application; requires --runtime-port")
    args = parser.parse_args(argv)
    if args.action == "verify-snapshot":
        if args.project_dir is None:
            parser.error("verify-snapshot requires --project-dir")
        try:
            verify_runtime_snapshot(args.project_dir)
        except (QualificationError, OSError) as exc:
            print(str(exc), file=sys.stderr)
            return 1
        print("Project snapshot and original inputs match the qualification manifest.")
        return 0
    if args.sdk is None:
        parser.error("activate ESP-IDF or supply --sdk")
    if args.action == "build" and args.work_dir is None:
        parser.error("build requires --work-dir (a new path without whitespace)")
    try:
        kernel, sdk = args.kernel.resolve(), args.sdk.resolve()
        if args.action == "inspect":
            print(json.dumps(inspect(kernel, sdk), indent=2))
        else:
            report = build(kernel, sdk, args.work_dir.resolve(),
                           args.compat_patch.resolve() if args.compat_patch else None, args.runtime_port,
                           args.resource_patch.resolve() if args.resource_patch else None, args.configuration_app)
            print(f"Build evidence: {report}\nPhysical board and application runtime remain untested.")
    except (QualificationError, OSError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
