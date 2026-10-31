"""Validate replayed timing observations and derive labels without hardware I/O."""
from ai.timing import canonical, digest, integer_numbers, validate
from tools.config_validator.planner import ROOT, Catalogue, check, load_document, require


VERSION = "0.1.0"
FEATURES = ["execution_kind", "interval_ms", "goal_ms", "active_tasks", "source_period_ms", "timeout_us"]
FEATURE_LIMITS = [[0, 1], [1, 60000], [1, 60000], [1, 8], [0, 60000], [1, 1000000]]


def features(experiment, interval, catalogue):
    """Only pre-run facts; no labels, observed outcomes, identities or defaults."""
    target = experiment["target"]
    profile = experiment["profile"]
    device = next(dev for dev in profile["devices"] if dev["device_id"] == target["instance_id"])
    descriptor = catalogue.descriptors[(device["descriptor"]["id"], device["descriptor"]["version"])]
    consumer = target["execution_kind"] == "queue_consumer"
    periods = [0]
    if consumer:
        for source in device["behaviour"]["sources"]:
            producers = [dev for dev in profile["devices"] if dev["enabled"]
                         and dev["behaviour"].get("destination") == source]
            queue = next(q for q in profile["queues"] if q["id"] == source)
            if queue["item_type"] == "health_event":
                periods.append(profile["runtime"]["health_report_interval_ms"])
            else:
                require(producers, "Display source has no active producer")
                for producer in producers:
                    ref = producer["descriptor"]
                    field = catalogue.descriptors[(ref["id"], ref["version"])]["execution"]["period_field"]
                    require(field is not None, "Unknown source cadence; stream-fed display model is unsupported")
                    periods.append(producer["behaviour"][field])
    execution = descriptor["execution"]
    timeout = device["behaviour"][execution["timeout_field"]] * (1000 if execution["timeout_unit"] == "ms" else 1)
    vector = [int(consumer), interval, experiment["scenario"]["goal"]["limit_ms"],
              experiment["resources"]["active_tasks"], max(periods), timeout]
    require(all(type(value) is int and low <= value <= high
                for value, (low, high) in zip(vector, FEATURE_LIMITS)), "Feature outside the model contract")
    return vector


def assess_run(experiment, run, schema=None):
    """Interrupted runs are unusable; complete bad runs are valid negative examples."""
    integer_numbers(run)
    live = run.get("version") == "0.2.0"
    selected_schema = (load_document(ROOT / "ai/contracts/timing-run-v0.2.schema.json") if live
                       else schema or load_document(ROOT / "ai/contracts/timing-run.schema.json"))
    check(run, selected_schema, "timing run")
    require(run["experiment_sha256"] == digest(experiment), "Run refers to a different experiment")
    trials = {row["interval_ms"]: row for row in experiment["trials"]}
    require(run["interval_ms"] in trials, "Run interval was not planned")
    scenario = experiment["scenario"]
    require(run["repetition"] < scenario["repetitions"], "Unplanned repetition")
    if run["origin"] == "simulated":
        require(run["method"] == "simulator-v1" and run["firmware_sha256"] is None
                and run["identity_evidence_sha256"] is None, "Simulation cannot claim hardware evidence")
    else:
        method = ("telemetry_sample_age" if scenario["goal"]["metric"] == "observed_sample_age_ms"
                  else "external_visible_delay")
        require(run["method"] == method and run["firmware_sha256"] is not None
                and run["identity_evidence_sha256"] is not None
                and experiment["build_manifest"]["backend"] == "target", "Hardware provenance is incomplete")
    if live:
        from ai.provenance import check_run_evidence
        check_run_evidence(experiment, run)
        require(run["capture"]["terminal_tolerance_ms"] == 2 * run["sample_interval_ms"], "Invalid terminal tolerance")
        if run["capture"]["status"] == "interrupted":
            require(bool(run["capture"]["reason"]), "Interrupted capture requires a reason")
            return dict(usable=False, reason=run["capture"]["reason"], passes=None)
        require(run["capture"]["reason"] is None, "Complete capture cannot have an interruption reason")
    expected_hash = trials[run["interval_ms"]]["profile_sha256"]
    samples = run["samples"]
    end = scenario["warmup_ms"] + scenario["duration_ms"]
    reason = None
    if len(samples) < 2:
        return dict(usable=False, reason="insufficient_observations", passes=None)
    terminal_ok = (end <= samples[-1]["elapsed_ms"] <= end + run["capture"]["terminal_tolerance_ms"]
                   if live else samples[-1]["elapsed_ms"] == end)
    if samples[0]["elapsed_ms"] != 0 or not terminal_ok:
        reason = "incomplete_window"
    for previous, current in zip(samples, samples[1:]):
        gap = current["elapsed_ms"] - previous["elapsed_ms"]
        if not 0 < gap <= 2 * run["sample_interval_ms"]:
            reason = "sampling_gap_or_reordered_time"
        if current["uptime_ms"] <= previous["uptime_ms"]:
            reason = "reset_or_reordered_uptime"
        if current["health_faults"] < previous["health_faults"]:
            reason = "counter_reset"
    if any(row["session_id"] != run["session_id"] or row["profile_sha256"] != expected_hash for row in samples):
        reason = "session_or_profile_changed"
    require(all(row["metric_ms"] is not None for row in samples if row["quality"] == "fresh"),
            "Fresh observations require a metric")
    observed = [row for row in samples if row["elapsed_ms"] >= scenario["warmup_ms"]]
    if len(observed) < 2:
        reason = "insufficient_observations"
    if reason:
        return dict(usable=False, reason=reason, passes=None)
    maximum = max((row["metric_ms"] for row in observed if row["metric_ms"] is not None), default=None)
    healthy = all(row["quality"] == "fresh" for row in observed) and (not live or run["capture"]["runtime_healthy"])
    faults = samples[-1]["health_faults"] - samples[0]["health_faults"]
    return dict(usable=True, reason=None, passes=bool(healthy and faults == 0 and maximum is not None
                and maximum <= scenario["goal"]["limit_ms"]), max_observed_ms=maximum,
                unhealthy_samples=sum(row["quality"] != "fresh" for row in observed), health_faults=faults)


