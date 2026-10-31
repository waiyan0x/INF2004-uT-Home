"""Explicit study collection with one serial owner and original-profile restoration."""
import argparse
from dataclasses import asdict
import json
from pathlib import Path
import random
import sys
import zlib

from ai.protocol_study import MAX_BYTES, validate_study
from ai.provenance import check_firmware, check_target, usb_identity, utc_now
from ai.recorder import apply_trial
from ai.recording import record_run, save_new
from ai.timing import canonical, digest
from tools.configurator.workflow import Workflow
from tools.config_validator.planner import ConfigurationError, load_document, require


def collect(workflow, port, study, identity, original, output, families, *, execution_context=None, qualification_folder=None,
            collect_costs=False, previous_captures=()):
    """Caller supplies an explicitly selected study subset. Never retry a run."""
    validate_study(study)
    known = {c["experiment"]["scenario"]["family_id"]: c["experiment"] for c in study["cases"]}
    require(families and len(families) == len(set(families)) and set(families) <= set(known), "Invalid selected families")
    require(canonical(original) == canonical(study["base_profile"]), "Restore profile differs from study baseline")
    for family in families:
        check_target(known[family], identity)
    bindings = {}
    if execution_context is not None:
        from ai.static_context import bind_experiment, check_capture
        require(qualification_folder is not None, "Static capture requires retained qualification evidence")
        for family in families:
            binding = bind_experiment(known[family], execution_context)
            # Refuse unqualified context/evidence before opening serial or Apply.
            check_capture(binding, identity["build"]["elf_sha256"], qualification_folder)
            bindings[family] = binding
    from ai.protocol_resume import resume_plan
    continuation = resume_plan(study, identity, previous_captures, families,
                               collect_costs=collect_costs, bindings=bindings if execution_context is not None else None)
    pending = {(r['family_id'], r['interval_ms'], r['repetition']) for r in continuation['missing_runs']}
    require(pending, "Selected families are already complete; no board access required")
    raw = json.dumps(original, ensure_ascii=True, separators=(",", ":")).encode("ascii")
    original_crc = zlib.crc32(raw)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    save_new(output / "study.json", study)
    save_new(output / "identity.json", identity)
    if bindings:
        save_new(output / "static-bindings.json", bindings)
    with (output / "restore-profile.json").open("xb") as file:
        file.write(raw)
    summary = dict(started_utc=utc_now(), study_sha256=digest(study), selected_families=families,
                   runs=[], restored=False, error=None, measured_costs=collect_costs)
    if previous_captures:
        summary.update(previous_captures=continuation['previous_captures'],
                       previously_completed_runs=continuation['completed_runs'])
    changed = False
    try:
        connection = workflow.dispatch("connect", {"port": port})
        summary["before"] = connection
        state = connection["state"]
        require(state["active_crc"] == state["saved_crc"] == original_crc and state["flags"] & 0x70 == 0x50,
                "Connected board does not match the preserved baseline")
        firmware = check_firmware(workflow.client, identity, active_only=True)
        if bindings:
            for binding in bindings.values():
                check_capture(binding, firmware["build_sha256"], qualification_folder)
        order = list(families)
        random.Random(42).shuffle(order)
        summary["family_order"] = order
        for family in order:
            if not any(key[0] == family for key in pending):
                continue
            experiment = known[family]
            folder = output / family
            folder.mkdir()
            save_new(folder / "experiment.json", experiment)
            intervals = [t["interval_ms"] for t in experiment["trials"]]
            random.Random("42-" + family).shuffle(intervals)
            for interval in intervals:
                repetitions = [rep for rep in range(experiment['scenario']['repetitions'])
                               if (family, interval, rep) in pending]
                if not repetitions:
                    continue
                require(not (output / "stop-requested").exists(), "Collection stopped by operator")
                setup = folder / ("apply-" + str(interval))
                setup.mkdir()
                # Even an ambiguous Apply reply requires a separate restore attempt.
                changed = True
                receipt = apply_trial(workflow, experiment, interval, identity, setup)
                for repetition in repetitions:
                    require(not (output / "stop-requested").exists(), "Collection stopped by operator")
                    run_folder = folder / f"run-{interval}-{repetition}"
                    result = record_run(workflow.client, experiment, interval, repetition, identity, receipt,
                                        run_folder, cadence_ms=study["specification"]["cadence_ms"], collect_costs=collect_costs)
                    summary["runs"].append(dict(family_id=family, interval_ms=interval, repetition=repetition,
                                               folder=str(run_folder), usable=result["usable"], reason=result["reason"]))
                    # Keep holdout labels out of progress output until fitting is frozen.
                    print(json.dumps(dict(event="captured", family=family, completed_runs=len(summary["runs"]),
                                          usable=result["usable"])), flush=True)
                    require(result["usable"], "Interrupted capture; retained partial evidence without automatic retry")
    except (Exception, KeyboardInterrupt) as error:
        summary["error"] = str(error) or type(error).__name__
    finally:
        if changed:
            try:
                if workflow.client is None:
                    workflow.dispatch("connect", {"port": port})
                review = workflow.dispatch("review", {"profile": raw.decode("ascii")})
                save_new(output / "restore-review.json", review)
                reply = workflow.dispatch("apply", {"review_id": review["review_id"]})
                summary["restore_reply"] = reply
                require(reply["status"] == 1 and reply["active_crc"] == reply["saved_crc"] == original_crc,
                        "Original profile restoration not confirmed")
                summary["after"] = asdict(workflow.client.telemetry())
                summary["restored"] = True
            except (Exception, KeyboardInterrupt) as error:
                summary["restore_error"] = str(error) or type(error).__name__
        workflow.close()
        summary["finished_utc"] = utc_now()
        save_new(output / "summary.json", summary)
    return summary


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("study", "identity", "restore-profile", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--port", required=True)
    parser.add_argument("--wiring-unchanged", action="store_true")
    parser.add_argument('--measure-costs', action='store_true', help='Require command-9 measurements and zero allocation counters')
    parser.add_argument('--resume-from', nargs='+', type=Path, default=[],
                        help='All prior capture batch folders; verify saved runs and collect only missing repetitions')
    parser.add_argument("--families", nargs="+", required=True, help="Explicit family IDs, or 'all'")
    parser.add_argument("--execution-context", type=Path, required=True, help="Reviewed static-memory execution sidecar")
    parser.add_argument("--qualification-folder", type=Path, required=True, help="Retained evidence referenced by the sidecar")
    args = parser.parse_args(argv)
    try:
        require(args.wiring_unchanged, "Confirm unchanged wiring and close other serial owners")
        require(not args.output.exists(), "Output already exists")
        study = validate_study(load_document(args.study, MAX_BYTES))
        identity = load_document(args.identity)
        require(usb_identity(args.port) == identity["usb"], "Selected board differs from identity evidence")
        families = ([c["experiment"]["scenario"]["family_id"] for c in study["cases"]]
                    if args.families == ["all"] else args.families)
        require((args.execution_context is None) == (args.qualification_folder is None), "Supply both static-context arguments")
        result = collect(Workflow(), args.port, study, identity, load_document(args.restore_profile), args.output, families,
                         execution_context=load_document(args.execution_context) if args.execution_context else None,
                         qualification_folder=args.qualification_folder, collect_costs=args.measure_costs,
                         previous_captures=args.resume_from)
        print(json.dumps(dict(output=str(args.output), completed_runs=len(result["runs"]),
                              restored=result["restored"], error=result["error"], restore_error=result.get("restore_error"))), flush=True)
        return 0 if result["error"] is None and result["restored"] else 2
    except (ConfigurationError, OSError, ValueError, KeyError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
