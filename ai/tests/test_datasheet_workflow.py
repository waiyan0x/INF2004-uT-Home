"""Datasheet extraction is isolated from catalogue, configuration and serial I/O."""
import base64
from concurrent.futures import ThreadPoolExecutor
import http.client
import io
import json
import os
from unittest import TestCase
from unittest.mock import patch
from threading import Event, Thread
from urllib.error import HTTPError
from pypdf import PdfWriter
from tools.configurator import datasheet
from tools.configurator.web import make_server
from tools.configurator.workflow import Workflow


def pdf_body():
    writer = PdfWriter()
    writer.add_blank_page(width=100, height=100)
    output = io.BytesIO()
    writer.write(output)
    return {"pdf_base64": base64.b64encode(output.getvalue()).decode("ascii")}


def example():
    return {"device_name": "SEN0390", "i2c_address": {"address_7bit": 74, "write_byte": 148,
                                                       "read_byte": 149, "pages": [1]}, "facts": [
        {"name": "I2C address", "value": "0x4A (7-bit)", "status": "inferred", "pages": [1],
         "note": "0x94 and 0x95 include the read/write bit."},
        {"name": "mode register", "value": None, "status": "conflicting", "pages": [1],
         "note": "Review conflicting register references."}],
        "unresolved": ["Confirm EN and read timing."]}


