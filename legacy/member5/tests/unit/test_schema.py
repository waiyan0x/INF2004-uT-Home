import json
import unittest
from pathlib import Path

from jsonschema import Draft202012Validator


ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = ROOT / "schemas" / "profile.schema.json"
VALID_PROFILE_PATH = ROOT / "configs" / "nodes" / "first-prototype.json"
UNSUPPORTED_PROTOCOL_PATH = ROOT / "configs" / "invalid" / "unsupported-protocol.json"


def load_json(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


class ProfileSchemaTests(unittest.TestCase):
    def setUp(self) -> None:
        self.schema = load_json(SCHEMA_PATH)
        Draft202012Validator.check_schema(self.schema)
        self.validator = Draft202012Validator(self.schema)

    def test_first_prototype_conforms_to_schema(self) -> None:
        errors = list(self.validator.iter_errors(load_json(VALID_PROFILE_PATH)))
        self.assertEqual([], errors)

    def test_unsupported_protocol_is_rejected(self) -> None:
        errors = list(self.validator.iter_errors(load_json(UNSUPPORTED_PROTOCOL_PATH)))
        self.assertTrue(any(error.validator == "enum" for error in errors))

    def test_unknown_root_property_is_rejected(self) -> None:
        profile = load_json(VALID_PROFILE_PATH)
        profile["unexpected"] = True
        errors = list(self.validator.iter_errors(profile))
        self.assertTrue(any(error.validator == "additionalProperties" for error in errors))


if __name__ == "__main__":
    unittest.main()
