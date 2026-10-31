"""Correlate a bench identity, exact applied bytes and a retained target build.

These are reproducibility records, not cryptographic device attestation. Wiring
confirmation must be renewed by the operator if the physical setup changes.
"""
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import zlib

from ai.timing import canonical, digest, export_profile
from tools.config_transfer.capabilities import Capabilities
from tools.config_transfer.discovery import Discovery
from tools.config_validator.planner import ROOT, load_document, require
from tools.device_discovery import identify_i2c


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def caps_key(caps):
    return {k: v for k, v in caps.items() if k != "request_id"}


def config_key(config):
    return [config[k] for k in ("active_revision", "saved_revision", "active_crc", "saved_crc")] + [config["flags"] & 0x70]


def usb_identity(port):
    """Enumerate afresh; a COM name alone is not board identity."""
    from serial.tools import list_ports
    matches = [p for p in list_ports.comports() if p.device.lower() == port.lower()]
    require(len(matches) == 1, "The selected serial port is not uniquely present")
    p = matches[0]
    require(p.vid is not None and p.pid is not None and p.serial_number,
            "USB VID, PID and serial number are required")
    return dict(vid=p.vid, pid=p.pid, serial_number=p.serial_number)


def retained_build(folder):
    folder = Path(folder)
    evidence = load_document(folder / "evidence.json", 1024 * 1024)
    require(evidence.get("build_status") == "passed" and evidence.get("target") == "esp32c3"
            and evidence.get("configuration_application") is True, "A passed C3 application build is required")
    result = {"evidence_sha256": hashlib.sha256((folder / "evidence.json").read_bytes()).hexdigest()}
    for name, key in (("uthome_runtime_port.elf", "elf_sha256"), ("uthome_runtime_port.bin", "binary_sha256")):
        path = folder / "build" / name
        require(0 < path.stat().st_size <= 64 * 1024 * 1024, "Invalid build artifact size")
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        require(actual == evidence["artifacts"]["build/" + name]["sha256"], "Build artifact hash mismatch")
        result[key] = actual
    return result


def make_identity(client, usb, build):
    """Explicit reviewed I2C discovery only; never stop an active profile."""
    before = asdict(client.telemetry())
    require(before["result"] == 0 and before["runtime"]["retained_instances"] == 0,
            "Apply an empty profile separately before identifying equipment")
    discovery, caps = client.discover_i2c(wiring_confirmed=True)
    require(discovery.result == "complete" and discovery.build_sha256 == build["elf_sha256"],
            "Discovery did not complete on the retained firmware")
    after = asdict(client.telemetry())
    require(after["result"] == 0 and config_key(before["configuration"]) == config_key(after["configuration"])
            and after["timestamp_ms"] >= before["timestamp_ms"], "Board changed during discovery")
    manifest = load_document(ROOT / "configs/builds/host-reference.json")
    manifest.update(backend="target", board=caps.board, drivers=list(caps.drivers),
                    build_id="measured-" + build["binary_sha256"],
                    qualification="Observed compiled bindings; physical acceptance remains incomplete.")
    result = dict(contract="uthome.bench-identity", version="0.1.0", recorded_utc=utc_now(),
                  usb=usb, build=build, capabilities=asdict(caps), discovery=asdict(discovery), manifest=manifest)
    validate_identity(result)
    return result


def validate_identity(identity):
    require(identity.get("contract") == "uthome.bench-identity" and identity.get("version") == "0.1.0",
            "Unsupported bench identity")
    require(set(identity) == {"contract", "version", "recorded_utc", "usb", "build", "capabilities", "discovery", "manifest"},
            "Unexpected identity fields")
    require(set(identity["usb"]) == {"vid", "pid", "serial_number"} and identity["usb"]["serial_number"],
            "Missing USB identity")
    for key in ("elf_sha256", "binary_sha256", "evidence_sha256"):
        value = identity["build"][key]
        require(isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value),
                "Invalid build hash")
    observation = Discovery(**identity["discovery"])
    caps = Capabilities(**identity["capabilities"])
    require(observation.result == "complete" and observation.build_sha256 == identity["build"]["elf_sha256"],
            "Identity has no successful discovery on the retained build")
    require(identity["manifest"]["backend"] == "target" and identity["manifest"]["board"] == caps.board
            and identity["manifest"]["drivers"] == list(caps.drivers)
            and identity["manifest"]["build_id"] == "measured-" + identity["build"]["binary_sha256"],
            "Manifest does not match observed build bindings")
    return identify_i2c(observation, caps)


