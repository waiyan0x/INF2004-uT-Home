"""Cross-field limits for the bounded generic I2C register-read contract."""
from typing import Any

from .documents import require


def validate_recipe_selection(behaviour: dict[str, Any]) -> None:
    """Validate relations after individual fields have passed their schema."""
    require(behaviour["select_offset"] + behaviour["select_length"] <= behaviour["read_length"],
            "Selected bytes must fit inside the register-read response")
    require(behaviour["read_pause_ms"] < behaviour["timeout_ms"],
            "Read-address pause must be shorter than the operation timeout")
