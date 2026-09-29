"""Validate a profile and print its normalized plan. Never activates hardware."""
import argparse
import json
import sys
from .planner import Catalogue, ConfigurationError, build_plan, load_document


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile")
    parser.add_argument("--build-manifest", required=True,
                        help="Explicit host fixture or selected firmware build capabilities")
    args = parser.parse_args()
    try:
        plan = build_plan(load_document(args.profile, 8192), load_document(args.build_manifest), Catalogue())
    except (ConfigurationError, OSError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(json.dumps(plan, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
