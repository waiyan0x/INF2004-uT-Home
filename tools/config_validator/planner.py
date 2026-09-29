"""Catalogue-backed node admission and normalized planning; no hardware I/O."""
from __future__ import annotations

from collections import Counter
from copy import deepcopy
import json
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[2]


class ConfigurationError(ValueError):
    """A candidate must not be activated."""


def load_document(path: str | Path, max_bytes: int = 65536) -> dict[str, Any]:
    with Path(path).open("rb") as source:
        return parse_document(source.read(max_bytes + 1), max_bytes)


def parse_document(raw: bytes, max_bytes: int = 65536) -> dict[str, Any]:
    """Apply the same strict JSON handling to files and browser candidates."""
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ConfigurationError(f"Duplicate JSON key: {key}")
            result[key] = value
        return result

    def invalid_constant(value):
        raise ConfigurationError(f"Non-finite JSON number: {value}")

    if len(raw) > max_bytes:
        raise ConfigurationError(f"Document exceeds {max_bytes} bytes")
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=unique,
                           parse_constant=invalid_constant)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ConfigurationError(f"Invalid JSON: {error}") from error
    if not isinstance(value, dict):
        raise ConfigurationError("Document root must be an object")
    return value


def check(value, schema, location):
    errors = sorted(Draft202012Validator(schema).iter_errors(value),
                    key=lambda error: str(list(error.path)))
    if errors:
        error = errors[0]
        path = ".".join(map(str, error.path))
        raise ConfigurationError(f"{location}.{path}: {error.message}")


def require(condition, message):
    if not condition:
        raise ConfigurationError(message)


def unique_by(items, field, label):
    result = {}
    for item in items:
        key = item[field]
        require(key not in result, f"Duplicate {label}: {key}")
        result[key] = item
    return result


class Catalogue:
    """Read reviewed repository descriptors, never descriptor paths from a node."""

    def __init__(self, root: str | Path = ROOT):
        self.root = Path(root)
        self.node_schema = load_document(self.root / "schemas/profile.schema.json")
        descriptor_schema = load_document(self.root / "schemas/device-descriptor.schema.json")
        self.descriptors = {}
        for path in sorted((self.root / "catalogue/devices").glob("*.json")):
            descriptor = load_document(path)
            check(descriptor, descriptor_schema, str(path))
            for name in ("instance_schema", "bus_schema"):
                Draft202012Validator.check_schema(descriptor[name])
                self._local_references_only(descriptor[name])
            key = (descriptor["descriptor_id"], descriptor["descriptor_version"])
            require(key not in self.descriptors, f"Duplicate descriptor: {key}")
            self.descriptors[key] = descriptor
        self.boards = {}
        for path in sorted((self.root / "catalogue/boards").glob("*.json")):
            board = load_document(path)
            key = (board["platform"], board["variant"], board["capability_version"])
            require(key not in self.boards, f"Duplicate board capability: {key}")
            self.boards[key] = board

    @classmethod
    def _local_references_only(cls, value):
        if isinstance(value, dict):
            for key, item in value.items():
                if key in {"$ref", "$dynamicRef"}:
                    require(isinstance(item, str) and item.startswith("#"),
                            "Descriptors must not fetch external schema references")
                cls._local_references_only(item)
        elif isinstance(value, list):
            for item in value:
                cls._local_references_only(item)


