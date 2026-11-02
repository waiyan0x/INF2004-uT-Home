"""Comparison keys for decoded state; these do not attest physical identity."""
from collections.abc import Mapping

ConfigurationKey = tuple[int, int, int, int, int]


def configuration_key(value: Mapping[str, int]) -> ConfigurationKey:
    """Ignore per-request fields while retaining active/saved/fault state."""
    return (value['active_revision'], value['saved_revision'], value['active_crc'],
            value['saved_crc'], value['flags'] & 0x70)


def capability_key(value: Mapping[str, object]) -> dict[str, object]:
    """Every capability matters except the request's correlation identifier."""
    return {key: item for key, item in value.items() if key != 'request_id'}
