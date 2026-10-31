"""Freeze protocol workload groups and derive labels from verified live journals."""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys

from ai import workload
from ai.recording import load_recording, save_new
from ai.records import assess_run
from ai.timing import canonical, digest, prepare, validate
from tools.config_validator.planner import Catalogue, ROOT, ConfigurationError, load_document, require

MAX_BYTES = 64 * 1024 * 1024
ROLES = {"sensor": ("i2c", "periodic"), "display": ("spi", "queue_consumer"), "stream": ("uart", "stream")}


def prepare_study(profile, manifest, specification, catalogue=None):
    catalogue = catalogue or Catalogue()
    version = specification.get("version")
    fields = {"version", "study_id", "roles", "intervals_ms", "warmup_ms",
              "duration_ms", "repetitions", "cadence_ms", "families"}
    if version == "0.2.0":
        fields.add("goal_ms")
    require(version in ("0.2.0", "0.3.0") and set(specification) == fields,
            "Unsupported study specification")
    require(set(specification["roles"]) == set(ROLES) and len(set(specification["roles"].values())) == 3,
            "Declare distinct I2C sensor, SPI display and UART stream roles")
    require(type(specification["cadence_ms"]) is int and 100 <= specification["cadence_ms"] <= 1000,
            "Invalid fixed observer cadence")
    require(10 <= len(specification["families"]) <= 32, "Freeze 10..32 workload families before collection")
    require(not profile.get("rules"), "This study does not tune rule-controlled outputs")
    cases, seen, splits = [], set(), {part: [] for part in ("train", "validation", "test")}
    for family in specification["families"]:
        family_fields = {"id", "split", "i2c_clock_hz", "spi_enabled", "spi_clock_hz", "spi_refresh_ms", "uart_enabled"}
        if version == "0.3.0":
            family_fields.add("goal_ms")
        require(set(family) == family_fields, "Invalid family fields")
        require(family["split"] in splits and type(family["spi_enabled"]) is bool and type(family["uart_enabled"]) is bool,
                "Invalid split or enabled flags")
        candidate = deepcopy(profile)
        devices = {d["device_id"]: d for d in candidate["devices"]}
        buses = {b["id"]: b for b in candidate["interfaces"]}
        require(all(name in devices for name in specification["roles"].values()), "Missing role instance")
        for role, name in specification["roles"].items():
            device = devices[name]
            ref = device["descriptor"]
            descriptor = catalogue.descriptors[(ref["id"], ref["version"])]
            require((descriptor["protocol"], descriptor["execution"]["kind"]) == ROLES[role], "Role capability mismatch")
            device["enabled"] = True if role == "sensor" else family["spi_enabled" if role == "display" else "uart_enabled"]
            bus = buses[device["interface"]["ref"]]
            if role in ("sensor", "display"):
                bus["clock_hz"] = family["i2c_clock_hz" if role == "sensor" else "spi_clock_hz"]
            if role == "display":
                device["behaviour"][descriptor["execution"]["period_field"]] = family["spi_refresh_ms"]
        require(not any(d["enabled"] and d["device_id"] not in specification["roles"].values() for d in candidate["devices"]),
                "Additional enabled devices require a new reviewed study specification")
        scenario = dict(contract_version="0.1.0", scenario_id=specification["study_id"] + "." + family["id"],
                        family_id=family["id"], instance_id=specification["roles"]["sensor"],
                        conditions="Protocol workload study; fixed observer cadence and unchanged wiring. "
                                   "Configured work rates are not measured CPU/wire utilization. Family: " + family["id"],
                        goal=dict(metric="observed_sample_age_ms",
                                  limit_ms=(family if version == "0.3.0" else specification)["goal_ms"]),
                        candidate_intervals_ms=specification["intervals_ms"], warmup_ms=specification["warmup_ms"],
                        duration_ms=specification["duration_ms"], repetitions=specification["repetitions"])
        experiment = prepare(candidate, manifest, scenario, catalogue)
        family_key = workload.family_key(experiment)
        require(family_key not in seen and not any(family["id"] in ids for ids in splits.values()),
                "Duplicate workload family; repeated captures cannot inflate independence")
        seen.add(family_key)
        vectors = [workload.features(experiment, t["interval_ms"], catalogue) for t in experiment["trials"]]
        splits[family["split"]].append(family["id"])
        cases.append(dict(family_key=family_key, experiment=experiment, features=vectors))
    require(all(len(ids) >= 2 for ids in splits.values()), "Every evaluation partition requires at least two families")
    sources = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
               for name in ("ai/workload.py", "ai/protocol_study.py")}
    return dict(contract="uthome.protocol-study", version=version, specification=deepcopy(specification),
                base_profile=deepcopy(profile), build_manifest=deepcopy(manifest), split=splits,
                feature_names=workload.FEATURES, source_sha256=sources, cases=cases, deployment_allowed=False)


def validate_study(study, catalogue=None):
    expected = prepare_study(study["base_profile"], study["build_manifest"], study["specification"], catalogue)
    require(canonical(study) == canonical(expected), "Study differs from its frozen specification or sources")
    return study


