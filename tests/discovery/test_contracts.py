"""Offline discovery-policy checks; no probing, serial access or C compilation."""
from copy import deepcopy
from pathlib import Path
import unittest

from jsonschema import Draft202012Validator, ValidationError

from tools.config_validator import Catalogue, build_plan, load_document

ROOT = Path(__file__).resolve().parents[2]


class DiscoveryContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        schema = load_document(ROOT / "schemas/discovery-catalogue.schema.json")
        Draft202012Validator.check_schema(schema)
        cls.validator = Draft202012Validator(schema)
        cls.document = load_document(ROOT / "catalogue/discovery.json")
        cls.catalogue = Catalogue(ROOT)

    def test_reviewed_catalogue_conforms(self):
        self.validator.validate(self.document)

    def test_references_are_unique_and_cover_reviewed_descriptors(self):
        references = [(entry["descriptor"]["id"], entry["descriptor"]["version"])
                      for entry in self.document["entries"]]
        self.assertEqual(len(references), len(set(references)))
        self.assertEqual(set(references), set(self.catalogue.descriptors))

    def test_probe_matches_descriptor_protocol_and_addresses(self):
        protocols = {"i2c_chip_id": "i2c", "uart_reports": "uart"}
        for entry in self.document["entries"]:
            ref = entry["descriptor"]
            descriptor = self.catalogue.descriptors[(ref["id"], ref["version"])]
            rule = entry["identification"]
            with self.subTest(descriptor=ref["id"]):
                if rule["method"] in protocols:
                    self.assertEqual(descriptor["protocol"], protocols[rule["method"]])
                if rule["method"] == "i2c_chip_id":
                    interface = descriptor["instance_schema"]["properties"]["interface"]
                    allowed = interface["properties"]["address"]["enum"]
                    self.assertTrue(set(rule["addresses"]) <= {int(a, 16) for a in allowed})

    def test_sen0390_requires_manual_identity_confirmation(self):
        entry = next(entry for entry in self.document["entries"]
                     if entry["descriptor"]["id"] == "sen0390")
        self.assertEqual(entry["identification"]["method"], "manual")
        self.assertEqual(entry["identification"]["identity_status"], "user_confirmed")
        preset = load_document(ROOT / "catalogue/connections/bench-i2c.json")
        self.assertNotIn(0x4A, [read["address"] for read in preset["reads"]])

    def test_local_evidence_paths_exist(self):
        for entry in self.document["entries"]:
            for source in entry["sources"]:
                with self.subTest(source=source):
                    path = (ROOT / source).resolve()
                    self.assertTrue(path.is_relative_to(ROOT))
                    self.assertTrue(path.is_file())

    def test_unsupported_commands_and_identity_claims_are_rejected(self):
        changes = [
            (0, "method", "scan_all_gpio"),
            (0, "write_commands", [255]),
            (0, "addresses", [0]),
            (0, "addresses", [118, 118]),
            (0, "register", 256),
            (0, "value", -1),
            (1, "identity_status", "verified"),
            (1, "minimum_valid_reports", 1),
            (1, "window_ms", 0),
            (1, "window_ms", 6000),
            (2, "identity_status", "verified"),
            (2, "confirmation", ""),
        ]
        for index, field, value in changes:
            candidate = deepcopy(self.document)
            candidate["entries"][index]["identification"][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValidationError):
                self.validator.validate(candidate)

    def test_versions_and_missing_evidence_are_rejected(self):
        for field in ("schema_version", "catalogue_version"):
            candidate = deepcopy(self.document)
            candidate[field] = "9.0.0"
            with self.subTest(field=field), self.assertRaises(ValidationError):
                self.validator.validate(candidate)
        for field in ("descriptor", "sources", "cautions"):
            candidate = deepcopy(self.document)
            del candidate["entries"][0][field]
            with self.subTest(field=field), self.assertRaises(ValidationError):
                self.validator.validate(candidate)

    def test_existing_node_admission_still_uses_original_contracts(self):
        manifest = load_document(ROOT / "configs/builds/host-reference.json")
        for path in sorted((ROOT / "configs/nodes").glob("*.json")):
            with self.subTest(profile=path.name):
                document = load_document(path)
                original = deepcopy(document)
                plan = build_plan(document, manifest, self.catalogue)
                self.assertTrue(plan["devices"])
                self.assertEqual(document, original)


if __name__ == "__main__":
    unittest.main()
