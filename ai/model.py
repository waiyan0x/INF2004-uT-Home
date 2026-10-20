"""Small experimental decision tree; data-only export, no deployment or pickle."""
import math
import random

from ai.records import FEATURES, FEATURE_LIMITS, features, validate_dataset
from ai.timing import digest, validate, variant
from tools.config_validator.planner import Catalogue, build_plan, require


def context(experiment):
    provenance = experiment["provenance"]
    reference = experiment["target"]["descriptor"]
    descriptor = next(item for item in provenance["descriptors"]
                      if (item["id"], item["version"]) == (reference["id"], reference["version"]))
    return digest(dict(board=provenance["board"], descriptor=descriptor,
                       node_schema=provenance["node_schema_sha256"],
                       descriptor_schema=provenance["descriptor_schema_sha256"],
                       admission=provenance["implementation_sha256"]["tools/config_validator/planner.py"],
                       discovery=provenance["discovery_policy"], build=provenance["build_manifest_sha256"]))


def validate_model(model):
    keys = {"contract", "version", "origin", "deployment_allowed", "feature_names", "domain", "contexts",
            "nodes", "threshold_per_mille", "dataset_sha256", "bundle_sha256", "training"}
    require(isinstance(model, dict) and set(model) == keys and model["contract"] == "uthome.timing-tree"
            and model["version"] == "0.1.0" and model["deployment_allowed"] is False
            and model["origin"] in ("simulated", "hardware") and model["feature_names"] == FEATURES,
            "Unsupported or deployable model artifact")
    require(type(model["threshold_per_mille"]) is int and model["threshold_per_mille"] == 900,
            "Unreviewed decision threshold")
    require(isinstance(model["domain"], list) and len(model["domain"]) == len(FEATURES), "Invalid feature domain")
    for bounds, (low, high) in zip(model["domain"], FEATURE_LIMITS):
        require(isinstance(bounds, list) and len(bounds) == 2 and all(type(n) is int for n in bounds)
                and low <= bounds[0] <= bounds[1] <= high, "Invalid feature range")
    hashes = [model["dataset_sha256"], model["bundle_sha256"]]
    require(isinstance(model["contexts"], list) and 1 <= len(model["contexts"]) <= 256, "Invalid contexts")
    hashes += model["contexts"]
    require(all(isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value)
                for value in hashes), "Invalid provenance hash")
    require(len(set(model["contexts"])) == len(model["contexts"]), "Duplicate contexts")
    training = model["training"]
    require(isinstance(training, dict) and set(training) == {"library", "version", "seed", "max_depth", "min_samples_leaf"}
            and training["library"] == "scikit-learn" and training["version"] == "1.8.0"
            and type(training["seed"]) is int and 0 <= training["seed"] <= 0xFFFFFFFF
            and type(training["max_depth"]) is int and training["max_depth"] == 4
            and type(training["min_samples_leaf"]) is int and training["min_samples_leaf"] == 3,
            "Invalid training provenance")
    nodes = model["nodes"]
    require(isinstance(nodes, list) and 1 <= len(nodes) <= 31, "Tree exceeds the depth-four budget")
    visited = set()
    def visit(index, depth):
        require(type(index) is int and 0 <= index < len(nodes) and index not in visited and depth <= 4,
                "Invalid, shared or cyclic tree node")
        visited.add(index)
        node = nodes[index]
        require(isinstance(node, dict) and set(node) == {"feature", "threshold", "left", "right", "score"}
                and all(type(n) is int for n in node.values()) and 0 <= node["score"] <= 1000,
                "Malformed tree node")
        if node["feature"] == -1:
            require(node["left"] == node["right"] == node["threshold"] == 0, "Invalid leaf")
        else:
            require(0 <= node["feature"] < len(FEATURES)
                    and 0 <= node["threshold"] <= FEATURE_LIMITS[node["feature"]][1]
                    and node["left"] > index and node["right"] > index, "Invalid split")
            visit(node["left"], depth + 1)
            visit(node["right"], depth + 1)
    visit(0, 0)
    require(len(visited) == len(nodes), "Unreachable tree nodes")
    return model


def score(model, vector):
    """Return empirical leaf pass fraction (not calibrated confidence), or None."""
    require(isinstance(vector, list) and len(vector) == len(FEATURES)
            and all(type(value) is int for value in vector), "Invalid feature vector")
    if any(not low <= value <= high for value, (low, high) in zip(vector, model["domain"])):
        return None
    index = 0
    for _ in range(5):
        node = model["nodes"][index]
        if node["feature"] == -1:
            return node["score"]
        index = node["left"] if vector[node["feature"]] <= node["threshold"] else node["right"]
    raise ValueError("Tree depth exceeded")


def predict(model, vector):
    validate_model(model)
    value = score(model, vector)
    return -1 if value is None else int(value >= model["threshold_per_mille"])


def recommend(model, experiment, *, allow_simulated=False, catalogue=None):
    validate_model(model)
    catalogue = catalogue or Catalogue()
    experiment = validate(experiment, catalogue)
    result = dict(status="abstain", reason=None, instance_id=experiment["target"]["instance_id"],
                  interval_ms=None, origin=model["origin"], deployment_allowed=False, profile=None)
    if model["origin"] == "simulated" and not allow_simulated:
        return dict(result, reason="simulated_model_requires_explicit_offline_use")
    if context(experiment) not in model["contexts"]:
        return dict(result, reason="unseen_catalogue_or_build_context")
    candidates = [row["interval_ms"] for row in experiment["trials"]
                  if predict(model, features(experiment, row["interval_ms"], catalogue)) == 1]
    if not candidates:
        return dict(result, reason="no_supported_candidate_passes")
    interval = max(candidates)
    target = experiment["target"]
    profile = variant(experiment["profile"], target["instance_id"], target["period_field"], interval)
    build_plan(profile, experiment["build_manifest"], catalogue)
    return dict(result, status="experimental_candidate", interval_ms=interval, profile=profile)


