"""Bounded read-only timing capture and verification of its retained journal."""
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import secrets
import time

from ai.provenance import check_firmware, check_receipt, config_key
from ai.records import assess_run
from ai.timing import canonical, digest, validate
from tools.config_validator.planner import load_document, require


FAULT_EVENTS = [0, 1, 2, 3, 5, 6, 7]
MAX_RAW_BYTES = 64 * 1024 * 1024


def save_new(path, value):
    with Path(path).open("xb") as output:
        output.write(canonical(value) + b"\n")


def sample_from_snapshot(experiment, snapshot, elapsed, session, profile_hash, expected_config,
                         generation, previous):
    require(snapshot["result"] == 0, "telemetry_unavailable")
    require(config_key(snapshot["configuration"]) == expected_config, "configuration_changed")
    require(snapshot["runtime"]["generation"] == generation, "runtime_generation_changed")
    counters = snapshot["health"]["event_counts"]
    require(len(counters) == 8, "health_contract_changed")
    if previous is not None:
        require(snapshot["timestamp_ms"] > previous["timestamp_ms"], "reset_or_reordered_uptime")
        require(all(a <= b for a, b in zip(previous["health"]["event_counts"], counters)), "counter_reset")
    name = experiment["target"]["instance_id"]
    devices, queues = experiment["profile"]["devices"], experiment["profile"]["queues"]
    index = next(i for i, d in enumerate(devices) if d["device_id"] == name)
    queue = next(i for i, q in enumerate(queues) if q["id"] == devices[index]["behaviour"]["destination"])
    matching = [s for s in snapshot["samples"] if s["name"] == name]
    require(len(matching) <= 1, "duplicate_target")
    row = matching[0] if matching else None
    if row:
        require((row["source_index"], row["queue"], row["type"]) == (index, queue, 1), "source_identity_changed")
    return dict(elapsed_ms=elapsed, uptime_ms=snapshot["timestamp_ms"], session_id=session,
                profile_sha256=profile_hash, metric_ms=row["age_ms"] if row else None,
                quality=row["quality"] if row else "missing", health_faults=sum(counters[i] for i in FAULT_EVENTS))


def runtime_healthy(snapshot):
    runtime = snapshot["runtime"]
    return runtime["state"] == 1 and all(runtime[k] == 0 for k in
        ("failed_workers", "service_error", "apply_error", "recovery_error"))


