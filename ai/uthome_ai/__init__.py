"""Contract validation and feature encoding for μT-Home AI."""

from .contracts import (
    ContractValidationError,
    encode_feature_vector,
    load_json_document,
    validate_exchange,
    validate_recommendation,
    validate_request,
)

__all__ = [
    "ContractValidationError",
    "encode_feature_vector",
    "load_json_document",
    "validate_exchange",
    "validate_recommendation",
    "validate_request",
]
