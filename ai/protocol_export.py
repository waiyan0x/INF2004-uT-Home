"""Export the experimental eighteen-input classifier; never enable deployment."""
import argparse
import json
from pathlib import Path

from ai.export import _cross_check, _export_c, _verify_c
from ai.protocol_model import predict, validate_model
from ai.recording import save_new
from ai.timing import digest
from ai.workload import FEATURES
from tools.config_validator.planner import load_document


def export_c(model, folder):
    validate_model(model)
    report = _export_c(model, folder, "protree")
    report.update(contract="uthome.protocol-c-export", version="0.1.0",
                  input_contract=dict(name=model["contract"], version=model["version"],
                                      features=FEATURES, storage="uint32_t[18]"),
                  context_hashes=model["contexts"], context_check="required_by_caller",
                  threshold_per_mille=900, max_node_visits=5, max_candidates=4,
                  writable_global_bytes=0, feature_bytes=72,
                  qualification="experimental_only", automatic_apply=False)
    save_new(Path(folder) / "manifest.json", report)
    return report


def verify_c(model, folder, extra_vectors=()):
    validate_model(model)
    manifest = load_document(Path(folder) / "manifest.json")
    if manifest["model_sha256"] != digest(model):
        raise ValueError("Generated export belongs to another model")
    return _verify_c(model, folder, "protree", "pro_main.c", predict, extra_vectors)


def cross_check(folder, compiler):
    return _cross_check(folder, compiler, "protree")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cross-compiler", type=Path)
    args = parser.parse_args(argv)
    model = load_document(args.model)
    report = dict(export=export_c(model, args.output))
    report["parity"] = verify_c(model, args.output)
    if args.cross_compiler:
        report["target_object"] = cross_check(args.output, args.cross_compiler)
    save_new(args.output / "verification.json", report)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
