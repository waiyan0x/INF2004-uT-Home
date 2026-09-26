from __future__ import annotations

import argparse
import json
from pathlib import Path

from . import ScenarioError, run_scenario


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("scenario")
    parser.add_argument("--output", required=True)
    parser.add_argument("--evidence")
    parser.add_argument("--require-complete", action="store_true")
    args = parser.parse_args()
    try:
        report = run_scenario(Path(args.scenario), Path(args.evidence) if args.evidence else None)
    except (OSError, ValueError, ScenarioError) as error:
        parser.exit(2, f"Scenario error: {error}\n")
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Scenario {report['scenario_id']}: {'PASS' if report['host_checks_passed'] else 'FAIL'}")
    for result in report["results"]:
        print(f"- {result['step']}: {result['status']}")
    return 0 if report["host_checks_passed"] and (not args.require_complete or report["demonstration_complete"]) else 2


if __name__ == "__main__":
    raise SystemExit(main())