def observe_peers(folder, experiment, run):
    """Auxiliary checks, never visible OLED latency or radar detection accuracy."""
    rows = [json.loads(line) for line in (Path(folder) / "raw.jsonl").read_text().splitlines()]
    snapshots = [r["snapshot"] for r in rows if r["type"] == "sample"
                 and r["elapsed_ms"] >= experiment["scenario"]["warmup_ms"]]
    peers, displays = [], []
    catalogue = Catalogue()
    for index, device in enumerate(experiment["profile"]["devices"]):
        if not device["enabled"]:
            continue
        ref = device["descriptor"]
        descriptor = catalogue.descriptors[(ref["id"], ref["version"])]
        if descriptor["execution"]["kind"] == "stream":
            expected_type = {"target_report": 2}.get(descriptor["routing"]["output_type"])
            require(expected_type is not None, "Stream sample type lacks an observation adapter")
            queue_index = next(i for i, q in enumerate(experiment["profile"]["queues"])
                               if q["id"] == device["behaviour"]["destination"])
            observations = []
            for snapshot in snapshots:
                matches = [s for s in snapshot["samples"] if s["name"] == device["device_id"]]
                row = matches[0] if len(matches) == 1 else None
                require(row is None or (row["source_index"], row["queue"], row["type"]) == (index, queue_index, expected_type),
                        "Peer source mapping changed")
                observations.append(row)
            peers.append(dict(instance_id=device["device_id"], observations=len(observations),
                              unhealthy=sum(r is None or r["quality"] != "fresh" for r in observations)))
        if descriptor["execution"]["kind"] == "queue_consumer":
            indices = [i for i, q in enumerate(experiment["profile"]["queues"])
                       if q["id"] in device["behaviour"]["sources"]]
            consumed = sum(snapshots[-1]["queues"][i]["consumed"] - snapshots[0]["queues"][i]["consumed"] for i in indices)
            require(consumed >= 0, "Consumer counters decreased")
            # cfgbind reads shared_sample/cache, so zero FIFO consumption is
            # normal. Neither these counters nor global health prove rendering.
            displays.append(dict(instance_id=device["device_id"], fifo_consumed_delta=consumed,
                                 completion_measured=False, visible_latency_measured=False))
    return dict(streams=peers, consumers=displays,
                passes=all(p["unhealthy"] == 0 for p in peers))


def dataset(study, folders):
    """Rebuild every label from raw files; never consume unverified run hashes."""
    validate_study(study)
    expected = {digest(c["experiment"]): c for c in study["cases"]}
    grouped, origins = {}, set()
    evidence = set()
    for folder in folders:
        experiment, run = load_recording(folder)
        key = digest(experiment)
        require(key in expected, "Recording was not planned in this study")
        require(run["sample_interval_ms"] == study["specification"]["cadence_ms"], "Observer cadence changed")
        origins.add(run["origin"])
        evidence.add((run["firmware_sha256"], run["identity_evidence_sha256"]))
        trial = (run["interval_ms"], run["repetition"])
        case = grouped.setdefault(key, {})
        require(trial not in case, "Duplicate trial/repetition")
        assessment = assess_run(experiment, run)
        peers = observe_peers(folder, experiment, run) if assessment["usable"] else None
        case[trial] = dict(assessment=assessment, peers=peers, run_sha256=digest(run),
                           raw_sha256=run["capture"]["raw_sha256"], folder=str(folder))
    require(origins == {"hardware"} and len(evidence) == 1, "Study needs one observed build/identity and real recordings")
    rows, excluded = [], []
    for key, case in expected.items():
        experiment, observed = case["experiment"], grouped.get(key, {})
        planned = {(t["interval_ms"], rep) for t in experiment["trials"]
                   for rep in range(experiment["scenario"]["repetitions"])}
        if set(observed) != planned or any(not r["assessment"]["usable"] for r in observed.values()):
            excluded.append(dict(family_id=experiment["scenario"]["family_id"], reason="missing_or_interrupted_runs"))
            continue
        for trial in experiment["trials"]:
            runs = [observed[(trial["interval_ms"], rep)] for rep in range(experiment["scenario"]["repetitions"])]
            rows.append(dict(scenario_id=experiment["scenario"]["scenario_id"], family_id=experiment["scenario"]["family_id"],
                             interval_ms=trial["interval_ms"], features=workload.features(experiment, trial["interval_ms"]),
                             label=int(all(r["assessment"]["passes"] and r["peers"]["passes"] for r in runs)), evidence=runs))
    return dict(contract="uthome.protocol-dataset", version="0.2.0", origin="hardware", study_sha256=digest(study),
                feature_names=workload.FEATURES, split=study["split"], rows=rows, excluded=excluded,
                deployment_allowed=False)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    plan = sub.add_parser("prepare")
    for name in ("profile", "manifest", "specification", "output"):
        plan.add_argument("--" + name, type=Path, required=True)
    data = sub.add_parser("dataset")
    data.add_argument("--study", type=Path, required=True)
    data.add_argument("--output", type=Path, required=True)
    data.add_argument("recordings", type=Path, nargs="+")
    args = parser.parse_args(argv)
    try:
        if args.command == "prepare":
            result = prepare_study(load_document(args.profile), load_document(args.manifest), load_document(args.specification))
        else:
            result = dataset(load_document(args.study, MAX_BYTES), args.recordings)
        save_new(args.output, result)
        print(json.dumps(dict(output=str(args.output), contract=result["contract"])))
        return 0
    except (ConfigurationError, OSError, KeyError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
