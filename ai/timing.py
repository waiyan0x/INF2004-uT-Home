"""Prepare reproducible interval experiments offline; never access a board."""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys

from tools.config_validator.planner import (
    Catalogue, ConfigurationError, ROOT, build_plan, check, load_document, require,
)


VERSION = "0.1.0"
SCHEMA = ROOT / "ai/contracts/timing-scenario.schema.json"
MAX_EXPERIMENT_BYTES = 262144


def canonical(value):
    """Stable JSON bytes for correlation, independent of source-file formatting."""
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False).encode("ascii")


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def integer_numbers(value):
    # JSON Schema considers 500.0 an integer; the firmware requires integer tokens.
    require(not isinstance(value, float), "Numbers must use integer JSON tokens")
    if isinstance(value, dict):
        for item in value.values():
            integer_numbers(item)
    elif isinstance(value, list):
        for item in value:
            integer_numbers(item)


def variant(profile, instance_id, field, interval):
    candidate = deepcopy(profile)
    device = next(item for item in candidate["devices"] if item["device_id"] == instance_id)
    device["behaviour"][field] = interval
    return candidate


def prepare(profile, manifest, scenario, catalogue=None):
    """Change one instance's period only; validate every full candidate profile."""
    catalogue = catalogue or Catalogue()
    integer_numbers(profile)
    integer_numbers(manifest)
    integer_numbers(scenario)
    check(scenario, load_document(SCHEMA), "timing scenario")
    require(isinstance(manifest.get("build_id"), str) and 0 < len(manifest["build_id"]) <= 128,
            "An explicit build_id is required")
    baseline = build_plan(profile, manifest, catalogue)
    devices = {device["device_id"]: device for device in profile["devices"]}
    require(scenario["instance_id"] in devices, "Unknown target instance")
    device = devices[scenario["instance_id"]]
    require(device["enabled"], "The target instance must already be enabled")
    reference = device["descriptor"]
    descriptor = catalogue.descriptors[(reference["id"], reference["version"])]
    execution, routing = descriptor["execution"], descriptor["routing"]
    field = execution["period_field"]
    require(field is not None and execution["kind"] != "stream",
            "Stream devices do not expose a tunable reading interval")
    if execution["kind"] == "periodic" and routing["output_type"] is not None:
        metric = "observed_sample_age_ms"
    elif execution["kind"] == "queue_consumer" and routing["input_types"]:
        metric = "visible_update_delay_ms"
    else:
        raise ConfigurationError("Only reading and display intervals are in scope; no actuator tuning")
    require(scenario["goal"]["metric"] == metric, "Goal metric does not match the target capability")
    # Changing a sensor's timing can also change a rule-driven motor's behaviour.
    require(not any(devices[rule["target"]]["enabled"] for rule in profile.get("rules", [])),
            "Timing experiments require rule-controlled outputs to be disabled in the input profile")
    bounds = descriptor["instance_schema"]["properties"]["behaviour"]["properties"].get(field, {})
    require(bounds.get("type") == "integer" and type(bounds.get("minimum")) is int
            and type(bounds.get("maximum")) is int and 0 < bounds["minimum"] <= bounds["maximum"],
            "Descriptor must declare a bounded integer period in milliseconds")
    current = device["behaviour"][field]
    default = descriptor["defaults"].get("behaviour", {}).get(field)
    require(type(default) is int, "Descriptor requires an explicit default interval for comparison")
    intervals = sorted(set(scenario["candidate_intervals_ms"] + [current, default]))
    require(len(intervals) >= 2, "An experiment needs at least two distinct intervals")
    require(scenario["duration_ms"] >= 10 * max(intervals),
            "Observation duration must cover at least ten periods of the slowest trial")
    trials = []
    for interval in intervals:
        candidate = variant(profile, device["device_id"], field, interval)
        build_plan(candidate, manifest, catalogue)
        require(len(canonical(candidate)) <= 8192, "Compact candidate profile exceeds 8192 bytes")
        roles = []
        if interval == current:
            roles.append("current_profile")
        if interval == default:
            roles.append("descriptor_default")
        if interval in scenario["candidate_intervals_ms"]:
            roles.append("candidate")
        trials.append(dict(interval_ms=interval, roles=roles, profile_sha256=digest(candidate)))
    board = catalogue.boards[tuple(profile["board"][key] for key in
                                  ("platform", "variant", "capability_version"))]
    references = sorted({(dev["descriptor"]["id"], dev["descriptor"]["version"])
                         for dev in profile["devices"]})
    policy = load_document(catalogue.root / "catalogue/discovery.json")
    provenance = {
        "profile_sha256": digest(profile), "build_manifest_sha256": digest(manifest),
        "scenario_sha256": digest(scenario), "node_schema_sha256": digest(catalogue.node_schema),
        "descriptor_schema_sha256": digest(load_document(catalogue.root / "schemas/device-descriptor.schema.json")),
        "board": {**profile["board"], "sha256": digest(board)},
        "descriptors": [dict(id=key[0], version=key[1], sha256=digest(catalogue.descriptors[key]))
                        for key in references],
        "discovery_policy": dict(version=policy["catalogue_version"], sha256=digest(policy)),
        "implementation_sha256": {
            path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
            for path in ("ai/timing.py", "ai/contracts/timing-scenario.schema.json",
                         "tools/config_validator/planner.py")
        },
    }
    return deepcopy({
        "contract": "uthome.timing-experiment", "contract_version": VERSION,
        "stage": "planned", "measurement_origin": "none", "training_label": None,
        "identity_evidence": "not_collected", "actual_firmware_sha256": None,
        "scenario": scenario, "profile": profile, "build_manifest": manifest,
        "target": dict(instance_id=device["device_id"], descriptor=reference,
                       protocol=descriptor["protocol"], execution_kind=execution["kind"],
                       period_field=field, minimum_ms=bounds["minimum"], maximum_ms=bounds["maximum"]),
        "resources": baseline["resources"], "provenance": provenance, "trials": trials,
    })


