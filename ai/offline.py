"""Run the timing-model pipeline without equipment; no UI, USB or deployment."""
import argparse
import importlib.metadata
import json
from pathlib import Path
import random
import subprocess
import sys

from ai.export import cross_check, export_c, verify_c
from ai.model import recommend, train
from ai.records import build_dataset, features
from ai.timing import canonical, digest, prepare
from tools.config_validator.planner import ROOT, Catalogue, ConfigurationError, load_document, require


MAX_BYTES = 64 * 1024 * 1024


def save(path, value):
    raw = canonical(value)
    require(len(raw) <= MAX_BYTES, "Artifact exceeds 64 MiB")
    with Path(path).open("xb") as output:
        output.write(raw)


def simulate(*, families=24, seed=42, catalogue=None):
    """Toy queue/age assumptions exercise the pipeline, not an ESP32 simulator."""
    require(type(families) is int and 10 <= families <= 128, "Choose 10 to 128 simulated families")
    require(type(seed) is int and 0 <= seed <= 0xFFFFFFFF, "Invalid simulation seed")
    catalogue = catalogue or Catalogue()
    manifest = load_document(ROOT / "configs/builds/host-reference.json")
    rng = random.Random(seed)
    cases = []
    for index in range(families * 2):
        family = index // 2
        kind = family % 3
        slot = family // 3
        fixture, instance = (("two-environment-sensors", "room_1"), ("distance-only", "front_range"),
                             ("first-prototype", "oled_status"))[kind]
        profile = load_document(ROOT / f"configs/nodes/{fixture}.json")
        intervals = [200, 500, 1000, 2000] if kind == 0 else [100, 200, 500, 1000]
        if kind == 0:
            profile["devices"][1]["enabled"] = bool(slot % 2)
            profile["devices"][0]["behaviour"]["timeout_ms"] = 10 + (slot % 9) * 10
        if kind == 1:
            profile["devices"][0]["behaviour"]["timeout_us"] = 25000 + (slot % 11) * 500
        if kind == 2:
            intervals = [100, 250, 500, 1000]
            profile["devices"][0]["behaviour"]["interval_ms"] = [200, 500, 1000, 2000][slot % 4]
            profile["devices"][1]["behaviour"]["timeout_ms"] = 5 + (slot % 4) * 5
            # Stream cadence is outside this first feature contract.
            profile["devices"][1]["behaviour"]["sources"] = ["telemetry.environment"]
        scenario = dict(contract_version="0.1.0", scenario_id=f"simulation-{seed}-{index}",
                        family_id=f"simulation-family-{digest(profile)[:24]}", instance_id=instance,
                        conditions=f"SIMULATED ONLY: simulator-v1, seed {seed}, family {index}; toy age/load assumptions.",
                        goal=dict(metric="visible_update_delay_ms" if kind == 2 else "observed_sample_age_ms",
                                  limit_ms=rng.choice([400, 800, 1200, 2500, 4000])),
                        candidate_intervals_ms=intervals, warmup_ms=2000, duration_ms=30000, repetitions=3)
        experiment = prepare(profile, manifest, scenario, catalogue)
        runs = []
        order = [(trial, repetition) for trial in experiment["trials"] for repetition in range(3)]
        rng.shuffle(order)
        for trial, repetition in order:
            interval = trial["interval_ms"]
            f = features(experiment, interval, catalogue)
            phase = rng.randrange(interval)
            delay = 25 * f[3] + f[5] // 3000
            overloaded = interval < 40 * f[3] + f[5] // 1000
            session = f"sim-{seed}-{index}-{interval}-{repetition}"
            run = dict(contract="uthome.timing-run", version="0.1.0", experiment_sha256=digest(experiment),
                       interval_ms=interval, repetition=repetition, origin="simulated", method="simulator-v1",
                       firmware_sha256=None, identity_evidence_sha256=None, session_id=session,
                       sample_interval_ms=250, samples=[])
            for elapsed in range(0, 32001, 250):
                age = (interval + f[4] if kind == 2 else (elapsed + phase) % interval) + delay + rng.randrange(20)
                bad = overloaded and elapsed >= scenario["warmup_ms"]
                run["samples"].append(dict(elapsed_ms=elapsed, uptime_ms=100000 + elapsed,
                    session_id=session, profile_sha256=trial["profile_sha256"], metric_ms=age,
                    quality="stale" if bad else "fresh", health_faults=0))
            runs.append(run)
        cases.append(dict(experiment=experiment, runs=runs))
    return dict(contract="uthome.timing-runs", version="0.1.0", cases=cases)


