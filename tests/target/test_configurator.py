"""Local UI workflow, HTTP isolation, capabilities and uncertain deployment tests."""
from dataclasses import replace
from concurrent.futures import ThreadPoolExecutor
import http.client
import json
import struct
from threading import Thread
import unittest
from unittest.mock import patch
import zlib
from tools.configurator.workflow import Workflow
from tools.configurator.web import make_server, ASSETS
from tools.config_transfer.capabilities import Capabilities, decode_capabilities
from tools.config_transfer.client import TransferError, Client
from tools.config_transfer.protocol import Command, Reply, HEADER, encode_request
from tools.config_validator.planner import ROOT, load_document
from test_usb_client import Stream

class FakeClient:
    def __init__(self, stream):
        self.stream=stream; self.calls=[]; self.fail=False
        self.current=Reply(Command.QUERY, 1, 0, 8, 0, 0, 0, 0, 16)
        self.caps=Capabilities(9, False, 8192, 0x4FB, 8, 4, 0, "esp32-c3-supermini", "2.0.0",
                               ("bmp388","ssd1306_spi","hlk_ld2450","hc_sr04"))
    def capabilities(self):return self.caps
    def query(self):
        self.current=replace(self.current,request_id=self.current.request_id+1)
        return self.current
    def exchange(self,*args):
        self.calls.append(args)
        if self.fail:raise TransferError("reply lost; outcome unknown")
        return replace(self.current,command=args[0],request_id=args[1],revision=args[2],status=1)

class Port:
    closed=False
    def close(self):self.closed=True

def frame(data, rid=27):
    head=HEADER.pack(b"UTH1",1,0x87,0,rid,0,len(data),0)[:20]
    return head+struct.pack("<I",zlib.crc32(data,zlib.crc32(head)))+data