def record_run(client, experiment, interval, repetition, identity, receipt, folder, *,
               cadence_ms=200, clock=time.monotonic, sleep=time.sleep, collect_costs=False):
    """Caller owns one borrowed client. Never APPLY, SAVE, RESET or retry.

    Pre/post discovery must return BUSY on the active profile; this supplies
    the running ELF hash without borrowing its I2C bus. Wiring was confirmed
    separately when producing identity evidence.
    """
    experiment = validate(experiment)
    expected = check_receipt(experiment, interval, identity, receipt)
    require(type(cadence_ms) is int and 100 <= cadence_ms <= 1000, "Cadence must be 100..1000 ms")
    require(type(repetition) is int and 0 <= repetition < experiment["scenario"]["repetitions"], "Unplanned repetition")
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=False)
    save_new(folder / "experiment.json", experiment)
    session = "capture-" + secrets.token_hex(12)
    profile_hash = next(t["profile_sha256"] for t in experiment["trials"] if t["interval_ms"] == interval)
    run = dict(contract="uthome.timing-run", version="0.2.0", experiment_sha256=digest(experiment),
               interval_ms=interval, repetition=repetition, origin="hardware", method="telemetry_sample_age",
               firmware_sha256=identity["build"]["binary_sha256"], identity_evidence_sha256=digest(identity),
               session_id=session, sample_interval_ms=cadence_ms, samples=[],
               capture=dict(status="interrupted", reason="not_started", clock="host_receive_monotonic_ms",
                            terminal_tolerance_ms=2 * cadence_ms, fault_event_codes=FAULT_EVENTS,
                            runtime_healthy=True, raw_sha256="0" * 64),
               evidence=dict(identity=identity, receipt=receipt, firmware_before=None, firmware_after=None))
    end_ms = experiment["scenario"]["warmup_ms"] + experiment["scenario"]["duration_ms"]
    hasher, raw_size = hashlib.sha256(), 0
    costs, cost_summary = [], None
    cost_due_ms = experiment['scenario']['warmup_ms']
    with (folder / "raw.jsonl").open("xb") as output:
        def journal(value):
            nonlocal raw_size
            raw = canonical(value) + b"\n"
            require(raw_size + len(raw) <= MAX_RAW_BYTES, "raw_journal_limit")
            output.write(raw)
            output.flush()
            hasher.update(raw)
            raw_size += len(raw)

        journal(dict(type="header", experiment_sha256=digest(experiment), session_id=session,
                     interval_ms=interval, repetition=repetition, sample_interval_ms=cadence_ms,
                     identity_sha256=digest(identity), receipt_sha256=digest(receipt)))
        try:
            before = asdict(client.telemetry())
            journal(dict(type="preflight", snapshot=before))
            require(before["result"] == 0 and config_key(before["configuration"]) == expected
                    and before["runtime"]["retained_instances"] > 0, "applied_trial_not_active")
            firmware = check_firmware(client, identity, active_only=True)
            require(firmware["active_revision"] == expected[0], "configuration_changed_during_build_check")
            run["evidence"]["firmware_before"] = firmware
            journal(dict(type="firmware_before", value=run["evidence"]["firmware_before"]))
            generation, previous = before["runtime"]["generation"], before
            cost_context = dict(identity=identity, profile_hash=profile_hash, expected=expected,
                                generation=generation, sources=[i for i, d in enumerate(experiment['profile']['devices'])
                                                                if d['enabled']])
            if collect_costs:
                from ai import cost_capture
                cost_capture.check_audit(before)
            origin = None
            due = clock()
            for _ in range(7000):
                sleep(max(0, due - clock()))
                sent = clock()
                snapshot = asdict(client.telemetry())
                received = clock()
                require(received >= sent, "host_clock_reversed")
                if origin is None:
                    origin = received
                elapsed = round((received - origin) * 1000)
                journal(dict(type="sample", elapsed_ms=elapsed, rtt_ms=round((received - sent) * 1000), snapshot=snapshot))
                row = sample_from_snapshot(experiment, snapshot, elapsed, session, profile_hash, expected, generation, previous)
                run["samples"].append(row)
                run["capture"]["runtime_healthy"] &= runtime_healthy(snapshot)
                if collect_costs:
                    cost_capture.check_audit(snapshot)
                    if elapsed >= cost_due_ms:
                        observed = dict(type='workcost', elapsed_ms=elapsed,
                                        response=cost_capture.observe(client, **cost_context))
                        journal(observed)
                        costs.append(observed)
                        cost_due_ms = elapsed + 1000
                if len(run["samples"]) > 1:
                    gap = elapsed - run["samples"][-2]["elapsed_ms"]
                    require(0 < gap <= 2 * cadence_ms, "sampling_gap_or_reordered_time")
                previous = snapshot
                if elapsed >= end_ms:
                    require(elapsed <= end_ms + 2 * cadence_ms, "terminal_overshoot")
                    firmware = check_firmware(client, identity, active_only=True)
                    require(firmware["active_revision"] == expected[0], "configuration_changed_during_build_check")
                    run["evidence"]["firmware_after"] = firmware
                    after = asdict(client.telemetry())
                    journal(dict(type="postflight", snapshot=after, firmware=run["evidence"]["firmware_after"]))
                    sample_from_snapshot(experiment, after, elapsed, session, profile_hash, expected, generation, previous)
                    if collect_costs:
                        cost_capture.check_audit(after)
                        cost_summary = cost_capture.summarize(costs, **cost_context)
                    # Faults after the measured window are retained in raw evidence;
                    # the last measured snapshot remains the endpoint for labels.
                    run["capture"].update(status="complete", reason=None)
                    break
                due = max(origin + (len(run["samples"]) * cadence_ms / 1000), received)
            else:
                raise ValueError("sample_limit")
        except (Exception, KeyboardInterrupt) as error:
            run["capture"].update(status="interrupted", reason=(str(error) or type(error).__name__)[:250])
        journal(dict(type="footer", capture={k: v for k, v in run["capture"].items() if k != "raw_sha256"}))
    run["capture"]["raw_sha256"] = hasher.hexdigest()
    # Preflight failures cannot claim a correlated hardware run.
    if run["evidence"]["firmware_before"] is None:
        save_new(folder / "failure.json", dict(reason=run["capture"]["reason"], raw_sha256=hasher.hexdigest()))
        return dict(usable=False, passes=None, reason=run["capture"]["reason"])
    save_new(folder / "run.json", run)
    assessment = assess_run(experiment, run)
    save_new(folder / "assessment.json", assessment)
    if cost_summary is not None:
        save_new(folder / 'workcost.json', dict(cost_summary, raw_sha256=hasher.hexdigest(), run_sha256=digest(run)))
    return assessment