def build_plan(document: dict[str, Any], manifest: dict[str, Any],
               catalogue: Catalogue | None = None) -> dict[str, Any]:
    """Validate a complete candidate without mutating it or any active state.

    The manifest represents the selected build's supported driver IDs. A target
    must independently enforce its compiled registry; this is not provisioning.
    """
    catalogue = catalogue or Catalogue()
    document = deepcopy(document)
    check(document, catalogue.node_schema, "node")
    require(manifest.get("manifest_version") == "1.0.0", "Unsupported build manifest")
    require(manifest.get("backend") in {"host-model", "target"}, "Unknown build backend")
    require(isinstance(manifest.get("drivers"), list)
            and all(isinstance(item, str) for item in manifest["drivers"]),
            "Build manifest requires driver IDs")
    board_key = tuple(document["board"][key] for key in
                      ("platform", "variant", "capability_version"))
    require(board_key in catalogue.boards, f"Unsupported board capabilities: {board_key}")
    board = catalogue.boards[board_key]
    require(manifest.get("board") == board["variant"]
            and manifest.get("board_capability_version") == board["capability_version"],
            "Build and board capability versions do not match")
    limits = board["resources"]
    target_limits = board.get("target_limits", {}) if manifest["backend"] == "target" else {}
    runtime = document["runtime"]
    require(runtime["max_device_tasks"] <= limits["max_device_tasks"], "Task limit exceeds board capacity")
    require(runtime["queue_depth"] <= limits["max_queue_depth"], "Queue depth exceeds board capacity")
    require(len(document["devices"]) <= limits["max_devices"], "Too many configured instances")
    require(len(document["queues"]) <= limits["max_queues"], "Too many queues")
    interfaces = unique_by(document["interfaces"], "id", "interface ID")
    queues = unique_by(document["queues"], "id", "queue ID")
    unique_by(document["devices"], "device_id", "device ID")
    unique_by([dev["task"] for dev in document["devices"]], "name", "task name")
    for queue in queues.values():
        require(queue["depth"] <= runtime["queue_depth"], "Queue depth exceeds node policy")

    pins = {}
    ports = set()
    def claim_pins(fields, owner):
        for field, pin in fields.items():
            if not field.endswith("_gpio"):
                continue
            require(type(pin) is int and pin in board["allowed_gpios"], f"Unavailable GPIO {pin}: {owner}")
            require(pin in target_limits.get("protocol_gpios", {}).get(fields.get("protocol"),
                        target_limits.get("allowed_gpios", board["allowed_gpios"])),
                    f"GPIO {pin} is unavailable in the target backend")
            rule = board["conditional_pins"].get(str(pin))
            if rule:
                require(field in rule["allowed_fields"] and all(
                    fields.get(key) == value for key, value in rule["required_fields"].items()),
                    f"GPIO {pin} boot condition is not satisfied: {owner}")
            require(pin not in pins, f"GPIO {pin} conflict: {owner} and {pins.get(pin)}")
            pins[pin] = owner

    for iface in interfaces.values():
        protocol = iface["protocol"]
        port = iface["host" if protocol == "spi" else "port"]
        require(port in board["ports"][protocol], f"Unavailable {protocol} controller {port}")
        require((protocol, port) not in ports, "A bus controller must have one shared interface declaration")
        ports.add((protocol, port))
        check(iface, {"type": "object", "properties": board["interface_limits"][protocol]}, "board.interface")
        for field, minimum in target_limits.get("interface_minimums", {}).get(protocol, {}).items():
            require(iface[field] >= minimum, f"Target {protocol}.{field} must be at least {minimum}")
        fixed = board["fixed_pins"].get(protocol, {}).get(str(port), {})
        require(all(iface.get(key) == value for key, value in fixed.items()), "Selected port has fixed pin bindings")
        claim_pins(iface, iface["id"])

    addresses = set()
    uart_owners = set()
    queue_consumers = {}
    instance_counts = Counter()
    driver_limits = {}
    tasks = []
    stack_bytes = 0
    active_count = 0
    for device in document["devices"]:
        reference = device["descriptor"]
        key = (reference["id"], reference["version"])
        require(key in catalogue.descriptors, f"Unknown descriptor/version: {key}")
        descriptor = catalogue.descriptors[key]
        driver = descriptor["driver_id"]
        require(device["driver"] == driver, "Driver does not match descriptor")
        require(driver in manifest["drivers"], f"Driver unavailable in selected build: {driver}")
        check(device, descriptor["instance_schema"], device["device_id"])
        instance_counts[driver] += 1
        driver_limits[driver] = min(driver_limits.get(driver, 8), descriptor["resources"]["max_instances"],
                                    target_limits.get("driver_instances", {}).get(driver, 8))
        connection = device["interface"]
        protocol = descriptor["protocol"]
        if protocol != "gpio":
            require(connection.get("ref") in interfaces, "Unknown interface reference")
            bus = interfaces[connection["ref"]]
            require(bus["protocol"] == protocol, "Descriptor/bus protocol mismatch")
            check(bus, descriptor["bus_schema"], device["device_id"] + ".bus")
            if protocol == "i2c":
                try:
                    address = int(connection["address"], 16)
                except (KeyError, ValueError, TypeError) as error:
                    raise ConfigurationError("I2C device needs a hexadecimal address") from error
                require(0x08 <= address <= 0x77, "Reserved I2C address")
                bus_address = (bus["id"], address)
                require(bus_address not in addresses, "Duplicate I2C bus address")
                addresses.add(bus_address)
            elif protocol == "uart":
                require(bus["id"] not in uart_owners, "UART requires one owning instance")
                uart_owners.add(bus["id"])
        else:
            require(connection.get("protocol") == "gpio", "GPIO descriptor requires GPIO binding")
        claim_pins(connection, device["device_id"])
        behaviour = device["behaviour"]
        routing = descriptor["routing"]
        if routing["output_type"] is not None:
            destination = behaviour.get("destination")
            require(destination in queues, "Unknown destination queue")
            require(queues[destination]["item_type"] == routing["output_type"], "Queue sample type mismatch")
        sources = behaviour.get("sources", [])
        require(len(sources) == len(set(sources)), "Duplicate source queue")
        for source in sources:
            require(source in queues, "Unknown source queue")
            require(queues[source]["item_type"] in routing["input_types"], "Unsupported consumer sample type")
            if device["enabled"]:
                require(source not in queue_consumers,
                        f"Queue {source} has more than one enabled consumer")
                queue_consumers[source] = device["device_id"]
        task = device["task"]
        require(limits["min_device_priority"] <= task["priority"] <= limits["max_device_priority"],
                "Priority is reserved for a platform service")
        require(task["stack_size_bytes"] >= descriptor["resources"]["min_stack_bytes"]
                and task["stack_size_bytes"] % 256 == 0, "Invalid driver stack allocation")
        execution = descriptor["execution"]
        period = 0 if execution["period_field"] is None else behaviour[execution["period_field"]]
        timeout = behaviour[execution["timeout_field"]] * (1000 if execution["timeout_unit"] == "ms" else 1)
        require((execution["kind"] == "stream" and period == 0) or
                (execution["kind"] != "stream" and type(period) is int and period > 0), "Invalid execution period")
        require(type(timeout) is int and 0 < timeout <= 0xFFFFFFFF, "Invalid normalized timeout")
        if device["enabled"]:
            active_count += 1
            stack_bytes += task["stack_size_bytes"]
        tasks.append({"device_id":device["device_id"],"driver_id":driver,"descriptor":deepcopy(reference),
                      "enabled":device["enabled"],"execution_kind":execution["kind"],"period_ms":int(period),
                      "timeout_us":int(timeout),"priority":int(task["priority"]),"stack_size_bytes":int(task["stack_size_bytes"]),
                      "configuration":deepcopy(device)})
    require(all(instance_counts[key] <= value for key, value in driver_limits.items()), "Driver instance capacity exceeded")
    require(active_count <= runtime["max_device_tasks"], "Enabled instances exceed task capacity")
    require(stack_bytes <= limits["device_stack_budget_bytes"], "Device stacks exceed board budget")
    rules = rule_plan(document, catalogue)
    return {"plan_version":"1.0.0","node_schema_version":document["schema_version"],"node_id":document["node_id"],
            "rules":rules,
            "board":deepcopy(document["board"]),"build_id":manifest.get("build_id"),"backend":manifest["backend"],
            "devices":tasks,"interfaces":deepcopy(document["interfaces"]),"queues":deepcopy(document["queues"]),
            "resources":{"active_tasks":active_count,"device_stack_bytes":stack_bytes,
                         "queue_payload_bytes":sum(q["depth"] * limits["max_queue_item_bytes"] for q in queues.values())}}


