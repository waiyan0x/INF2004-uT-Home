"""Report known static-memory blockers without certifying absence of others."""
import argparse
import hashlib
import json
import re
from pathlib import Path

from ai.recording import save_new
from ai.static_context import check_evidence, qualification_reasons
from tools.config_validator.planner import ROOT, ConfigurationError, load_document


KNOWN_PATHS = {
    "firmware/src/config/cfgjson.c": ("malloc(", "free("),
    "firmware/src/runtime/tkport.c": (),
    "firmware/hal/ports/esp32c3/c3store.c": ("esp_partition_find_first(",),
}


def inspect_source(root=ROOT):
    findings, hashes, startup_calls = [], {}, []
    for name, tokens in KNOWN_PATHS.items():
        raw = (Path(root) / name).read_bytes()
        hashes[name] = hashlib.sha256(raw).hexdigest()
        source = raw.decode('utf-8')
        # Keep offsets/newlines while excluding prose and string literals.
        masked = re.sub(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                        lambda m: ''.join('\n' if c == '\n' else ' ' for c in m.group()), source)
        for number, line in enumerate(masked.splitlines(), 1):
            if any(token in line for token in tokens):
                findings.append(dict(path=name, line=number, source=line.strip()))
        if name.endswith('/tkport.c'):
            start = re.search(r'\bint\s+rt_port_init\s*\(\s*void\s*\)\s*\{', masked)
            end, depth = start.end() if start else 0, 1
            while start and end < len(masked) and depth:
                depth += (masked[end] == '{') - (masked[end] == '}')
                end += 1
            for call in re.finditer(r'\btk_(?:cre|del)_[a-z0-9_]+\s*\(', masked):
                item = dict(path=name, line=masked.count('\n', 0, call.start()) + 1,
                            source=source.splitlines()[masked.count('\n', 0, call.start())].strip())
                if (start and depth == 0 and start.end() <= call.start() < end
                        and re.fullmatch(r'tk_cre_tsk\s*\(', call.group())):
                    startup_calls.append(item)
                else:
                    findings.append(item)
            # The reviewed permanent-worker design has one bounded creation
            # callsite in init. Zero/multiple sites require another review.
            if len(startup_calls) != 1:
                findings.append(dict(path=name, line=0, source='permanent_worker_startup_callsite_changed'))
    return dict(findings=findings, startup_calls_requiring_runtime_evidence=startup_calls, source_sha256=hashes,
                scope="known_blocker_check_only_not_complete_allocation_audit")


def report(context=None, running_elf_sha256=None, qualification_folder=None):
    source = inspect_source()
    reasons = (["missing_reviewed_execution_context"] if context is None
               else qualification_reasons(context, running_elf_sha256))
    if source["findings"]:
        reasons.append("known_allocation_or_lifecycle_paths_require_review")
    if context is not None:
        try:
            check_evidence(context, qualification_folder)
        except (ConfigurationError, OSError) as error:
            reasons.append("qualification_artifacts_not_verified: " + str(error))
    return dict(contract="uthome.static-ai-readiness", version="0.1.0", source=source,
                capture_allowed=not reasons, deployment_allowed=False, blockers=reasons,
                note="Code scans cannot establish zero allocation or qualified hardware; retained runtime evidence is required.")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--context", type=Path)
    parser.add_argument("--running-elf-sha256")
    parser.add_argument("--qualification-folder", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    result = report(load_document(args.context) if args.context else None, args.running_elf_sha256, args.qualification_folder)
    if args.output:
        save_new(args.output, result)
    print(json.dumps(result, indent=2))
    return 0 if result["capture_allowed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