def load_recording(folder):
    """Verify retained journal correlation before adding a run to a dataset."""
    folder = Path(folder)
    experiment = validate(load_document(folder / "experiment.json"))
    run = load_document(folder / "run.json", MAX_RAW_BYTES)
    require((folder / "raw.jsonl").stat().st_size <= MAX_RAW_BYTES, "Raw journal exceeds size bound")
    raw = (folder / "raw.jsonl").read_bytes()
    require(hashlib.sha256(raw).hexdigest() == run["capture"]["raw_sha256"], "Raw journal hash mismatch")
    rows = [json.loads(line) for line in raw.splitlines()]
    header, footer = rows[0], rows[-1]
    require(header["type"] == "header" and footer["type"] == "footer", "Incomplete raw journal")
    require(header["experiment_sha256"] == digest(experiment) and header["session_id"] == run["session_id"]
            and header["identity_sha256"] == digest(run["evidence"]["identity"])
            and header["receipt_sha256"] == digest(run["evidence"]["receipt"])
            and all(header[k] == run[k] for k in ("interval_ms", "repetition", "sample_interval_ms")),
            "Journal header differs from run")
    require(canonical(footer["capture"]) == canonical({k: v for k, v in run["capture"].items() if k != "raw_sha256"}),
            "Journal outcome differs from run")
    before = rows[1]["snapshot"]
    require(rows[1]["type"] == "preflight" and rows[2]["type"] == "firmware_before"
            and canonical(rows[2]["value"]) == canonical(run["evidence"]["firmware_before"]), "Missing firmware journal evidence")
    expected = check_receipt(experiment, run["interval_ms"], run["evidence"]["identity"], run["evidence"]["receipt"])
    require(before["result"] == 0 and config_key(before["configuration"]) == expected
            and before["runtime"]["retained_instances"] > 0, "Invalid journal preflight")
    profile_hash = next(t["profile_sha256"] for t in experiment["trials"] if t["interval_ms"] == run["interval_ms"])
    measured = [r for r in rows if r["type"] == "sample"]
    require(len(measured) >= len(run["samples"]), "Run has unrecorded samples")
    previous = before
    healthy = True
    for actual, recorded in zip(measured, run["samples"]):
        expected_row = sample_from_snapshot(experiment, actual["snapshot"], actual["elapsed_ms"], run["session_id"],
                                            profile_hash, expected, before["runtime"]["generation"], previous)
        require(expected_row == recorded, "Run sample differs from raw telemetry")
        healthy &= runtime_healthy(actual["snapshot"])
        previous = actual["snapshot"]
    require(healthy == run["capture"]["runtime_healthy"], "Runtime health differs from journal")
    if run["capture"]["status"] == "complete":
        require(len(measured) == len(run["samples"]) and rows[-2]["type"] == "postflight"
                and canonical(rows[-2]["firmware"]) == canonical(run["evidence"]["firmware_after"]), "Missing terminal evidence")
        sample_from_snapshot(experiment, rows[-2]["snapshot"], run["samples"][-1]["elapsed_ms"], run["session_id"],
                             profile_hash, expected, before["runtime"]["generation"], previous)
    assess_run(experiment, run)
    cost_rows = [r for r in rows if r['type'] == 'workcost']
    if cost_rows and run['capture']['status'] == 'complete':
        from ai import cost_capture
        for row in [r for r in rows if r['type'] in ('preflight', 'sample', 'postflight')]:
            cost_capture.check_audit(row['snapshot'])
        summary = cost_capture.summarize(cost_rows, identity=run['evidence']['identity'],
            profile_hash=profile_hash, expected=expected, generation=before['runtime']['generation'],
            sources=[i for i, d in enumerate(experiment['profile']['devices']) if d['enabled']])
        require(load_document(folder / 'workcost.json') ==
                dict(summary, raw_sha256=run['capture']['raw_sha256'], run_sha256=digest(run)),
                'Workcost summary differs from retained raw measurements')
    return experiment, run
