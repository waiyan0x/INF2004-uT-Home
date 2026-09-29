#!/usr/bin/env python3
"""Apply pinned clang-format plus the narrowly scoped BARR switch-break rule."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

import check


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--format", dest="formatter",
                        default=os.environ.get("BARR_CLANG_FORMAT", "clang-format"))
    parser.add_argument("paths", nargs="*", help="Optional paths relative to --root; defaults to all checked C/H files.")
    arguments = parser.parse_args(argv)
    root = arguments.root.resolve()
    if not (root / ".clang-format").is_file():
        parser.error("The project .clang-format file is missing.")
    try:
        policy = json.loads((root / "tools/c_standard/policy.json").read_text(encoding="utf-8"))
        failures = check.check_format(arguments.formatter, [], policy["clang_format_version"])
    except (OSError, ValueError, KeyError) as exception:
        parser.error(str(exception))
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    allowed = {path.resolve() for path in check.source_files(root)}
    requested = {(root / name).resolve() for name in arguments.paths} if arguments.paths else allowed
    if not requested <= allowed:
        parser.error("Every requested path must be a checked project C/H file.")
    # Finish every formatter invocation before writing anything.  A tool error
    # cannot leave a subset of files rewritten by this command.
    prepared = []
    for path in sorted(requested):
        result = subprocess.run([arguments.formatter, "--style=file", "--fallback-style=none", str(path)],
                                capture_output=True)
        if result.returncode:
            print(result.stderr.decode(errors="replace"), file=sys.stderr)
            return 1
        try:
            formatted = check.canonical_format(result.stdout.decode("utf-8")).encode("utf-8")
        except UnicodeError as exception:
            print(f"{path}: formatter output is not UTF-8: {exception}", file=sys.stderr)
            return 1
        prepared.append((path, formatted))
    changed = 0
    for path, formatted in prepared:
        if path.read_bytes() != formatted:
            path.write_bytes(formatted)
            changed += 1
    print(f"Formatted {len(prepared)} C/H files; changed {changed}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