def validate(experiment, catalogue=None):
    """Recompute against reviewed sources; reject drift, extra fields and fake labels."""
    require(isinstance(experiment, dict), "Experiment must be an object")
    require(experiment.get("contract") == "uthome.timing-experiment"
            and experiment.get("contract_version") == VERSION, "Unsupported timing experiment contract")
    require(all(isinstance(experiment.get(key), dict) for key in
                ("profile", "build_manifest", "scenario")), "Missing experiment inputs")
    integer_numbers(experiment)
    expected = prepare(experiment["profile"], experiment["build_manifest"],
                       experiment["scenario"], catalogue)
    require(canonical(experiment) == canonical(expected),
            "Experiment differs from its inputs or reviewed sources; prepare a new plan, preserve the old one")
    return expected


def export_profile(experiment, interval, catalogue=None):
    checked = validate(experiment, catalogue)
    require(type(interval) is int and any(row["interval_ms"] == interval for row in checked["trials"]),
            "Select an interval already present in the experiment")
    target = checked["target"]
    return variant(checked["profile"], target["instance_id"], target["period_field"], interval)


def write_new(path, value, *, compact=False):
    # Exclusive creation preserves existing plans/profiles without an overwrite flag.
    raw = canonical(value) if compact else json.dumps(value, indent=2, ensure_ascii=True).encode("ascii")
    require(len(raw) <= MAX_EXPERIMENT_BYTES, "Experiment exceeds the file size limit")
    with Path(path).open("xb") as output:
        output.write(raw if compact else raw + b"\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("prepare", help="Create a plan from a profile, manifest and scenario")
    create.add_argument("--profile", required=True)
    create.add_argument("--build-manifest", required=True)
    create.add_argument("--scenario", required=True)
    create.add_argument("--output", required=True)
    verify = commands.add_parser("validate", help="Recheck a saved plan against current reviewed sources")
    verify.add_argument("experiment")
    export = commands.add_parser("export", help="Write one trial as a draft for the existing review flow")
    export.add_argument("experiment")
    export.add_argument("--interval-ms", type=int, required=True)
    export.add_argument("--output", required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "prepare":
            experiment = prepare(load_document(args.profile, 32768), load_document(args.build_manifest),
                                 load_document(args.scenario, 8192))
            write_new(args.output, experiment)
            print(f"Prepared {len(experiment['trials'])} intervals; no measurements or training labels.")
        elif args.command == "validate":
            validate(load_document(args.experiment, MAX_EXPERIMENT_BYTES))
            print("Plan matches reviewed sources; hardware has not been checked.")
        else:
            profile = export_profile(load_document(args.experiment, MAX_EXPERIMENT_BYTES), args.interval_ms)
            write_new(args.output, profile, compact=True)
            print("Draft exported. Use the existing Validate and review / Apply flow before a bench run.")
    except (ConfigurationError, OSError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
