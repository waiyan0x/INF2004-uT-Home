"""Command-line entry point for the Member 5 configurator."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .bundle import BundleError, create_bundle, deploy_to_filesystem, verify_bundle
from .generator import GenerationError, generate_profile
from .validator import validate_profile
from .workflow import WorkflowError, new_candidate, review_candidate


def _read(path: str) -> object:
    return json.loads(Path(path).read_text(encoding="utf-8"))


def _write(path: str, value: object) -> None:
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="python3 -m tools.configurator")
    commands = parser.add_subparsers(dest="command", required=True)
    validate = commands.add_parser("validate")
    validate.add_argument("profile")
    generate = commands.add_parser("generate")
    generate.add_argument("recommendation")
    generate.add_argument("--output", required=True)
    review = commands.add_parser("approve")
    review.add_argument("candidate")
    review.add_argument("--reviewer", required=True)
    review.add_argument("--note", default="")
    review.add_argument("--output", required=True)
    reject = commands.add_parser("reject")
    reject.add_argument("candidate")
    reject.add_argument("--reviewer", required=True)
    reject.add_argument("--note", default="")
    reject.add_argument("--output", required=True)
    bundle = commands.add_parser("bundle")
    bundle.add_argument("approved")
    bundle.add_argument("--output", required=True)
    bundle.add_argument("--model")
    verify = commands.add_parser("verify-bundle")
    verify.add_argument("bundle")
    deploy = commands.add_parser("deploy-filesystem")
    deploy.add_argument("bundle")
    deploy.add_argument("destination")
    serve = commands.add_parser("serve")
    serve.add_argument("--host", default="127.0.0.1")
    serve.add_argument("--port", type=int, default=8080)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "validate":
            result = validate_profile(_read(args.profile))
            print(json.dumps(result.to_dict(), indent=2))
            return 0 if result.valid else 2
        if args.command == "generate":
            profile = generate_profile(_read(args.recommendation))
            _write(args.output, new_candidate(profile, source=args.recommendation))
        elif args.command in {"approve", "reject"}:
            decision = "approved" if args.command == "approve" else "rejected"
            _write(args.output, review_candidate(_read(args.candidate), decision, args.reviewer, args.note))
        elif args.command == "bundle":
            manifest = create_bundle(_read(args.approved), Path(args.output), Path(args.model) if args.model else None)
            print(json.dumps(manifest, indent=2))
        elif args.command == "verify-bundle":
            print(json.dumps(verify_bundle(Path(args.bundle)), indent=2))
        elif args.command == "deploy-filesystem":
            print(json.dumps(deploy_to_filesystem(Path(args.bundle), Path(args.destination)), indent=2))
        elif args.command == "serve":
            from .web import serve
            serve(args.host, args.port)
        return 0
    except (OSError, json.JSONDecodeError, GenerationError, WorkflowError, BundleError) as error:
        print(json.dumps({"error": str(error)}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
