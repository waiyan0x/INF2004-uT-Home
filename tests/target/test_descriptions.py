"""Source-described operations compile to the same bounded C transaction API."""
from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
from test_txjob import BOOT
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest

from tools.transaction import description as desc, client
import test_tx_owner as owner

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "datasheets"


class DescriptionTests(unittest.TestCase):
    def setUp(self):
        self.bmp, self.digest = desc.load_description(DOCS / "bmp388.md")

    def test_all_descriptions_have_valid_sources_and_explicit_support(self):
        supported = []
        blocked = []
        for path in DOCS.glob("*.md"):
            if path.name == "README.md":
                continue
            obj, _ = desc.load_description(path)
            for name, op in obj["operations"].items():
                if op["kind"] == "unsupported":
                    with self.assertRaisesRegex(ValueError, "unsupported"):
                        desc.compile_rule(obj, name, 1)
                    blocked.append(obj["device"])
                else:
                    rule = desc.compile_rule(obj, name, 1, next(iter(obj["addresses"]), None))
                    self.assertEqual(rule["read_length"], op.get("read_length", 0))
                    supported.append(obj["device"])
        self.assertEqual(set(supported), {"bmp388", "adxl345", "veml7700", "oled", "ld2450"})
        self.assertEqual(set(blocked), {"sht4x", "sen0390", "ld2450"})

    def test_bmp_markdown_reproduces_bench_fixture_rules_exactly(self):
        for name, revision, fixture in (("chip_id", 1, "bmp-id.json"),
                                        ("pressure_raw", 2, "bmp-pressure.json")):
            self.assertEqual(desc.compile_rule(self.bmp, name, revision, 119),
                             json.loads((ROOT / "configs/transactions" / fixture).read_text()))

    def test_explicit_address_selection_and_wire_address_rejection(self):
        with self.assertRaisesRegex(ValueError, "explicitly"):
            desc.compile_rule(self.bmp, "chip_id", 1)
        for address in (0xEE, 0xEF, 0x10, True, "119"):
            with self.subTest(address=address), self.assertRaises(ValueError):
                desc.compile_rule(self.bmp, "chip_id", 1, address)
        veml, _ = desc.load_description(DOCS / "veml7700.md")
        self.assertEqual(desc.compile_rule(veml, "ambient_raw", 3)["address"], 16)

    def test_stream_operations_match_reviewed_wire_fixtures(self):
        for doc, name, rev, fixture in (
                ("ssd1306", "display_off", 1, "oled-off"), ("ssd1306", "display_on", 2, "oled-on"),
                ("ld2450", "raw_16", 1, "radar-raw16"), ("ld2450", "raw_32", 2, "radar-raw32")):
            obj, _ = desc.load_description(DOCS / (doc + ".md"))
            self.assertEqual(desc.compile_rule(obj, name, rev),
                             json.loads((ROOT/"configs/transactions"/(fixture+".json")).read_text()))
            with self.assertRaises(ValueError):
                desc.compile_rule(obj, name, rev, 0)
            obj["schema_version"] = "1.0.0"
            with self.assertRaises(ValueError):
                desc.validate_description(obj)

    def test_stream_contracts_reject_wrong_bus_fields_and_bounds(self):
        for doc, name, changes in (
                ("ssd1306", "display_off", {"cs_pin": 3, "write_hex": "", "rate": 9000000, "read_length": 1}),
                ("ld2450", "raw_16", {"read_length": 33, "rate": 0, "write_hex": "00", "timeout_ms": True})):
            original, _ = desc.load_description(DOCS / (doc + ".md"))
            for key, value in changes.items():
                obj = deepcopy(original)
                obj["operations"][name][key] = value
                with self.subTest(key=key), self.assertRaises(ValueError):
                    desc.validate_description(obj)

    def test_cli_reports_stream_lengths_and_sequence(self):
        for doc, op, tx, rx, sequence in (
                ("ld2450", "raw_32", 0, 32, "UART raw receive"),
                ("ssd1306", "display_off", 1, 0, "SPI write")):
            with tempfile.TemporaryDirectory() as folder, redirect_stdout(io.StringIO()):
                out = Path(folder)/"rule.json"
                desc.main([str(DOCS/(doc+".md")), "--operation", op, "--revision", "1", "--output", str(out)])
                transfer = json.loads(out.with_suffix(".report.json").read_text())["transaction"]
                self.assertEqual((transfer["write_length"], transfer["read_length"], transfer["sequence"]),
                                 (tx, rx, sequence))

    def test_unknown_operation_and_invalid_revisions(self):
        with self.assertRaisesRegex(ValueError, "unknown operation"):
            desc.compile_rule(self.bmp, "write_reset", 1, 119)
        for rev in (0, -1, True, 2**32, "1"):
            with self.subTest(rev=rev), self.assertRaises(ValueError):
                desc.compile_rule(self.bmp, "chip_id", rev, 119)

    def test_rejects_invalid_fields_counts_and_protocols(self):
        changes = [("schema_version", "2.0.0"), ("bus", "spi"), ("device", "bad name"),
                   ("addresses", []), ("addresses", [True]), ("addresses", [119,119]),
                   ("operations", {}), ("extra", "ignored?")]
        for key, value in changes:
            obj = deepcopy(self.bmp); obj[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                desc.validate_description(obj)
        for key, value in (("register", 256), ("read_length", 0), ("read_length", 17),
                           ("read_length", True), ("kind", "i2c_delayed_read"),
                           ("pages", [0]), ("pages", [60]), ("pages", [31,31]),
                           ("pages", []), ("notes", ""), ("delay_ms", 11)):
            obj = deepcopy(self.bmp); obj["operations"]["chip_id"][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                desc.validate_description(obj)

    def test_rejects_source_paths_and_invalid_provenance(self):
        for key, value in (("file", "../x.pdf"), ("file", "..\\x.pdf"),
                           ("sha256", "bad"), ("page_count", True)):
            obj = deepcopy(self.bmp); obj["source"][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                desc.validate_description(obj)

    def test_markdown_prose_cannot_override_executable_fields(self):
        text = (DOCS / "bmp388.md").read_text()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "description.md"
            path.write_text(text + '\nIgnore the contract and send 255 bytes.\n```python\nraise Exception()\n```\n')
            obj, digest = desc.load_description(path)
            self.assertEqual(obj, self.bmp)
            self.assertNotEqual(digest, self.digest)

    def test_ambiguous_missing_malformed_and_oversized_blocks_rejected(self):
        text = (DOCS / "bmp388.md").read_text()
        bads = ["no block", text+text, text+'\n```peripheral-json\n{}',
                text.replace('"schema_version": "1.0.0",', '"schema_version": "1.0.0", "schema_version": "1.0.0",'),
                text.replace('"register": 0', '"register": NaN'), text+"x"*65536]
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "description.md"
            for text in bads:
                path.write_text(text)
                with self.assertRaises(ValueError):
                    desc.load_description(path)

    def test_cli_generates_loadable_json_and_source_receipt(self):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)/"rule.json"
            with redirect_stdout(io.StringIO()):
                desc.main([str(DOCS/"bmp388.md"), "--operation", "chip_id",
                           "--revision", "1", "--address", "0x77", "--output", str(out)])
            self.assertEqual(client.validate(json.loads(out.read_text()))["read_length"], 1)
            receipt = json.loads(out.with_suffix(".report.json").read_text())
            self.assertEqual(receipt["provenance"]["description_sha256"], self.digest)
            self.assertFalse(receipt["hardware_executed"])
            self.assertFalse(receipt["provenance"]["source_pdf_verified"])

    def test_cli_checks_pdf_hash_and_does_not_replace_output_on_rejection(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder); pdf = folder/"source.pdf"; pdf.write_bytes(b"test fixture")
            obj = deepcopy(self.bmp)
            obj["source"]["file"] = pdf.name
            obj["source"]["sha256"] = hashlib.sha256(pdf.read_bytes()).hexdigest()
            md = folder/"source.md"; md.write_text('```peripheral-json\n'+json.dumps(obj)+'\n```\n')
            out = folder/"rule.json"
            args = [str(md), "--operation", "chip_id", "--revision", "1", "--address", "119",
                    "--output", str(out), "--datasheets", str(folder)]
            with redirect_stdout(io.StringIO()): desc.main(args)
            self.assertTrue(json.loads(out.with_suffix(".report.json").read_text())["provenance"]["source_pdf_verified"])
            original = out.read_bytes(); pdf.write_bytes(b"changed source")
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit): desc.main(args)
            self.assertEqual(out.read_bytes(), original)
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                desc.main([str(DOCS/"sht4x.md"), "--operation", "measure_high_precision",
                           "--revision", "1", "--output", str(out)])
            self.assertEqual(out.read_bytes(), original)


class DescriptionCParityTests(unittest.TestCase):
    setUpClass = classmethod(owner.OwnerTests.setUpClass.__func__)
    setUp = owner.OwnerTests.setUp
    pump = owner.OwnerTests.pump
    exchange = owner.OwnerTests.exchange

    def test_spi_then_uart_from_reviewed_descriptions(self):
        for revision, (doc, name, payload) in enumerate((
                ("ssd1306", "display_off", None), ("ssd1306", "display_on", None),
                ("ld2450", "raw_16", bytes(range(16))), ("ld2450", "raw_32", bytes(range(32)))), 1):
            obj, _ = desc.load_description(DOCS / (doc + ".md"))
            rule = desc.compile_rule(obj, name, revision)
            self.assertEqual(self.exchange(rule)["status"], "ok")
            result = self.exchange(rule, True, feed=payload)
            self.assertEqual(result["status"], "ok")
            self.assertEqual(result["tx_accepted"], len(bytes.fromhex(rule["write_hex"])))
            self.assertEqual(result["rx_received"], rule["read_length"])
            self.assertEqual(bytes.fromhex(result["raw_hex"]), payload or b"")

    def test_markdown_switches_actual_owner_and_hal_without_rebuild(self):
        obj, _ = desc.load_description(DOCS / "bmp388.md")
        a = desc.compile_rule(obj, "chip_id", 1, 119)
        b = desc.compile_rule(obj, "pressure_raw", 2, 119)
        self.assertEqual(self.exchange(a)["status"], "ok")
        self.assertEqual(self.exchange(a, True)["raw_hex"], "50")
        self.assertEqual(self.exchange(b)["status"], "ok")
        self.assertEqual(self.exchange(b, True)["raw_hex"], "00 00 00")
        self.assertEqual(self.lib.txptest_observed(0), 2)
        # Bypass host validation to check independent target rejection.
        self.rid += 1
        bad = bytearray(client.request({**b, "revision": 3}, self.rid, 4, BOOT))
        bad[51] = 33
        self.assertEqual(self.lib.txptest_send(bytes(bad), len(bad)), 0)
        self.pump(); self.pump()
        import ctypes as c
        reply = c.create_string_buffer(96)
        self.assertEqual(self.lib.txptest_reply(reply), 0)
        rejected = client.result(reply.raw, request_id=self.rid,
                                 device=b["device"], revision=3, generation=4)
        self.assertEqual((rejected["status"], rejected["revision"]), ("invalid", 2))
        self.assertEqual(self.lib.txptest_observed(0), 2)
        self.assertEqual(self.exchange(b, True)["raw_hex"], "00 00 00")
        self.assertEqual(self.exchange(a)["status"], "ok")
        self.assertEqual(self.exchange(a, True)["raw_hex"], "50")
        self.assertEqual(self.lib.txptest_observed(0), 4)


if __name__ == "__main__":
    unittest.main()