def split_families(rows, seed):
    families = sorted({row["family_id"] for row in rows})
    require(len(families) >= 10, "At least ten independent scenario families are required")
    random.Random(seed).shuffle(families)
    size = max(2, len(families) // 5)
    return dict(train=sorted(families[2 * size:]), validation=sorted(families[:size]), test=sorted(families[size:2 * size]))


def evaluate(model, rows, cases, majority):
    """Report decision violations and abstentions, not accuracy alone."""
    by_name = {case["scenario"]["scenario_id"]: case for case in cases}
    grouped = {}
    for row in rows:
        grouped.setdefault(row["scenario_id"], []).append(row)
    results = {}
    for method in ("tree", "current_profile", "descriptor_default", "simple_rule", "majority"):
        result = dict(scenarios=len(grouped), violations=0, optimal_choices=0, abstentions=0,
                      unnecessary_abstentions=0, correct_trials=0, classified_trials=0, unsupported_trials=0)
        for name, trials in grouped.items():
            case = by_name[name]
            oracle = max((r["interval_ms"] for r in trials if r["label"] == 1), default=None)
            if method in ("current_profile", "descriptor_default"):
                chosen = next(r["interval_ms"] for r in case["trials"] if method in r["roles"])
            else:
                accepted = []
                for row in trials:
                    if method == "tree":
                        prediction = predict(model, row["features"]) if context(case) in model["contexts"] else -1
                    elif method == "simple_rule":
                        f = row["features"]
                        prediction = int(f[1] + f[4] <= f[2])
                    else:
                        prediction = majority
                    if prediction == -1:
                        result["unsupported_trials"] += 1
                    else:
                        result["classified_trials"] += 1
                        result["correct_trials"] += int(prediction == row["label"])
                    if prediction == 1:
                        accepted.append(row["interval_ms"])
                chosen = max(accepted, default=None)
            result["abstentions"] += int(chosen is None)
            result["unnecessary_abstentions"] += int(chosen is None and oracle is not None)
            result["optimal_choices"] += int(chosen == oracle)
            result["violations"] += int(chosen is not None and not next(r["label"] for r in trials if r["interval_ms"] == chosen))
        results[method] = result
    return results


def train(dataset, bundle, *, seed=42, catalogue=None):
    # Import only at training time; prediction needs the standard library/jsonschema.
    import sklearn
    from sklearn.tree import DecisionTreeClassifier

    require(sklearn.__version__ == "1.8.0", "Use the pinned training requirements")
    require(type(seed) is int and 0 <= seed <= 0xFFFFFFFF, "Invalid seed")
    catalogue = catalogue or Catalogue()
    validate_dataset(dataset, bundle, catalogue)
    split = split_families(dataset["rows"], seed)
    rows = [row for row in dataset["rows"] if row["family_id"] in split["train"]]
    require({row["label"] for row in rows} == {0, 1}, "Training needs both passing and failing trials")
    tree = DecisionTreeClassifier(max_depth=4, min_samples_leaf=3, random_state=seed)
    x = [row["features"] for row in rows]
    tree.fit(x, [row["label"] for row in rows])
    raw = tree.tree_
    nodes = []
    for index in range(raw.node_count):
        leaf = raw.children_left[index] == -1
        fraction = raw.value[index][0]
        nodes.append(dict(feature=-1 if leaf else int(raw.feature[index]),
                          threshold=0 if leaf else math.floor(float(raw.threshold[index])),
                          left=0 if leaf else int(raw.children_left[index]),
                          right=0 if leaf else int(raw.children_right[index]),
                          score=math.floor(float(fraction[1] / sum(fraction)) * 1000)))
    model = dict(contract="uthome.timing-tree", version="0.1.0", origin=dataset["origin"], deployment_allowed=False,
                 feature_names=FEATURES, domain=[[min(col), max(col)] for col in zip(*x)],
                 contexts=sorted({context(case) for case in dataset["cases"] if case["scenario"]["family_id"] in split["train"]}),
                 nodes=nodes, threshold_per_mille=900, dataset_sha256=digest(dataset), bundle_sha256=digest(bundle),
                 training=dict(library="scikit-learn", version=sklearn.__version__, seed=seed, max_depth=4, min_samples_leaf=3))
    validate_model(model)
    # Ensure integer conversion preserves all raw sklearn leaf scores before export.
    for row in dataset["rows"]:
        f = row["features"]
        if all(low <= value <= high for value, (low, high) in zip(f, model["domain"])):
            reference = math.floor(float(tree.predict_proba([f])[0][1]) * 1000)
            require(score(model, f) == reference, "Integer tree conversion differs from sklearn")
    majority = int(sum(row["label"] for row in rows) * 2 > len(rows))
    report = dict(contract="uthome.timing-evaluation", version="0.1.0", origin=dataset["origin"],
                  deployment_allowed=False, hardware_accuracy=None, model_sha256=digest(model),
                  split=split, node_count=len(nodes), max_depth=tree.get_depth(),
                  dataset_rows=len(dataset["rows"]), excluded_scenarios=dataset["excluded"],
                  results={part: evaluate(model, [r for r in dataset["rows"] if r["family_id"] in groups],
                                          dataset["cases"], majority) for part, groups in split.items()})
    return model, report
