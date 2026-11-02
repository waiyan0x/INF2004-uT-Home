"""Reviewed device and board definitions used by host admission."""

from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

from .documents import ROOT, check, load_document, require


class Catalogue:
    """Read reviewed repository descriptors, never descriptor paths from a node."""

    def __init__(self, root: str | Path = ROOT) -> None:
        self.root = Path(root)
        self.node_schema = load_document(self.root / "schemas/profile.schema.json")
        descriptor_schema = load_document(
            self.root / "schemas/device-descriptor.schema.json"
        )
        self.descriptors: dict[tuple[str, str], dict[str, Any]] = {}
        for path in sorted((self.root / "catalogue/devices").glob("*.json")):
            descriptor = load_document(path)
            check(descriptor, descriptor_schema, str(path))
            for name in ("instance_schema", "bus_schema"):
                Draft202012Validator.check_schema(descriptor[name])
                self._local_references_only(descriptor[name])
            key = (descriptor["descriptor_id"], descriptor["descriptor_version"])
            require(key not in self.descriptors, f"Duplicate descriptor: {key}")
            self.descriptors[key] = descriptor
        self.boards: dict[tuple[str, str, str], dict[str, Any]] = {}
        for path in sorted((self.root / "catalogue/boards").glob("*.json")):
            board = load_document(path)
            board_key = (board["platform"], board["variant"], board["capability_version"])
            require(board_key not in self.boards, f"Duplicate board capability: {board_key}")
            self.boards[board_key] = board

    @classmethod
    def _local_references_only(cls, value: object) -> None:
        if isinstance(value, dict):
            for key, item in value.items():
                if key in {"$ref", "$dynamicRef"}:
                    require(
                        isinstance(item, str) and item.startswith("#"),
                        "Descriptors must not fetch external schema references",
                    )
                cls._local_references_only(item)
        elif isinstance(value, list):
            for item in value:
                cls._local_references_only(item)
