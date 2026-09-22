from __future__ import annotations

import argparse
import json
from pathlib import Path

from . import run_scenario


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("scenario")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    report = run_scenario(Path(args.scenario))
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Scenario {report['scenario_id']}: {'PASS' if report['host_checks_passed'] else 'FAIL'}")
    for result in report["results"]:
        print(f"- {result['step']}: {result['status']}")
    return 0 if report["host_checks_passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
