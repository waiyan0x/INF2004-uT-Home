"""Assess a frozen protocol study without enabling model deployment.

This reports the predeclared safety/coverage/benefit criteria. A passing result
still needs independent C parity, target resources and combined-system evidence.
The CLI rebuilds data from retained hardware journals; no labels are accepted
from a caller-supplied dataset file.
"""
import argparse
from pathlib import Path

from ai import workload
from ai.protocol_model import evaluate, predict, validate_model
from ai.protocol_study import MAX_BYTES, dataset, validate_study
from ai.recording import save_new
from ai.timing import digest
from tools.config_validator.planner import load_document, require


def choices(model, experiment, rows):
    """Compare decisions for one complete family, keeping abstention distinct."""
    compatible = workload.compatibility(experiment) in model["contexts"]
    passing = {row["interval_ms"] for row in rows if row["label"] == 1}
    tree = max((row["interval_ms"] for row in rows
                if compatible and predict(model, row["features"]) == 1), default=None)
    rule = max((row["interval_ms"] for row in rows
                if row["features"][1] + row["features"][4] <= row["features"][2]), default=None)
    oracle = max(passing, default=None)
    # Prefer fewer periodic reads only when the selected interval passes.
    # A failed rule choice is not a valid baseline interval to optimize against.
    safe_tree, safe_rule = tree in passing, rule in passing
    return dict(tree_ms=tree, simple_rule_ms=rule, oracle_ms=oracle,
                compatible_context=compatible,
                violation=tree is not None and not safe_tree,
                unnecessary_abstention=tree is None and oracle is not None,
                safe_coverage_gain=safe_tree and not safe_rule,
                safe_interval_gain=safe_tree and safe_rule and tree > rule,
                regression_against_safe_rule=safe_rule and (not safe_tree or tree < rule),
                safe_interval_delta_ms=tree - rule if safe_tree and safe_rule else None)


def assess(study, model, data):
    """Require exact model/data bindings and report each held-out family."""
    validate_study(study)
    validate_model(model)
    require(not data["excluded"], "Incomplete studies cannot pass the model gate")
    require(model["study_sha256"] == digest(study)
            and model["dataset_sha256"] == digest(data), "Model/data/study binding mismatch")
    cases = {case["experiment"]["scenario"]["family_id"]: case["experiment"]
             for case in study["cases"]}
    expected = {(family, trial["interval_ms"]) for family, experiment in cases.items()
                for trial in experiment["trials"]}
    actual = [(row["family_id"], row["interval_ms"]) for row in data["rows"]]
    require(len(actual) == len(expected) and set(actual) == expected,
            "Missing, duplicate or unexpected candidate rows")
    for row in data["rows"]:
        require(type(row["label"]) is int and row["label"] in (0, 1), "Invalid candidate label")
        require(row["features"] == workload.features(cases[row["family_id"]], row["interval_ms"]),
                "Candidate features differ from the frozen workload")
    partitions = {}
    for partition, families in study["split"].items():
        decisions = {family: choices(model, cases[family],
                                    [row for row in data["rows"] if row["family_id"] == family])
                     for family in families}
        counts = {key: sum(int(value[key]) for value in decisions.values()) for key in
                  ("violation", "unnecessary_abstention", "safe_coverage_gain",
                   "safe_interval_gain", "regression_against_safe_rule")}
        partitions[partition] = dict(families=decisions, counts=counts,
                                     metrics=evaluate(model, data, study, partition))
    heldout = [value for name in ("validation", "test")
               for value in partitions[name]["families"].values()]
    reasons = []
    if any(value["violation"] for value in heldout):
        reasons.append("heldout_violation")
    if any(value["unnecessary_abstention"] for value in heldout):
        reasons.append("heldout_unnecessary_abstention")
    if any(value["regression_against_safe_rule"] for value in heldout):
        reasons.append("heldout_regression_against_safe_rule")
    if not any(value["safe_coverage_gain"] or value["safe_interval_gain"] for value in heldout):
        reasons.append("no_measured_benefit_over_simple_rule")
    return dict(contract="uthome.protocol-model-gate", version="0.1.0",
                study_sha256=digest(study), model_sha256=digest(model), dataset_sha256=digest(data),
                statistical_criteria_passed=not reasons, reasons=reasons,
                benefit_definition="At least one held-out safe coverage or interval gain, with no regression against a passing rule choice",
                partitions=partitions, deployment_allowed=False, automatic_apply=False,
                pending_required_checks=["python_c_parity", "target_resource_and_latency_limits",
                                         "combined_system_qualification", "reviewed_desk_integration"],
                scope="Frozen supported workload/build contexts; no unseen-device or radio claim")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--study", required=True, type=Path)
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("recordings", type=Path, nargs="+")
    args = parser.parse_args(argv)
    require(not args.output.exists(), "Output already exists")
    study = load_document(args.study, MAX_BYTES)
    report = assess(study, load_document(args.model), dataset(study, args.recordings))
    save_new(args.output, report)
    print("Statistical criteria:", "passed" if report["statistical_criteria_passed"] else "failed")
    print("Deployment remains disabled.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