def train_files(bundle, dataset, folder):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=False)
    model, evaluation = train(dataset, bundle)
    save(folder / "model.json", model)
    save(folder / "evaluation.json", evaluation)
    dependencies = {name: importlib.metadata.version(name) for name in
                    ("scikit-learn", "numpy", "scipy", "joblib", "threadpoolctl", "jsonschema")}
    save(folder / "environment.json", dict(python=sys.version, dependencies=dependencies))
    checks = export_c(model, folder / "c")
    checks["parity"] = verify_c(model, folder / "c", [row["features"] for row in dataset["rows"]])
    save(folder / "c-checks.json", checks)
    example = dataset["cases"][0]
    save(folder / "recommendation.json", recommend(model, example, allow_simulated=model["origin"] == "simulated"))
    print(json.dumps(dict(origin=model["origin"], rows=len(dataset["rows"]), nodes=len(model["nodes"]),
                          test=evaluation["results"]["test"], c_parity=checks["parity"], deployment_allowed=False), indent=2))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("simulate", "demo"):
        command = commands.add_parser(name)
        command.add_argument("output", type=Path)
        command.add_argument("--families", type=int, default=24)
        command.add_argument("--seed", type=int, default=42)
    dataset = commands.add_parser("dataset")
    dataset.add_argument("bundle", type=Path)
    dataset.add_argument("output", type=Path)
    fitting = commands.add_parser("train")
    fitting.add_argument("bundle", type=Path)
    fitting.add_argument("dataset", type=Path)
    fitting.add_argument("output", type=Path)
    prediction = commands.add_parser("recommend")
    prediction.add_argument("model", type=Path)
    prediction.add_argument("experiment", type=Path)
    prediction.add_argument("output", type=Path)
    prediction.add_argument("--allow-simulated", action="store_true")
    cross = commands.add_parser("cross-check")
    cross.add_argument("c_folder", type=Path)
    cross.add_argument("--compiler", type=Path, required=True)
    cross.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command in ("simulate", "demo"):
            require(not args.output.exists(), "Output already exists; choose a new path")
            print("Generating simulated runs; no hardware measurements.", flush=True)
            bundle = simulate(families=args.families, seed=args.seed)
            if args.command == "simulate":
                save(args.output, bundle)
            else:
                args.output.mkdir(parents=True, exist_ok=False)
                save(args.output / "runs.json", bundle)
                print("Validating runs and preparing labels.", flush=True)
                data = build_dataset(bundle)
                save(args.output / "dataset.json", data)
                print("Training, evaluating and checking the C export.", flush=True)
                train_files(bundle, data, args.output / "trained")
        elif args.command == "dataset":
            save(args.output, build_dataset(load_document(args.bundle, MAX_BYTES)))
        elif args.command == "train":
            train_files(load_document(args.bundle, MAX_BYTES), load_document(args.dataset, MAX_BYTES), args.output)
        elif args.command == "cross-check":
            require(not args.output.exists(), "Output already exists; choose a new path")
            save(args.output, cross_check(args.c_folder, args.compiler))
        else:
            model = load_document(args.model)
            experiment = load_document(args.experiment, 262144)
            save(args.output, recommend(model, experiment, allow_simulated=args.allow_simulated))
    except (ConfigurationError, OSError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr.decode(errors="replace") if isinstance(error.stderr, bytes) else error.stderr, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
