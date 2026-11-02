"""Bounded JSON loading and shared admission diagnostics; no hardware I/O."""

from __future__ import annotations

import json
import math
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

    def finite_float(token):
        value = float(token)
        if not math.isfinite(value):
            invalid_constant(token)
        return value

    if len(raw) > max_bytes:
        raise ConfigurationError(f"Document exceeds {max_bytes} bytes")
    try:
        value = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=unique,
            parse_constant=invalid_constant,
            parse_float=finite_float,
        )
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ConfigurationError(f"Invalid JSON: {error}") from error
    if not isinstance(value, dict):
        raise ConfigurationError("Document root must be an object")
    return value


def check(value, schema, location):
    errors = sorted(
        Draft202012Validator(schema).iter_errors(value),
        key=lambda error: str(list(error.path)),
    )
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
