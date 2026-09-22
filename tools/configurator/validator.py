"""Deterministic structural and semantic validation for μT-Home profiles."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from jsonschema import Draft202012Validator


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SCHEMA_PATH = ROOT / "schemas" / "profile.schema.json"
GPIO_KEYS = {
    "sda_gpio",
    "scl_gpio",
    "mosi_gpio",
    "miso_gpio",
    "sclk_gpio",
    "rx_gpio",
    "tx_gpio",
    "cs_gpio",
    "dc_gpio",
    "reset_gpio",
    "trigger_gpio",
    "echo_gpio",
}
SUPPORTED_PROTOCOLS = {"i2c", "spi", "uart", "gpio"}
SUPPORTED_DRIVERS = {"bmp388", "ssd1306_spi", "hlk_ld2450", "hc_sr04"}
MAX_TOTAL_STACK_BYTES = 131_072
MAX_TOTAL_QUEUE_DEPTH = 256
MAX_TOTAL_UART_BUFFER_BYTES = 16_384


@dataclass(frozen=True, order=True)
class ValidationError:
    """One stable, machine-readable profile validation failure."""

    path: str
    code: str
    message: str

    def to_dict(self) -> dict[str, str]:
        return {"code": self.code, "path": self.path, "message": self.message}


@dataclass(frozen=True)
class ValidationResult:
    """Complete deterministic result for one candidate profile."""

    errors: tuple[ValidationError, ...]

    @property
    def valid(self) -> bool:
        return not self.errors

    def to_dict(self) -> dict[str, object]:
        return {
            "valid": self.valid,
            "errors": [error.to_dict() for error in self.errors],
        }

    @classmethod
    def from_errors(cls, errors: Iterable[ValidationError]) -> "ValidationResult":
        return cls(tuple(sorted(set(errors))))


def _json_path(parts: Iterable[object]) -> str:
    path = "$"
    for part in parts:
        path += f"[{part}]" if isinstance(part, int) else f".{part}"
    return path


def _load_schema(schema_path: Path) -> dict[str, object]:
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    return schema


def _schema_errors(profile: object, schema_path: Path) -> list[ValidationError]:
    validator = Draft202012Validator(_load_schema(schema_path))
    errors: list[ValidationError] = []
    for error in validator.iter_errors(profile):
        path = _json_path(error.absolute_path)
        instance = error.instance
        if isinstance(instance, dict) and instance.get("protocol") not in SUPPORTED_PROTOCOLS:
            protocol = instance.get("protocol")
            errors.append(ValidationError(path, "UNSUPPORTED_PROTOCOL", f"Unsupported protocol: {protocol!r}."))
        elif isinstance(instance, dict) and instance.get("driver") not in SUPPORTED_DRIVERS:
            driver = instance.get("driver")
            errors.append(ValidationError(path, "UNSUPPORTED_DRIVER", f"Unsupported driver: {driver!r}."))
        else:
            errors.append(ValidationError(path, "SCHEMA_INVALID", error.message))
    return errors


def _duplicate_errors(items: list[dict[str, Any]], field: str, code: str, base_path: str) -> list[ValidationError]:
    seen: set[object] = set()
    errors: list[ValidationError] = []
    for index, item in enumerate(items):
        value = item.get(field)
        if value in seen:
            errors.append(ValidationError(f"{base_path}[{index}].{field}", code, f"Duplicate {field}: {value!r}."))
        seen.add(value)
    return errors


def _reserve_gpio(
    reservations: dict[int, tuple[str, str]],
    gpio: object,
    owner: str,
    path: str,
) -> ValidationError | None:
    if not isinstance(gpio, int):
        return None
    previous = reservations.get(gpio)
    if previous is None:
        reservations[gpio] = (owner, path)
        return None
    previous_owner, _ = previous
    return ValidationError(path, "PIN_CONFLICT", f"GPIO {gpio} is already reserved by {previous_owner}.")


def _semantic_errors(profile: object) -> list[ValidationError]:
    if not isinstance(profile, dict):
        return []

    interfaces = [item for item in profile.get("interfaces", []) if isinstance(item, dict)]
    queues = [item for item in profile.get("queues", []) if isinstance(item, dict)]
    devices = [item for item in profile.get("devices", []) if isinstance(item, dict)]
    errors: list[ValidationError] = []

    errors.extend(_duplicate_errors(interfaces, "id", "DUPLICATE_INTERFACE_ID", "$.interfaces"))
    errors.extend(_duplicate_errors(queues, "id", "DUPLICATE_QUEUE_ID", "$.queues"))
    errors.extend(_duplicate_errors(devices, "device_id", "DUPLICATE_DEVICE_ID", "$.devices"))
    tasks = [device["task"] for device in devices if isinstance(device.get("task"), dict)]
    errors.extend(_duplicate_errors(tasks, "name", "DUPLICATE_TASK_NAME", "$.devices[*].task"))

    interface_ids = {item.get("id") for item in interfaces}
    queue_ids = {item.get("id") for item in queues}
    reservations: dict[int, tuple[str, str]] = {}

    for index, interface in enumerate(interfaces):
        owner = f"interface {interface.get('id', index)}"
        for key in GPIO_KEYS & interface.keys():
            path = f"$.interfaces[{index}].{key}"
            conflict = _reserve_gpio(reservations, interface[key], owner, path)
            if conflict:
                errors.append(conflict)

    for index, device in enumerate(devices):
        device_id = str(device.get("device_id", index))
        driver = device.get("driver")
        if driver not in SUPPORTED_DRIVERS:
            errors.append(ValidationError(f"$.devices[{index}].driver", "UNSUPPORTED_DRIVER", f"Unsupported driver: {driver!r}."))

        interface = device.get("interface")
        if isinstance(interface, dict):
            reference = interface.get("ref")
            if reference is not None and reference not in interface_ids:
                errors.append(ValidationError(f"$.devices[{index}].interface.ref", "INTERFACE_NOT_FOUND", f"Interface {reference!r} does not exist."))
            for key in GPIO_KEYS & interface.keys():
                path = f"$.devices[{index}].interface.{key}"
                conflict = _reserve_gpio(reservations, interface[key], f"device {device_id}", path)
                if conflict:
                    errors.append(conflict)
            if driver == "hc_sr04" and interface.get("echo_voltage_divider_required") is not True:
                errors.append(ValidationError(f"$.devices[{index}].interface.echo_voltage_divider_required", "VOLTAGE_DIVIDER_REQUIRED", "HC-SR04 Echo must declare the required 5 V-to-3.3 V divider."))

        behaviour = device.get("behaviour")
        if isinstance(behaviour, dict):
            destinations = []
            if "destination" in behaviour:
                destinations.append(("destination", behaviour["destination"]))
            if isinstance(behaviour.get("sources"), list):
                destinations.extend((f"sources[{source_index}]", source) for source_index, source in enumerate(behaviour["sources"]))
            for suffix, queue_id in destinations:
                if queue_id not in queue_ids:
                    errors.append(ValidationError(f"$.devices[{index}].behaviour.{suffix}", "QUEUE_NOT_FOUND", f"Queue {queue_id!r} does not exist."))

    runtime = profile.get("runtime")
    if isinstance(runtime, dict):
        enabled_count = sum(device.get("enabled") is True for device in devices)
        maximum = runtime.get("max_device_tasks")
        if isinstance(maximum, int) and enabled_count > maximum:
            errors.append(ValidationError("$.runtime.max_device_tasks", "TASK_LIMIT_EXCEEDED", f"{enabled_count} enabled devices exceed the configured limit of {maximum}."))

    total_stack = sum(task.get("stack_size_bytes", 0) for task in tasks if isinstance(task.get("stack_size_bytes"), int))
    if total_stack > MAX_TOTAL_STACK_BYTES:
        errors.append(ValidationError("$.devices", "STACK_BUDGET_EXCEEDED", f"Task stacks total {total_stack} bytes; maximum is {MAX_TOTAL_STACK_BYTES}."))

    total_queue_depth = sum(queue.get("depth", 0) for queue in queues if isinstance(queue.get("depth"), int))
    if total_queue_depth > MAX_TOTAL_QUEUE_DEPTH:
        errors.append(ValidationError("$.queues", "QUEUE_BUDGET_EXCEEDED", f"Queue depths total {total_queue_depth}; maximum is {MAX_TOTAL_QUEUE_DEPTH}."))

    total_uart_buffer = sum(interface.get("rx_buffer_bytes", 0) for interface in interfaces if isinstance(interface.get("rx_buffer_bytes"), int))
    if total_uart_buffer > MAX_TOTAL_UART_BUFFER_BYTES:
        errors.append(ValidationError("$.interfaces", "BUFFER_BUDGET_EXCEEDED", f"UART receive buffers total {total_uart_buffer} bytes; maximum is {MAX_TOTAL_UART_BUFFER_BYTES}."))

    return errors


def validate_profile(profile: object, schema_path: Path = DEFAULT_SCHEMA_PATH) -> ValidationResult:
    """Validate a profile against the schema and cross-field safety rules."""
    return ValidationResult.from_errors(_schema_errors(profile, schema_path) + _semantic_errors(profile))