class ConfiguratorTests(unittest.TestCase):
    def setUp(self):
        self.ports=[]
        def opener(name):
            port=Port();self.ports.append(port);return port
        self.flow=Workflow(opener=opener,client_factory=FakeClient)
        self.addCleanup(self.flow.close)
        self.text=json.dumps(load_document(ROOT/"configs/nodes/distance-only.json"))
    def connect(self):return self.flow.dispatch("connect",{"port":"fake"})
    def review(self):return self.flow.dispatch("review",{"profile":self.text})

    def test_datasheet_draft_uses_reviewed_device_and_confirmed_pins(self):
        self.connect()
        request = dict(profile=self.text, descriptor_id="bmp388", descriptor_version="1.0.0",
                       device_id="light_room", address="0x77", sda_gpio=0, scl_gpio=1,
                       wiring_confirmed=True, identity_confirmed=True,
                       generated_operations=["untrusted write"])
        result = self.flow.dispatch("datasheet_prepare_i2c", request)
        device = result["profile"]["devices"][-1]
        self.assertEqual(device["driver"], "bmp388")
        self.assertEqual(device["interface"]["address"], "0x77")
        self.assertNotIn("generated_operations", device)
        self.assertEqual(self.flow.client.calls, [])
        self.assertFalse(result["identity_verified"])
        self.flow.dispatch("review", {"profile": json.dumps(result["profile"])})

    def test_datasheet_draft_refuses_missing_driver_or_unconfirmed_wiring(self):
        self.connect()
        request = dict(profile=self.text, descriptor_id="bmp388", descriptor_version="1.0.0",
                       device_id="room", address="0x77", sda_gpio=0, scl_gpio=1,
                       wiring_confirmed=True, identity_confirmed=True)
        for updates in ({"wiring_confirmed": False}, {"descriptor_id": "sen0390"},
                        {"address": "0x4a"}, {"sda_gpio": 18}, {"enable_gpio": 3}):
            with self.subTest(updates=updates), self.assertRaises(ValueError):
                self.flow.dispatch("datasheet_prepare_i2c", request | updates)
        self.assertEqual(self.flow.client.calls, [])

    def test_datasheet_draft_reports_a_device_already_in_the_profile(self):
        self.connect()
        request = dict(profile=self.text, descriptor_id="bmp388", descriptor_version="1.0.0",
                       device_id="room", address="0x77", sda_gpio=0, scl_gpio=1,
                       wiring_confirmed=True, identity_confirmed=True)
        added = self.flow.dispatch("datasheet_prepare_i2c", request)
        with self.assertRaisesRegex(ValueError, "device name is already in the profile"):
            self.flow.dispatch("datasheet_prepare_i2c", request | {"profile": json.dumps(added["profile"])})
        self.assertEqual(self.flow.client.calls, [])

    def test_datasheet_enable_gpio_requires_reviewed_schema_and_no_pin_conflict(self):
        self.connect()
        descriptor = self.flow.catalogue.descriptors[("bmp388", "1.0.0")]
        interface = descriptor["instance_schema"]["properties"]["interface"]
        interface["properties"]["enable_gpio"] = {"type": "integer", "minimum": 0, "maximum": 21}
        interface["required"].append("enable_gpio")
        request = dict(profile=self.text, descriptor_id="bmp388", descriptor_version="1.0.0",
                       device_id="room", address="0x77", sda_gpio=0, scl_gpio=1,
                       wiring_confirmed=True, identity_confirmed=True)
        for update in ({}, {"enable_gpio": 0}, {"enable_gpio": 4}, {"enable_gpio": 18}):
            with self.subTest(update=update), self.assertRaises(ValueError):
                self.flow.dispatch("datasheet_prepare_i2c", request | update)
        result = self.flow.dispatch("datasheet_prepare_i2c", request | {"enable_gpio": 3})
        self.assertEqual(result["profile"]["devices"][-1]["interface"]["enable_gpio"], 3)
        self.assertEqual(self.flow.client.calls, [])

    def test_datasheet_draft_refuses_changed_firmware_capabilities(self):
        self.connect()
        self.flow.client.caps = replace(self.flow.client.caps, drivers=("hc_sr04",))
        with self.assertRaisesRegex(ValueError, "capabilities changed"):
            self.flow.dispatch("datasheet_prepare_i2c", dict(profile=self.text, descriptor_id="bmp388",
                descriptor_version="1.0.0", device_id="room", address="0x77", sda_gpio=0,
                scl_gpio=1, wiring_confirmed=True, identity_confirmed=True))
        self.assertEqual(self.flow.client.calls, [])

    def test_offline_review_never_opens_serial_and_cannot_deploy(self):
        review=self.review();self.assertFalse(review["connected"]);self.assertEqual(self.ports,[])
        self.assertEqual(review["plan"]["resources"]["active_tasks"],1)
        with self.assertRaises(ValueError):self.flow.dispatch("apply",{"review_id":review["review_id"]})

    def test_review_is_single_use_and_invalidated_by_new_candidate(self):
        self.connect();first=self.review();second=self.review()
        with self.assertRaises(ValueError):self.flow.dispatch("apply",{"review_id":first["review_id"]})
        self.assertEqual(self.flow.client.calls,[])
        # A stale token consumes the pending review conservatively.
        with self.assertRaises(ValueError):self.flow.dispatch("apply",{"review_id":second["review_id"]})
        valid=self.review();self.flow.dispatch("apply",{"review_id":valid["review_id"]})
        self.assertEqual(len(self.flow.client.calls),1)
        with self.assertRaises(ValueError):self.flow.dispatch("apply",{"review_id":valid["review_id"]})

    def test_changed_board_or_firmware_prevents_apply(self):
        self.connect();review=self.review()
        self.flow.client.current=replace(self.flow.client.current,active_revision=42)
        with self.assertRaisesRegex(ValueError,"state changed"):
            self.flow.dispatch("apply",{"review_id":review["review_id"]})
        review=self.review();self.flow.client.caps=replace(self.flow.caps,features=1)
        with self.assertRaisesRegex(ValueError,"capabilities changed"):
            self.flow.dispatch("apply",{"review_id":review["review_id"]})
        self.assertEqual(self.flow.client.calls,[])

    def test_lost_reply_disconnects_without_resending(self):
        self.connect();review=self.review();client=self.flow.client;client.fail=True
        with self.assertRaises(TransferError):self.flow.dispatch("apply",{"review_id":review["review_id"]})
        self.assertEqual(len(client.calls),1);self.assertIsNone(self.flow.client)
        self.assertTrue(self.ports[0].closed);self.assertIsNone(self.flow.pending)

    def test_invalid_json_target_pins_and_disabled_conflicts_are_rejected(self):
        self.connect()
        for text in ('{"a":1,"a":2}', self.text.replace('"queue_depth": 8','"queue_depth": 8.0'),
                     self.text.replace('"trigger_gpio": 4','"trigger_gpio": 2')):
            with self.assertRaises(ValueError):self.flow.dispatch("review",{"profile":text})
        self.assertEqual(self.flow.client.calls,[])

    def test_exclusive_requests_and_failed_connect_release_resources(self):
        self.flow.lock.acquire()
        with self.assertRaisesRegex(ValueError,"in progress"):self.review()
        self.flow.lock.release()
        class WrongBoard(FakeClient):
            def capabilities(self):return replace(self.caps,board="wrong-board")
        self.flow.client_factory=WrongBoard
        with self.assertRaises(ValueError):self.connect()
        self.assertTrue(self.ports[0].closed);self.assertIsNone(self.flow.client)

    def test_capabilities_boundaries_fragmentation_and_corruption(self):
        data=bytearray(128);struct.pack_into("<8I",data,0,1,0,8192,1,0x4FB,8,4,0)
        data[32:51]=b"esp32-c3-supermini\0";data[64:70]=b"2.0.0\0";data[96:103]=b"hc_sr04"
        raw=frame(data);caps=decode_capabilities(raw,request_id=27)
        self.assertEqual(caps.drivers,("hc_sr04",));self.assertFalse(caps.hardware_qualified)
        stream=Stream(b"noise"+raw,read_size=7)
        self.assertEqual(Client(stream,clock=stream.clock).exchange(Command.CAPABILITIES,27),caps)
        self.assertEqual(stream.written,encode_request(Command.CAPABILITIES,27))
        for raw_bad in (raw[:-1],raw+b"x",raw[:-1]+bytes([raw[-1]^1])):
            with self.assertRaises(ValueError):decode_capabilities(raw_bad,request_id=27)
        for index,value in ((0,2),(4,2),(12,9),(28,8)):
            bad=bytearray(data);struct.pack_into("<I",bad,index,value)
            with self.assertRaises(ValueError):decode_capabilities(frame(bad),request_id=27)

    def test_http_host_origin_token_and_assets_without_board_io(self):
        server=make_server(0,self.flow);thread=Thread(target=server.serve_forever,daemon=True);thread.start()
        self.addCleanup(server.server_close);self.addCleanup(server.shutdown)
        def request(method,path,body=None,headers=None):
            connection=http.client.HTTPConnection("127.0.0.1",server.server_port,timeout=3)
            connection.request(method,path,body,headers or {});response=connection.getresponse()
            result=response.status,response.read();connection.close();return result
        status,page=request("GET","/");self.assertEqual(status,200);self.assertIn(server.token.encode(),page)
        self.assertEqual(request("GET","/../../AGENTS.md")[0],404)
        self.assertEqual(request("GET","/",headers={"Host":"attacker.test"})[0],403)
        body=json.dumps({"profile":self.text})
        self.assertEqual(request("POST","/api/review",body,{"Content-Type":"application/json"})[0],403)
        headers={"Content-Type":"application/json","X-Session":server.token}
        self.assertEqual(request("POST","/api/review",body,headers)[0],200)
        headers["Origin"]="https://attacker.test"
        self.assertEqual(request("POST","/api/review",body,headers)[0],403)
        self.assertEqual(self.ports,[])

    def test_rejected_post_drains_bounded_body_without_parsing_or_board_io(self):
        server = make_server(0, self.flow)
        Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close); self.addCleanup(server.shutdown)
        body = b'x' * 65536
        with patch('tools.configurator.web.parse_document', side_effect=AssertionError('Unauthorized parse')):
            for token, origin in [('', None), (server.token, 'https://attacker.test'), ('\u00e9', None)]:
                connection = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=3)
                self.addCleanup(connection.close)
                connection.putrequest('POST', '/api/review')
                connection.putheader('Content-Type', 'application/json')
                connection.putheader('Content-Length', str(len(body)))
                connection.putheader('X-Session', token)
                if origin:
                    connection.putheader('Origin', origin)
                connection.endheaders()
                # Separate writes reproduce a header-before-body rejection race.
                for offset in range(0, len(body), 4096):
                    connection.send(body[offset:offset + 4096])
                response = connection.getresponse()
                self.assertEqual(response.status, 403)
                self.assertEqual(json.loads(response.read()), {'error': 'Local session required'})
                connection.close()
        self.assertEqual(self.ports, [])

    def test_bad_body_lengths_are_rejected_without_waiting_for_payload(self):
        server = make_server(0, self.flow)
        Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close); self.addCleanup(server.shutdown)
        for lengths, transfer in [([], None), (['0'], None), (['65537'], None),
                                  (['+2'], None), (['2', '2'], None), (['2'], 'chunked')]:
            with self.subTest(lengths=lengths, transfer=transfer):
                connection = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=1)
                self.addCleanup(connection.close)
                connection.putrequest('POST', '/api/review')
                connection.putheader('Content-Type', 'application/json')
                connection.putheader('X-Session', server.token)
                for value in lengths:
                    connection.putheader('Content-Length', value)
                if transfer:
                    connection.putheader('Transfer-Encoding', transfer)
                connection.endheaders()
                response = connection.getresponse()
                self.assertEqual(response.status, 400)
                response.read()
                connection.close()
        self.assertEqual(self.ports, [])

    def test_rules_require_connected_support_and_produce_a_reviewable_plan(self):
        self.text=json.dumps(load_document(ROOT/"configs/nodes/distance-controlled-output.json"))
        self.assertEqual(len(self.review()["plan"]["rules"]),1)
        self.connect()
        with self.assertRaisesRegex(ValueError,"schema"):
            self.review()
        self.flow.caps=self.flow.client.caps=replace(self.flow.caps,node_schema="2.1.0",
            drivers=self.flow.caps.drivers+("pwm_output",))
        with self.assertRaisesRegex(ValueError,"rules"):
            self.review()
        self.flow.caps=self.flow.client.caps=replace(self.flow.caps,features=1)
        reviewed=self.review()
        self.assertEqual(reviewed["plan"]["rules"][0]["source_index"],0)
        self.assertEqual(reviewed["plan"]["rules"][0]["target_index"],1)
        self.assertFalse(reviewed["hardware_qualified"])
        self.assertEqual(self.flow.client.calls,[])

    def test_restored_dashboard_assets_keep_http_boundary(self):
        server=make_server(0,self.flow)
        Thread(target=server.serve_forever,daemon=True).start()
        self.addCleanup(server.server_close);self.addCleanup(server.shutdown)
        def get(path):
            connection=http.client.HTTPConnection("127.0.0.1",server.server_port,timeout=3)
            connection.request("GET",path);response=connection.getresponse()
            result=response.status,response.read(),response.getheader("Content-Type")
            connection.close();return result
        status,page,_=get("/")
        self.assertEqual(status,200)
        self.assertIn(b"Configuration Desk",page)
        self.assertNotIn(b' id="themeToggle"', page)
        for control in (b' id="connect"',b' id="review"',b' id="apply"'):
            self.assertIn(control,page)
        for path,kind in (("/dashboard.js","text/javascript"),
                          ("/discovery.js","text/javascript"),
                          ("/styles/main.css","text/css"),
                          ("/styles/tokens.css","text/css"),
                          ("/assets/fonts/roboto-latin.woff2","font/woff2"),
                          ("/assets/fonts/roboto-greek.woff2","font/woff2")):
            status,content,mime=get(path)
            self.assertEqual(status,200,path);self.assertTrue(content)
            self.assertTrue(mime.startswith(kind),mime)
        for path in ("/styles/../../web.py","/assets/fonts/README.md",
                     "/styles/%2e%2e/web.py","/legacy/member5/tools/configurator/web.py"):
            self.assertEqual(get(path)[0],404,path)
        self.assertEqual(self.ports,[])

    def test_cold_browser_can_request_shared_assets_concurrently(self):
        server = make_server(0, self.flow)
        Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close); self.addCleanup(server.shutdown)
        def get(path):
            connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=3)
            try:
                connection.request("GET", path)
                response = connection.getresponse()
                self.assertTrue(response.read())
                return response.status
            finally:
                connection.close()
        with ThreadPoolExecutor(max_workers=16) as pool:
            self.assertTrue(all(status == 200 for status in pool.map(get, ASSETS)))
        self.assertEqual(self.ports, [])
