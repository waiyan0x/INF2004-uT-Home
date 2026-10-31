"""Check linked fixed-RAM budgets; never infer runtime heap/stack qualification."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
WRITABLE = (".dram0.bss", ".dram0.data", ".noinit")
SUPPORTED_BUILD_MODES = ("configuration_application", "wifi_probe", "ble_probe")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def build_is_supported(evidence):
    return (evidence.get("build_status") == "passed"
            and any(evidence.get(mode) for mode in SUPPORTED_BUILD_MODES))


def analyze(files, summary, budget):
    reasons, modules = [], {}
    for name, entry in files.items():
        if ("/main/libmain.a:" not in name and "/uthome_radio/libuthome_radio.a:" not in name
                and not name.endswith(":cJSON.c.obj")):
            continue
        size = sum(part["size"] for section, part in
                   entry["memory_types"].get("DRAM", {}).get("sections", {}).items()
                   if section in WRITABLE)
        if not size:
            continue
        module = name.rsplit(":", 1)[-1]
        if module in modules:
            raise ValueError("Ambiguous object attribution: " + module)
        limit = budget["module_limits"].get(module)
        modules[module] = dict(bytes=size, budget=limit)
        if limit is None or size > limit:
            reasons.append("module_budget_exceeded_or_missing: " + module)
    if not modules:
        raise ValueError("No application objects found")
    if sum(budget["module_limits"].values()) > budget["application_fixed_limit"]:
        reasons.append("module_reservations_exceed_application_limit")
    layout = {item["name"]: item for item in summary["layout"]}
    dram = layout["DRAM"]
    writable = sum(dram["parts"].get(part, {}).get("size", 0) for part in (".bss", ".data", ".noinit"))
    application = sum(item["bytes"] for item in modules.values())
    other = writable - application
    code = dram["used"] - writable
    if min(other, code) < 0:
        raise ValueError("Inconsistent map attribution")
    groups = {"application_fixed": application, "kernel_sdk_fixed": other, "ram_code": code,
              "total_dram": dram["used"], "rtc": layout.get("RTC SLOW", {}).get("used", 0)}
    for group, size in groups.items():
        if size > budget[group + "_limit"]:
            reasons.append("group_budget_exceeded: " + group)
    reservations = sum(budget[group + "_limit"] for group in
                       ("application_fixed", "kernel_sdk_fixed", "ram_code"))
    if reservations > budget["total_dram_limit"] or budget["total_dram_limit"] > dram["total"]:
        reasons.append("reservation_exceeds_total_or_linker_capacity")
    return dict(contract="uthome.fixed-memory-budget", version="0.1.0", scope=budget["scope"],
                passed=not reasons, failures=reasons, modules=modules, groups=groups,
                budgets={key: value for key, value in budget.items() if key.endswith("_limit")},
                linker_dram_capacity=dram["total"], linker_unassigned_bytes=dram["free"],
                runtime_heap_measured=False, stack_high_water_measured=False,
                whole_system_static_qualified=False,
                note="Linker-unassigned bytes are not measured free heap. Kernel pool stacks are already counted within their static pool.")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--budget", type=Path, default=Path(__file__).with_name("static-budget.json"))
    args = parser.parse_args(argv)
    if args.output.exists():
        parser.error("Use a new output path; historical evidence is never overwritten")
    evidence = json.loads((args.build / "evidence.json").read_text())
    if not build_is_supported(evidence):
        parser.error("A passed configuration-application or isolated radio build is required")
    for name in ("build/uthome_runtime_port.map", "build/uthome_runtime_port.elf"):
        if digest(args.build / name) != evidence["artifacts"][name]["sha256"]:
            parser.error("Artifact hash mismatch: " + name)
    map_path = args.build / "build/uthome_runtime_port.map"
    command = [sys.executable, "-m", "esp_idf_size", "--format", "json2"]
    files = json.loads(subprocess.run(command + ["--files", str(map_path)], check=True,
                                      capture_output=True, text=True).stdout)
    summary = json.loads(subprocess.run(command + [str(map_path)], check=True,
                                        capture_output=True, text=True).stdout)
    result = analyze(files, summary, json.loads(args.budget.read_text()))
    result["evidence_sha256"] = digest(args.build / "evidence.json")
    result["budget_sha256"] = digest(args.budget)
    result["artifacts"] = {name: evidence["artifacts"][name] for name in
                           ("build/uthome_runtime_port.map", "build/uthome_runtime_port.elf")}
    result["size_files"] = files
    result["size_summary"] = summary
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(result, output, indent=2)
        output.write("\n")
    print(json.dumps({key: result[key] for key in ("passed", "failures", "groups", "budgets")}, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
