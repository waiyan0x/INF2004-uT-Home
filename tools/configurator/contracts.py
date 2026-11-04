"""JSON-compatible results of the configurator's named capture operations."""
from typing import Any, TypedDict


class ReplyData(TypedDict):
    command: int
    request_id: int
    revision: int
    status: int
    active_revision: int
    saved_revision: int
    active_crc: int
    saved_crc: int
    flags: int
    message: str


class ConnectionResult(TypedDict):
    connected: bool
    capabilities: dict[str, Any]
    state: ReplyData


class ReviewResult(TypedDict):
    review_id: str
    sha256: str
    bytes: int
    profile: dict[str, Any]
    plan: dict[str, Any]
    connected: bool
    hardware_qualified: bool
    state: ReplyData | None