def build_dataset(bundle, catalogue=None):
    catalogue = catalogue or Catalogue()
    require(isinstance(bundle, dict) and set(bundle) == {"contract", "version", "cases"}
            and bundle["contract"] == "uthome.timing-runs" and bundle["version"] == VERSION,
            "Unsupported run bundle")
    require(isinstance(bundle["cases"], list) and 1 <= len(bundle["cases"]) <= 256, "Invalid case count")
    rows, cases, excluded, origins, names = [], [], [], set(), set()
    schema = load_document(ROOT / "ai/contracts/timing-run.schema.json")
    for case in bundle["cases"]:
        require(isinstance(case, dict) and set(case) == {"experiment", "runs"}, "Invalid case fields")
        experiment = validate(case["experiment"], catalogue)
        scenario = experiment["scenario"]
        name = scenario["scenario_id"]
        require(name not in names, "Duplicate scenario ID")
        names.add(name)
        require(isinstance(case["runs"], list) and len(case["runs"]) <= 100, "Invalid run count")
        grouped = {}
        evidence = set()
        for run in case["runs"]:
            result = assess_run(experiment, run, schema)
            origins.add(run["origin"])
            evidence.add((run["origin"], run["firmware_sha256"], run["identity_evidence_sha256"], run["sample_interval_ms"]))
            key = (run["interval_ms"], run["repetition"])
            require(key not in grouped, "Duplicate trial/repetition")
            grouped[key] = (result, digest(run))
        require(len(evidence) <= 1, "A scenario must use the same origin, build, identity evidence and cadence")
        expected = {(trial["interval_ms"], rep) for trial in experiment["trials"]
                    for rep in range(scenario["repetitions"])}
        unusable = [result["reason"] for result, _ in grouped.values() if not result["usable"]]
        if set(grouped) != expected or unusable:
            excluded.append(dict(scenario_id=name, reasons=sorted(set(unusable +
                            (["missing_repetitions"] if set(grouped) != expected else []))), case_sha256=digest(case)))
            continue
        cases.append(experiment)
        for trial in experiment["trials"]:
            results = [grouped[(trial["interval_ms"], rep)] for rep in range(scenario["repetitions"])]
            rows.append(dict(scenario_id=name, family_id=scenario["family_id"],
                             interval_ms=trial["interval_ms"], features=features(experiment, trial["interval_ms"], catalogue),
                             label=int(all(result["passes"] for result, _ in results)),
                             run_sha256=[sha for _, sha in results], summaries=[result for result, _ in results]))
    require(len(origins) == 1, "Never mix simulated and hardware data, or use an empty capture")
    return dict(contract="uthome.timing-dataset", version=VERSION, origin=origins.pop(),
                feature_names=FEATURES, bundle_sha256=digest(bundle), cases=cases, rows=rows,
                excluded=excluded, deployment_allowed=False)


def validate_dataset(dataset, bundle, catalogue=None):
    require(canonical(dataset) == canonical(build_dataset(bundle, catalogue)),
            "Dataset differs from the source runs; rebuild it")
