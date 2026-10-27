"""Host-only protocol experiment; separate from the six-input C export contract."""
import argparse
import math
from pathlib import Path
import sys

from ai.protocol_study import MAX_BYTES, dataset, validate_study
from ai.recording import save_new
from ai.timing import digest, export_profile, validate
from ai import workload
from tools.config_validator.planner import ConfigurationError, load_document, require


def validate_model(model):
    require(set(model) == {"contract", "version", "feature_names", "domain", "contexts", "nodes", "threshold_per_mille",
                          "study_sha256", "dataset_sha256", "origin", "deployment_allowed", "training"}
            and model["contract"] == "uthome.protocol-tree" and model["version"] == "0.2.0"
            and model["feature_names"] == workload.FEATURES and model["origin"] == "hardware"
            and model["deployment_allowed"] is False and model["threshold_per_mille"] == 900,
            "Unsupported protocol model")
    require(model["training"] == dict(library="scikit-learn", version="1.8.0", seed=42, max_depth=4, min_samples_leaf=3),
            "Unreviewed training settings")
    require(len(model["domain"]) == len(workload.FEATURES), "Wrong feature count")
    for bounds, (low, high) in zip(model["domain"], workload.LIMITS):
        require(len(bounds) == 2 and all(type(v) is int for v in bounds) and low <= bounds[0] <= bounds[1] <= high,
                "Invalid feature domain")
    require(isinstance(model["contexts"], list) and 1 <= len(model["contexts"]) <= 256, "Missing supported contexts")
    for value in model["contexts"] + [model["study_sha256"], model["dataset_sha256"]]:
        require(isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value), "Invalid provenance hash")
    nodes, seen = model["nodes"], set()
    require(1 <= len(nodes) <= 31, "Tree exceeds budget")
    def visit(index, depth):
        require(type(index) is int and 0 <= index < len(nodes) and index not in seen and depth <= 4, "Invalid tree graph")
        seen.add(index)
        n = nodes[index]
        require(set(n) == {"feature", "threshold", "left", "right", "score"} and all(type(v) is int for v in n.values())
                and 0 <= n["score"] <= 1000, "Invalid tree node")
        if n["feature"] == -1:
            require(n["threshold"] == n["left"] == n["right"] == 0, "Invalid leaf")
        else:
            require(0 <= n["feature"] < len(workload.FEATURES) and 0 <= n["threshold"] <= workload.LIMITS[n["feature"]][1],
                    "Invalid split")
            visit(n["left"], depth + 1)
            visit(n["right"], depth + 1)
    visit(0, 0)
    require(len(seen) == len(nodes), "Unreachable tree nodes")


def predict(model, vector):
    validate_model(model)
    require(len(vector) == len(workload.FEATURES) and all(type(v) is int for v in vector), "Invalid feature vector")
    if any(not low <= v <= high for v, (low, high) in zip(vector, model["domain"])):
        return -1
    index = 0
    for _ in range(5):
        n = model["nodes"][index]
        if n["feature"] == -1:
            return int(n["score"] >= 900)
        index = n["left"] if vector[n["feature"]] <= n["threshold"] else n["right"]
    raise ValueError("Tree depth exceeded")


def recommend(model, experiment):
    validate(experiment)
    if model is None:
        return workload.fallback(experiment, "no_validated_protocol_model")
    validate_model(model)
    if workload.compatibility(experiment) not in model["contexts"]:
        return workload.fallback(experiment, "unseen_catalogue_or_build_context")
    candidates = []
    for trial in experiment["trials"]:
        result = workload.coverage(experiment, trial["interval_ms"])
        if result["status"] != "supported":
            return result
        if predict(model, result["features"]) == 1:
            candidates.append(trial["interval_ms"])
    if not candidates:
        return workload.fallback(experiment, "no_supported_candidate_passes")
    interval = max(candidates)
    return dict(status="experimental_candidate", interval_ms=interval, deployment_allowed=False,
                automatic_apply=False, profile=export_profile(experiment, interval))


