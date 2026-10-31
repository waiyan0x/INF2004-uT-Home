"""Separate fixed resource reservations from model workload features.

This sidecar does not reinterpret historical experiment/run versions. A matching
reviewed qualification record is required before deployment-qualifying capture;
passing this structural check alone is not a hardware or memory audit.
"""
from copy import deepcopy
from pathlib import Path

from ai.timing import digest, integer_numbers, validate
from tools.config_validator.planner import ROOT, check, load_document, require


CONTRACT = "uthome.execution-context"
VERSION = "0.1.0"
REQUIRED_CHECKS = (
    "startup_heap", "runtime_heap", "kernel_objects", "module_ram_budget",
    "stack_headroom", "apply_rollback_recovery", "parser_exhaustion",
    "queue_capacity", "discovery_ownership", "usb_disconnect",
)


def validate_context(context):
    integer_numbers(context)
    check(context, load_document(ROOT / "ai/contracts/execution-context.schema.json"), "execution context")
    workers = context["workers"]
    require(len(workers["stack_bytes"]) == workers["slots"], "Each permanent slot needs its own stack capacity")
    require(sum(workers["stack_bytes"]) == workers["reserved_stack_bytes"], "Stack reservation must include idle slots")
    require(set(context["qualification"]["checks"]) == set(REQUIRED_CHECKS), "Incomplete qualification checklist")
    require(context["ai"]["stack_accounting"] == "inside_controller_stack", "Do not allocate or double-count another AI stack")
    return context


def qualification_reasons(context, running_elf_sha256=None):
    """Unknown evidence blocks capture instead of silently becoming zero."""
    validate_context(context)
    reasons = []
    q = context["qualification"]
    if running_elf_sha256 != context["firmware_elf_sha256"]:
        reasons.append("running_firmware_not_verified")
    if not context["workers"]["created_at_startup"]:
        reasons.append("workers_not_permanent")
    for name in REQUIRED_CHECKS:
        if q["checks"][name] != "passed":
            reasons.append(name + "_not_qualified")
    for name in ("startup_application_heap_calls", "runtime_application_heap_calls", "post_startup_kernel_creations"):
        if q[name] != 0:
            reasons.append(name + "_not_zero")
    if q["apply_cycles"] < 20:
        reasons.append("fewer_than_20_apply_cycles")
    if not q["evidence_sha256"]:
        reasons.append("missing_qualification_evidence")
    if context["parser"]["workspace_bytes"] is None:
        reasons.append("parser_workspace_unbudgeted")
    return reasons


def bind_experiment(experiment, context):
    """Freeze an execution sidecar without changing any legacy feature hashes."""
    validate(experiment)
    validate_context(context)
    active = [d for d in experiment["profile"]["devices"] if d["enabled"]]
    require(experiment["resources"]["active_tasks"] == len(active),
            "active_tasks must count bound workloads, not permanent task objects")
    workers = context["workers"]
    require(len(active) <= workers["slots"], "Workload exceeds reserved worker capacity")
    # Slots are interchangeable; admit only when a complete size matching exists.
    requests = sorted((d["task"]["stack_size_bytes"] for d in active), reverse=True)
    capacities = sorted(workers["stack_bytes"], reverse=True)
    require(all(request <= available for request, available in zip(requests, capacities)),
            "A workload exceeds permanent slot stack capacity")
    require(len(experiment["trials"]) <= context["ai"]["max_candidates"], "Candidate list exceeds fixed advice capacity")
    return dict(contract="uthome.static-experiment", version=VERSION,
                experiment=deepcopy(experiment), execution_context=deepcopy(context),
                experiment_sha256=digest(experiment), execution_context_sha256=digest(context),
                active_workloads=len(active), reserved_workers=workers["slots"],
                reserved_stack_bytes=workers["reserved_stack_bytes"])


def check_capture(binding, running_elf_sha256, evidence_folder):
    """Require exact replay plus retained evidence hashes before starting a run."""
    expected = bind_experiment(binding["experiment"], binding["execution_context"])
    require(binding == expected, "Static experiment binding changed")
    reasons = qualification_reasons(binding["execution_context"], running_elf_sha256)
    require(not reasons, "Static baseline is not qualified: " + ", ".join(reasons))
    check_evidence(binding["execution_context"], evidence_folder)
    return expected


def check_evidence(context, evidence_folder):
    """Check retained artifact bytes, including path containment, without serial."""
    import hashlib
    validate_context(context)
    require(evidence_folder is not None, "Qualification evidence folder is required")
    root = Path(evidence_folder).resolve()
    require(context["qualification"]["evidence_sha256"], "Qualification evidence is missing")
    for name, expected_hash in context["qualification"]["evidence_sha256"].items():
        path = (root / name).resolve()
        require(path.is_relative_to(root) and path.is_file(), "Qualification evidence escaped its retained folder or is missing")
        with path.open("rb") as source:
            require(hashlib.file_digest(source, "sha256").hexdigest() == expected_hash, "Qualification evidence hash mismatch")
