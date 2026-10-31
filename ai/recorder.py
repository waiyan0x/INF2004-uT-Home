"""Identify equipment, explicitly apply one trial, or record an already applied trial."""
import argparse
import json
from pathlib import Path
import sys

from ai.provenance import (check_firmware, check_receipt, check_target, make_identity,
                           make_receipt, retained_build, usb_identity, validate_identity)
from ai.recording import load_recording, record_run, save_new
from ai.records import build_dataset
from ai.timing import digest, export_profile, validate
from tools.config_transfer.client import Client
from tools.configurator.workflow import Workflow, open_serial
from tools.config_validator.planner import ConfigurationError, load_document, require


def apply_trial(workflow, experiment, interval, identity, folder):
    """Explicit setup operation, kept separate from the read-only recorder."""
    validate(experiment)
    check_target(experiment, identity)
    profile = export_profile(experiment, interval)
    check_firmware(workflow.client, identity, active_only=False)
    review = workflow.dispatch("review", {"profile": json.dumps(profile)})
    save_new(Path(folder) / "request.json", dict(experiment_sha256=digest(experiment),
             identity_sha256=digest(identity), interval_ms=interval, review=review))
    reply = workflow.dispatch("apply", {"review_id": review["review_id"]})
    # Always retain the reply, including rejected or saving-failed responses.
    save_new(Path(folder) / "reply.json", reply)
    receipt = make_receipt(experiment, interval, identity, review, reply)
    save_new(Path(folder) / "receipt.json", receipt)
    return receipt


def bundle_recordings(folders):
    cases = {}
    for folder in folders:
        experiment, run = load_recording(folder)
        key = digest(experiment)
        cases.setdefault(key, dict(experiment=experiment, runs=[]))["runs"].append(run)
    bundle = dict(contract="uthome.timing-runs", version="0.1.0", cases=list(cases.values()))
    # This also detects duplicates and retains scenario exclusions.
    build_dataset(bundle)
    return bundle


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    identify = commands.add_parser("identify", help="Probe reviewed I2C wiring on an already idle board")
    identify.add_argument("--build-dir", type=Path, required=True)
    identify.add_argument("--wiring-confirmed", action="store_true")
    for name, help_text in (("apply-trial", "Explicitly review, apply and save one planned trial"),
                            ("record", "Read an already applied trial; never changes profiles")):
        command = commands.add_parser(name, help=help_text)
        command.add_argument("--experiment", type=Path, required=True)
        command.add_argument("--identity", type=Path, required=True)
        command.add_argument("--interval-ms", type=int, required=True)
        command.add_argument("--wiring-unchanged", action="store_true")
        if name == "record":
            command.add_argument("--receipt", type=Path, required=True)
            command.add_argument("--repetition", type=int, required=True)
            command.add_argument("--cadence-ms", type=int, default=200)
        command.add_argument("--port", required=True)
        command.add_argument("--output", type=Path, required=True)
    identify.add_argument("--port", required=True)
    identify.add_argument("--output", type=Path, required=True)
    bundle = commands.add_parser("bundle", help="Verify journals and assemble runs for offline dataset validation")
    bundle.add_argument("recordings", type=Path, nargs="+")
    bundle.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    stream = workflow = None
    owns_output = False
    try:
        require(not args.output.exists(), "Output already exists; choose a fresh path")
        if args.command == "bundle":
            result = bundle_recordings(args.recordings)
            save_new(args.output, result)
            print(json.dumps(dict(cases=len(result["cases"]), output=str(args.output))))
            return 0
        if args.command == "identify":
            require(args.wiring_confirmed, "Confirm bench I2C GPIO0/1 wiring, 3.3 V logic and module identity first")
            build = retained_build(args.build_dir)
            identity = None
        else:
            require(args.wiring_unchanged, "Confirm wiring has not changed since the retained identity observation")
            identity = load_document(args.identity)
            validate_identity(identity)
            experiment = validate(load_document(args.experiment))
            check_target(experiment, identity)
            if args.command == "record":
                receipt = load_document(args.receipt)
                check_receipt(experiment, args.interval_ms, identity, receipt)
        usb = usb_identity(args.port)
        if identity:
            require(usb == identity["usb"], "Selected USB board differs from identity evidence")
        if args.command == "record":
            stream = open_serial(args.port)
            assessment = record_run(Client(stream, timeout=2), experiment, args.interval_ms, args.repetition,
                                    identity, receipt, args.output, cadence_ms=args.cadence_ms)
            print(json.dumps(assessment, indent=2))
            return 0 if assessment["usable"] else 2
        args.output.mkdir(parents=True, exist_ok=False)
        owns_output = True
        if args.command == "identify":
            stream = open_serial(args.port)
            identity = make_identity(Client(stream, timeout=2), usb, build)
            save_new(args.output / "identity.json", identity)
            save_new(args.output / "manifest.json", identity["manifest"])
            print(json.dumps(dict(identity=str(args.output / "identity.json"), firmware=identity["build"])))
        else:
            workflow = Workflow()
            workflow.dispatch("connect", {"port": args.port})
            receipt = apply_trial(workflow, experiment, args.interval_ms, identity, args.output)
            print(json.dumps(dict(receipt=str(args.output / "receipt.json"), reply=receipt["reply"]), indent=2))
        return 0
    except (ConfigurationError, ValueError, KeyError, OSError, RuntimeError, KeyboardInterrupt) as error:
        message = str(error) or type(error).__name__
        if owns_output and not (args.output / "failure.json").exists():
            save_new(args.output / "failure.json", dict(error=message, automatic_retry=False))
        print(message, file=sys.stderr)
        return 1
    finally:
        if workflow is not None:
            workflow.close()
        if stream is not None:
            stream.close()


if __name__ == "__main__":
    raise SystemExit(main())