def evaluate(model, data, study, partition):
    cases = {c["experiment"]["scenario"]["family_id"]: c["experiment"] for c in study["cases"]}
    results = {}
    for method in ("tree", "simple_rule", "current_profile", "descriptor_default"):
        result = dict(families=0, violations=0, abstentions=0, optimal_choices=0)
        for family in study["split"][partition]:
            rows = [r for r in data["rows"] if r["family_id"] == family]
            experiment = cases[family]
            oracle = max((r["interval_ms"] for r in rows if r["label"]), default=None)
            if method == "tree":
                accepted = [r["interval_ms"] for r in rows if workload.compatibility(experiment) in model["contexts"] and predict(model, r["features"]) == 1]
                chosen = max(accepted, default=None)
            elif method == "simple_rule":
                chosen = max((r["interval_ms"] for r in rows if r["features"][1] + r["features"][4] <= r["features"][2]), default=None)
            else:
                chosen = next(t["interval_ms"] for t in experiment["trials"] if method in t["roles"])
            result["families"] += 1
            result["abstentions"] += int(chosen is None)
            result["optimal_choices"] += int(chosen == oracle)
            result["violations"] += int(chosen is not None and not next(r["label"] for r in rows if r["interval_ms"] == chosen))
        results[method] = result
    return results


def train(study, folders):
    """Only verified real journals can enter this trainer; rebuild all labels."""
    import sklearn
    from sklearn.tree import DecisionTreeClassifier
    require(sklearn.__version__ == "1.8.0", "Use the pinned training environment")
    validate_study(study)
    data = dataset(study, folders)
    require(not data["excluded"], "Complete every frozen family before training; exclusions remain preserved")
    rows = [r for r in data["rows"] if r["family_id"] in study["split"]["train"]]
    require({r["label"] for r in rows} == {0, 1}, "Training requires passing and failing examples")
    x = [r["features"] for r in rows]
    tree = DecisionTreeClassifier(max_depth=4, min_samples_leaf=3, random_state=42).fit(x, [r["label"] for r in rows])
    raw, nodes = tree.tree_, []
    for index in range(raw.node_count):
        leaf = raw.children_left[index] == -1
        fraction = raw.value[index][0]
        nodes.append(dict(feature=-1 if leaf else int(raw.feature[index]), threshold=0 if leaf else math.floor(raw.threshold[index]),
                          left=0 if leaf else int(raw.children_left[index]), right=0 if leaf else int(raw.children_right[index]),
                          score=math.floor(float(fraction[1] / sum(fraction)) * 1000)))
    model = dict(contract="uthome.protocol-tree", version="0.2.0", origin="hardware", deployment_allowed=False,
                 feature_names=workload.FEATURES, domain=[[min(c), max(c)] for c in zip(*x)], nodes=nodes, threshold_per_mille=900,
                 contexts=sorted({workload.compatibility(c["experiment"]) for c in study["cases"]
                                  if c["experiment"]["scenario"]["family_id"] in study["split"]["train"]}),
                 study_sha256=digest(study), dataset_sha256=digest(data),
                 training=dict(library="scikit-learn", version="1.8.0", seed=42, max_depth=4, min_samples_leaf=3))
    validate_model(model)
    # Check conversion on all in-domain vectors, including held-out combinations.
    for row in data["rows"]:
        expected = math.floor(float(tree.predict_proba([row["features"]])[0][1]) * 1000) >= 900
        actual = predict(model, row["features"])
        require(actual == -1 or actual == int(expected), "Integer tree conversion mismatch")
    evaluation = dict(contract="uthome.protocol-evaluation", version="0.2.0", study_sha256=digest(study),
                      model_sha256=digest(model), split=study["split"], deployment_allowed=False,
                      results={part: evaluate(model, data, study, part) for part in study["split"]})
    return model, evaluation, data


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--study", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("recordings", type=Path, nargs="+")
    args = parser.parse_args(argv)
    try:
        require(not args.output.exists(), "Output already exists")
        model, report, data = train(load_document(args.study, MAX_BYTES), args.recordings)
        args.output.mkdir(parents=True, exist_ok=False)
        for name, value in (("model", model), ("evaluation", report), ("dataset", data)):
            save_new(args.output / (name + ".json"), value)
        print("Saved host-only model and frozen-split evaluation; deployment remains disabled.")
        return 0
    except (ConfigurationError, ValueError, OSError, KeyError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