def rule_plan(document, catalogue):
    """Resolve typed source identity and a single command owner per output."""
    devices = {d["device_id"]:(i,d) for i,d in enumerate(document["devices"])}
    metric_types = {"distance_mm":"distance_sample", "temperature_centi_c":"environment_sample",
                    "pressure_pa":"environment_sample", "target_count":"target_report"}
    outputs = {name for name, (_,dev) in devices.items() if dev["behaviour"].get("profile") == "rule_output"}
    rules = document.get("rules", [])
    require(not outputs or document["schema_version"] == "2.1.0", "Rule outputs require schema 2.1.0")
    used = set()
    result = []
    for rule in rules:
        require(rule["source"] in devices and rule["target"] in outputs, "Unknown rule source or output")
        require(rule["target"] not in used, "An output can have only one rule")
        used.add(rule["target"])
        source_index, source = devices[rule["source"]]
        target_index, target = devices[rule["target"]]
        descriptor = catalogue.descriptors[(source["descriptor"]["id"], source["descriptor"]["version"])]
        require(descriptor["routing"]["output_type"] == metric_types[rule["metric"]], "Rule metric/source type mismatch")
        require(not target["enabled"] or source["enabled"], "An enabled output needs an enabled source")
        result.append(dict(rule, source_index=source_index, target_index=target_index))
    require(all(name in used or not devices[name][1]["enabled"] for name in outputs),
            "Every enabled output requires one rule")
    return result