def check_target(experiment, identity):
    """First live metric: an identified periodic BMP388 on the reviewed bus."""
    report = validate_identity(identity)
    require(canonical(experiment["build_manifest"]) == canonical(identity["manifest"]), "Experiment build differs from identity")
    target = experiment["target"]
    require(target["execution_kind"] == "periodic" and target["descriptor"]["id"] == "bmp388"
            and experiment["scenario"]["goal"]["metric"] == "observed_sample_age_ms",
            "Live recording currently supports BMP388 observed sample age only")
    device = next(d for d in experiment["profile"]["devices"] if d["device_id"] == target["instance_id"])
    bus = next(b for b in experiment["profile"]["interfaces"] if b["id"] == device["interface"]["ref"])
    require((bus["protocol"], bus["port"], bus["sda_gpio"], bus["scl_gpio"]) == ("i2c", 0, 0, 1),
            "Target wiring differs from the identified connection")
    require(any(d["identity"]["status"] == "verified" and d["identity"]["selected"] == target["descriptor"]
                and d["observation"]["address"] == int(device["interface"]["address"], 16)
                for d in report["devices"]), "Target address/model was not identified")
    return device


def check_firmware(client, identity, *, active_only):
    """On an active profile discovery returns BUSY + build hash without bus I/O."""
    result, caps = client.discover_i2c(wiring_confirmed=True)
    require(result.result in (("busy",) if active_only else ("busy", "complete")), "Unexpected build observation result")
    require(result.build_sha256 == identity["build"]["elf_sha256"], "Running firmware differs from retained build")
    require(canonical(caps_key(asdict(caps))) == canonical(caps_key(identity["capabilities"])), "Compiled capabilities changed")
    return asdict(result)


def make_receipt(experiment, interval, identity, review, reply):
    profile = export_profile(experiment, interval)
    raw = json.dumps(profile, ensure_ascii=True, separators=(",", ":")).encode("ascii")
    require(review["sha256"] == hashlib.sha256(raw).hexdigest(), "Reviewed bytes differ from the trial")
    result = dict(contract="uthome.trial-receipt", version="0.1.0", recorded_utc=utc_now(),
                  identity_sha256=digest(identity), experiment_sha256=digest(experiment), interval_ms=interval,
                  payload=raw.decode("ascii"), payload_sha256=review["sha256"], reply=reply)
    check_receipt(experiment, interval, identity, result)
    return result


def check_receipt(experiment, interval, identity, receipt):
    check_target(experiment, identity)
    require(receipt.get("contract") == "uthome.trial-receipt" and receipt.get("version") == "0.1.0",
            "Unsupported trial receipt")
    require(receipt["identity_sha256"] == digest(identity) and receipt["experiment_sha256"] == digest(experiment)
            and receipt["interval_ms"] == interval, "Receipt refers to another identity/experiment/trial")
    raw = receipt["payload"].encode("ascii")
    require(0 < len(raw) <= 8192 and hashlib.sha256(raw).hexdigest() == receipt["payload_sha256"], "Receipt payload hash mismatch")
    require(canonical(json.loads(raw)) == canonical(export_profile(experiment, interval)), "Applied payload differs from planned trial")
    reply = receipt["reply"]
    require(reply["command"] == 1 and reply["status"] == 1 and reply["revision"] > 0
            and reply["active_revision"] == reply["saved_revision"] == reply["revision"]
            and reply["active_crc"] == reply["saved_crc"] == zlib.crc32(raw)
            and reply["flags"] & 0x70 == 0x50, "Trial was not acknowledged active and saved with the exact payload CRC")
    return config_key(reply)


def check_run_evidence(experiment, run):
    evidence = run["evidence"]
    identity, receipt = evidence["identity"], evidence["receipt"]
    check_receipt(experiment, run["interval_ms"], identity, receipt)
    require(run["firmware_sha256"] == identity["build"]["binary_sha256"]
            and run["identity_evidence_sha256"] == digest(identity), "Run build/identity hashes differ from evidence")
    for key in ("firmware_before", "firmware_after"):
        observation = evidence[key]
        if observation is None and key == "firmware_after" and run["capture"]["status"] == "interrupted":
            continue
        require(isinstance(observation, dict) and observation["result"] == "busy"
                and observation["build_sha256"] == identity["build"]["elf_sha256"]
                and observation["active_revision"] == receipt["reply"]["active_revision"],
                "Missing or changed live firmware/profile evidence")
