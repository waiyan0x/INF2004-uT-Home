"""Command-line harness for uT-Home AI contracts."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .contracts import (
    ContractValidationError,
    encode_feature_vector,
    load_json_document,
    validate_exchange,
    validate_recommendation,
    validate_request,
)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Validate uT-Home AI contract documents and encode model inputs."
    )
    commands = parser.add_subparsers(dest="command", required=True)

    request = commands.add_parser("request", help="validate a feature request")
    request.add_argument("request_file", type=Path)

    response = commands.add_parser("response", help="validate a recommendation")
    response.add_argument("response_file", type=Path)

    encode = commands.add_parser("encode", help="validate and encode a feature request")
    encode.add_argument("request_file", type=Path)

    exchange = commands.add_parser(
        "exchange", help="validate a correlated request and recommendation"
    )
    exchange.add_argument("request_file", type=Path)
    exchange.add_argument("response_file", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)

    try:
        if args.command == "request":
            validate_request(load_json_document(args.request_file))
            print("VALID request")
        elif args.command == "response":
            validate_recommendation(load_json_document(args.response_file))
            print("VALID recommendation")
        elif args.command == "encode":
            vector = encode_feature_vector(load_json_document(args.request_file))
            print(json.dumps(vector, separators=(",", ":")))
        elif args.command == "exchange":
            validate_exchange(
                load_json_document(args.request_file),
                load_json_document(args.response_file),
            )
            print("VALID exchange")
    except (ContractValidationError, json.JSONDecodeError, OSError) as error:
        print(f"INVALID: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