class DatasheetTests(TestCase):
    def setUp(self):
        self.environment = patch.dict(os.environ, {"GEMINI_API_KEY": "test-secret", "UTHOME_GEMINI_MODEL": "test-model"})
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def test_draft_preserves_uncertainty_and_never_promotes(self):
        captured = []
        def request(raw, key, model):
            captured.append((raw, key, model))
            return example()
        result = datasheet.analyse(pdf_body(), request=request)
        self.assertEqual(result["status"], "unreviewed")
        self.assertEqual(result["draft"], example())
        self.assertEqual(result["pages"], 1)
        self.assertEqual(result["i2c_address_review"]["derived_7bit"], 0x4A)
        self.assertFalse(result["i2c_address_review"]["conflicting"])
        self.assertEqual(len(result["source_sha256"]), 64)
        self.assertNotIn("test-secret", json.dumps(result))
        self.assertEqual(captured[0][1:], ("test-secret", "test-model"))
        self.assertTrue(captured[0][0].startswith(b"%PDF-"))
        self.assertEqual(result["provider"], "gemini")
        self.assertNotIn("profile", result)

    def test_address_conflict_remains_visible_and_unreviewed(self):
        draft = example()
        draft["i2c_address"]["address_7bit"] = 0x48
        result = datasheet.analyse(pdf_body(), request=lambda *args: draft)
        self.assertTrue(result["i2c_address_review"]["conflicting"])
        self.assertEqual(result["status"], "unreviewed")

    def test_sen0390_reference_pages_and_conflicting_registers_stay_visible(self):
        writer = PdfWriter()
        for _ in range(16): writer.add_blank_page(width=100, height=100)
        pdf = io.BytesIO()
        writer.write(pdf)
        draft = example()
        draft["i2c_address"]["pages"] = [1, 13]
        draft["facts"][1].update(pages=[5, 8], note="0x04 in the table; 0x02 in later text")
        result = datasheet.analyse({"pdf_base64": base64.b64encode(pdf.getvalue()).decode("ascii")},
                                   request=lambda *args: draft)
        self.assertEqual(result["i2c_address_review"]["derived_7bit"], 0x4A)
        self.assertEqual(result["draft"]["facts"][1]["status"], "conflicting")
        self.assertEqual(result["draft"]["facts"][1]["pages"], [5, 8])

    def test_invalid_evidence_and_invented_fields_are_rejected(self):
        for mutate in (lambda d: d["facts"][0].update(pages=[2]),
                       lambda d: d["facts"][0].update(status="missing"),
                       lambda d: d["i2c_address"].update(pages=[]),
                       lambda d: d.update(driver="automatically_installed"),
                       lambda d: d["facts"].append({"name": "broken"})):
            draft = example(); mutate(draft)
            with self.subTest(draft=draft), self.assertRaises(ValueError):
                datasheet.analyse(pdf_body(), request=lambda *args: draft)

    def test_bad_upload_and_missing_key_do_not_call_model(self):
        with self.assertRaises(ValueError):
            datasheet.analyse({"pdf_base64": "!!!"}, request=lambda *args: self.fail())
        with patch.dict(os.environ, {"GEMINI_API_KEY": ""}), self.assertRaises(ValueError):
            datasheet.analyse(pdf_body(), request=lambda *args: self.fail())
        for malformed in ("unsafe\nheader", "nonascii\u00e9"):
            with patch.dict(os.environ, {"GEMINI_API_KEY": malformed}), self.assertRaises(ValueError) as caught:
                datasheet.analyse(pdf_body(), request=lambda *args: self.fail())
            self.assertNotIn(malformed, str(caught.exception))

    def test_pdf_without_extractable_text_is_sent_and_model_id_is_restricted(self):
        self.assertEqual(datasheet.analyse(pdf_body(), request=lambda *args: example())["pages"], 1)
        with patch.dict(os.environ, {"UTHOME_GEMINI_MODEL": "../other"}), self.assertRaises(ValueError):
            datasheet.analyse(pdf_body(), request=lambda *args: self.fail())

    def test_incomplete_or_refused_response_is_not_a_draft(self):
        class Response:
            def __init__(self, body): self.body = body
            def __enter__(self): return self
            def __exit__(self, *args): return False
            def read(self, limit): return json.dumps(self.body).encode()
        for reply in ({"candidates": [{"finishReason": "MAX_TOKENS"}]},
                      {"promptFeedback": {"blockReason": "SAFETY"}},
                      {"candidates": [{"finishReason": "STOP", "content": {}}]}):
            with self.subTest(reply=reply), patch.object(datasheet, "urlopen", return_value=Response(reply)):
                with self.assertRaises(ValueError): datasheet._request(b"pdf", "key", "model")

    def test_provider_request_carries_pdf_and_returns_structured_facts(self):
        class Response:
            def __enter__(self): return self
            def __exit__(self, *args): return False
            def read(self, limit):
                return json.dumps({"candidates": [{"finishReason": "STOP", "content": {
                    "parts": [{"text": json.dumps(example())}]}}]}).encode()
        def open_request(request, timeout):
            payload = json.loads(request.data)
            self.assertEqual(request.full_url, "https://generativelanguage.googleapis.com/v1beta/models/model:generateContent")
            self.assertEqual(timeout, 60)
            self.assertEqual(request.get_header("User-agent"), "uT-Home-Configuration-Desk/1.0")
            self.assertEqual(request.get_header("X-goog-api-key"), "secret")
            self.assertEqual(payload["generationConfig"]["responseFormat"]["text"]["schema"], datasheet.DRAFT_SCHEMA)
            self.assertEqual(payload["contents"][0]["parts"][0]["inlineData"], {"mimeType": "application/pdf", "data": "cGRm"})
            return Response()
        with patch.object(datasheet, "urlopen", side_effect=open_request):
            self.assertEqual(datasheet._request(b"pdf", "secret", "model"), example())
        error = HTTPError("https://generativelanguage.googleapis.com/", 401, "private detail", {}, None)
        with patch.object(datasheet, "urlopen", side_effect=error):
            with self.assertRaisesRegex(ValueError, "HTTP 401") as caught:
                datasheet._request(b"pdf", "secret", "model")
            self.assertNotIn("secret", str(caught.exception))
            self.assertNotIn("private detail", str(caught.exception))
        denied = HTTPError("https://generativelanguage.googleapis.com/", 403, "blocked", {}, None)
        with patch.object(datasheet, "urlopen", side_effect=denied):
            with self.assertRaisesRegex(ValueError, "network security"):
                datasheet._request(b"pdf", "secret", "model")

    def test_catalogue_match_is_only_a_candidate_and_checks_firmware(self):
        flow = Workflow()
        self.addCleanup(flow.close)
        offline = flow.dispatch("datasheet_match", {"device_name": "BMP388"})
        self.assertFalse(offline["identity_verified"])
        self.assertEqual(offline["matches"][0]["descriptor_id"], "bmp388")
        self.assertIsNone(offline["matches"][0]["driver_available"])
        light = flow.dispatch("datasheet_match", {"device_name": "SEN0390"})
        self.assertEqual(light["matches"][0]["descriptor_id"], "sen0390")
        self.assertIsNone(light["matches"][0]["driver_available"])
        class Client:
            def capabilities(self):
                class Caps: drivers = ("bmp388",)
                return Caps()
        flow.client = Client()
        self.assertTrue(flow.dispatch("datasheet_match", {"device_name": "bmp388"})["matches"][0]["driver_available"])
        self.assertFalse(flow.dispatch("datasheet_match", {"device_name": "sen0390"})["matches"][0]["driver_available"])

    def test_slow_model_request_does_not_hold_serial_workflow_lock(self):
        started, release = Event(), Event()
        class FakeWorkflow:
            client = None
            def dispatch(self, action, body): return {"action": action}
            def close(self): pass
        server = make_server(0, FakeWorkflow())
        Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        def fake_analysis(body):
            started.set()
            if not release.wait(3): raise ValueError("test timed out")
            return {"status": "unreviewed"}
        def post(path, body, authorized=True):
            conn = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=5)
            headers = {"Content-Type": "application/json", "X-Session": server.token if authorized else "wrong"}
            conn.request("POST", path, json.dumps(body), headers)
            response = conn.getresponse()
            result = response.status, json.loads(response.read())
            conn.close()
            return result
        with patch("tools.configurator.web.analyse", side_effect=fake_analysis):
            with ThreadPoolExecutor(max_workers=2) as pool:
                running = pool.submit(post, "/api/datasheet", {"pdf_base64": "x" * 70000})
                self.assertTrue(started.wait(3))
                self.assertEqual(post("/api/query", {})[1]["action"], "query")
                self.assertEqual(post("/api/datasheet", {}, authorized=False)[0], 403)
                self.assertEqual(post("/api/datasheet", {})[0], 400)
                release.set()
                self.assertEqual(running.result()[0], 200)
