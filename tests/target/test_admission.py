"""Independent target admission, malformed input and catalogue consistency."""
from copy import deepcopy
from pathlib import Path
import json
import random
import shutil
import subprocess
import sys
import tempfile
import unittest
from tools.config_validator.planner import Catalogue, build_plan, ConfigurationError

ROOT = Path(__file__).resolve().parents[2]
PARSER_SOURCES = ["firmware/src/config/" + name + ".c" for name in
                  ("jsonscan", "cfgjson", "cfgplan", "cfgdev", "cfgrule")]

def setUpModule():
    subprocess.run([sys.executable, str(ROOT / "tools/c_standard/check.py"),
                    "--root", str(ROOT)], cwd=ROOT, check=True)

class AdmissionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("gcc")
        if compiler is None:
            raise AssertionError("GCC is required")
        cls.temp = tempfile.TemporaryDirectory(prefix="uthome-admission-tests-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.folder = Path(cls.temp.name)
        cls.binary = cls.folder / "admit.exe"
        command = [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
                   "-DCJSON_NESTING_LIMIT=8", "-I", str(ROOT / "firmware/src/config"),
                   "-I", str(ROOT / "firmware/third_party/cjson"),
                   "-I", str(ROOT / "firmware/src/automation"), "-I", str(ROOT / "firmware/src/services"),
                   "-I", str(ROOT / "firmware/include")]
        objcopy = shutil.which("objcopy")
        if objcopy is None:
            raise AssertionError("GNU objcopy is required for allocator interception")
        aliases = []
        for original, replacement in (("malloc", "allocate"), ("calloc", "zero"),
                                      ("realloc", "resize"), ("free", "release")):
            aliases += ["--redefine-sym", f"{original}=heaptrap_{replacement}"]
        objects = []
        # Intercept the owned parser and vendor objects, not the host CRT's
        # startup/file-I/O allocator. No allocator call in these objects escapes.
        for index, source in enumerate(PARSER_SOURCES + ["firmware/third_party/cjson/cJSON.c"]):
            target = cls.folder / f"parser-{index}.o"
            subprocess.run(command + ["-c", str(ROOT / source), "-o", str(target)], check=True)
            subprocess.run([objcopy, *aliases, str(target)], check=True)
            objects.append(str(target))
        command += objects + [str(ROOT / "tests/target/admit_main.c"),
                              str(ROOT / "tests/target/heaptrap.c"), "-lm", "-o", str(cls.binary)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        cls.base = json.loads((ROOT / "configs/nodes/distance-only.json").read_bytes())
        cls.catalogue = Catalogue(ROOT)
        cls.manifest = json.loads((ROOT / "configs/builds/host-reference.json").read_bytes())

    def test_allocator_interception_control(self):
        result = subprocess.run([str(self.binary), "heap-probe"], capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.strip(), "heap probe intercepted: 1")
        self.assertEqual(result.stderr, "")

    def test_heap_violation_fails_without_aborting(self):
        path = self.folder / "heap-violation.json"
        path.write_text(json.dumps(self.base), encoding="utf-8")
        result = subprocess.run([str(self.binary), "heap-violation", str(path)],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertEqual(result.stderr.strip(), "Unexpected heap use: 1 calls")

    def run_documents(self, documents, catalogue=False, parser=False, light_probe=False):
        paths = []
        for index, document in enumerate(documents):
            raw = document if isinstance(document, bytes) else json.dumps(document, separators=(",",":")).encode()
            path = self.folder / f"input-{index}.json"
            path.write_bytes(raw)
            paths.append(str(path))
        mode = ("parser" if parser else "light-probe" if light_probe else
                "catalogue" if catalogue else "binding")
        result = subprocess.run([str(self.binary), mode, *paths],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        rows = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
        self.assertEqual(len(rows), len(documents))
        return rows

    def test_unadvertised_light_admission_matches_host_limits(self):
        document = json.loads((ROOT / "configs/nodes/c3-protocol-demo.json").read_bytes())
        descriptor = self.catalogue.descriptors[("sen0390", "1.0.0")]
        document["queues"].append(dict(id="readings.light", item_type="light_sample",
                                      depth=8, overflow_policy="drop_oldest"))
        document["devices"].append(dict(
            device_id="light", driver="sen0390", enabled=True,
            descriptor=dict(id="sen0390", version="1.0.0"),
            interface=dict(ref="i2c0", address="0x4a", enable_gpio=5),
            behaviour=dict(descriptor["defaults"]["behaviour"], destination="readings.light"),
            task=dict(name="light_reader", priority=12, stack_size_bytes=3072)))
        manifest = deepcopy(self.manifest)
        manifest["drivers"].append("sen0390")
        invalid = []
        for field, value in (("enable_gpio", 0), ("enable_gpio", 18), ("address", "0x4b")):
            candidate = deepcopy(document)
            candidate["devices"][-1]["interface"][field] = value
            invalid.append(candidate)
        for field, value in (("interval_ms", 999), ("timeout_ms", 29)):
            candidate = deepcopy(document)
            candidate["devices"][-1]["behaviour"][field] = value
            invalid.append(candidate)
        candidate = deepcopy(document)
        del candidate["devices"][-1]["interface"]["enable_gpio"]
        invalid.append(candidate)
        self.assertNotEqual(self.run_documents([document])[0][0], 0)
        results = self.run_documents([document, *invalid], light_probe=True)
        self.assertEqual([row[0] == 0 for row in results], [True] + [False] * len(invalid))
        build_plan(document, manifest, self.catalogue)
        for candidate in invalid:
            with self.assertRaises(ConfigurationError):
                build_plan(candidate, manifest, self.catalogue)

    def test_pool_exhaustion_and_recovery_with_heap_traps(self):
        # Root + array + 510 Boolean nodes use all 512 typed slots.
        at_limit = {"items": [True] * 510}
        over_limit = {"items": [True] * 511}
        requests = {f"k{i}": 1 for i in range(342)}
        valid = {"value": 4294967295, "signed": -2147483648}
        malformed = b'{"items": [true,]}'
        documents = [at_limit, over_limit, valid, requests, valid, malformed, valid] * 20
        rows = self.run_documents(documents, parser=True)
        expected = [True, False, True, False, True, False, True] * 20
        self.assertEqual([row[0] == 0 for row in rows], expected)

    def test_exact_input_limit_and_node_sized_text_with_heap_traps(self):
        boundary = b'{"text":"' + b'x' * 8181 + b'"}'
        self.assertEqual(len(boundary), 8192)
        # Text allocation equal to cJSON size may use a typed node's bytes.
        strings = {"values": ["x" * size for size in (30, 31, 32, 62, 63, 64)]}
        rows = self.run_documents([boundary, boundary + b' ', strings, {}], parser=True)
        self.assertEqual([row[0] == 0 for row in rows], [True, False, True, True])

    def test_example_profiles_and_actual_binding_availability(self):
        two = json.loads((ROOT / "configs/nodes/two-environment-sensors.json").read_bytes())
        demo = json.loads((ROOT / "configs/nodes/c3-protocol-demo.json").read_bytes())
        examples = [self.base, two, demo]
        for document, result in zip(examples, self.run_documents(examples)):
            host = build_plan(document, self.manifest, self.catalogue)
            self.assertEqual(result[:4], (0,len(document["devices"]),host["resources"]["active_tasks"],
                                        host["resources"]["device_stack_bytes"]))
        prototype = json.loads((ROOT / "configs/nodes/first-prototype.json").read_bytes())
        self.assertNotEqual(self.run_documents([prototype])[0][0], 0)
        # Compiled OLED binding now admits independent valid wiring; the
        # original prototype still conflicts with reserved target pins.
        oled = deepcopy(prototype)
        oled["interfaces"] = [oled["interfaces"][1]]
        oled["devices"] = [oled["devices"][1]]
        oled["devices"][0]["interface"]["cs_gpio"] = 0
        self.assertEqual(self.run_documents([oled], catalogue=True)[0][0], 0)
        self.assertEqual(self.run_documents([oled])[0][0], 0)

    def test_independent_semantic_rejections(self):
        bad = []
        paths = [
            (["schema_version"],"1.0.0"), (["node_id"],"bad name"),
            (["board","variant"],"esp32-s3"), (["runtime","max_device_tasks"],5),
            (["runtime","queue_depth"],9), (["runtime","rollback_on_error"],False),
            (["runtime","queue_depth"],True), (["queues",0,"item_type"],"target_report"),
            (["devices",0,"driver"],"unknown"), (["devices",0,"descriptor","version"],"2.0.0"),
            (["devices",0,"task","priority"],4), (["devices",0,"task","stack_size_bytes"],3073),
            (["devices",0,"behaviour","destination"],"missing"),
            (["devices",0,"behaviour","timeout_us"],30001),
            (["devices",0,"interface","echo_voltage_divider_required"],False),
            (["devices",0,"interface","echo_gpio"],4),
            (["devices",0,"interface","echo_gpio"],18),
            (["devices",0,"interface","trigger_gpio"],2),
        ]
        for path, value in paths:
            item = deepcopy(self.base)
            target = item
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = value
            bad.append(item)
        for path in [[],["runtime"],["devices",0],["devices",0,"behaviour"],["devices",0,"interface"]]:
            item = deepcopy(self.base)
            target = item
            for key in path:
                target = target[key]
            target["unexpected"] = 1
            bad.append(item)
        results = self.run_documents(bad + [self.base])
        for document, result in zip(bad, results):
            self.assertNotEqual(result[0],0,document)
        self.assertEqual(results[-1][0],0)

    def test_disabled_wiring_identity_address_and_resource_limits(self):
        two = json.loads((ROOT / "configs/nodes/two-environment-sensors.json").read_bytes())
        bad = []
        for field in ["device_id","task","interface"]:
            doc = deepcopy(two)
            doc["devices"][1][field] = deepcopy(doc["devices"][0][field])
            doc["devices"][1]["enabled"] = False
            bad.append(doc)
        doc = deepcopy(self.base)
        for number, pins in enumerate([(0,1),(6,7)]):
            dev = deepcopy(doc["devices"][0]);dev["device_id"]=f"extra{number}"
            dev["task"]["name"]=f"extra{number}";dev["enabled"]=False
            dev["interface"]["trigger_gpio"],dev["interface"]["echo_gpio"]=pins
            doc["devices"].append(dev)
        bad.append(doc) # Host catalogue allows four; C3 has two capture pairs.
        doc = deepcopy(two)
        doc["runtime"]["max_device_tasks"]=1
        bad.append(doc)
        for row in self.run_documents(bad):
            self.assertNotEqual(row[0],0)

    def test_decoded_duplicate_keys_lexical_bounds_and_parser_reuse(self):
        valid = json.dumps(self.base,separators=(",",":")).encode()
        cases = [
            b"",b"{}",b"null",b"[]",valid+b"x",valid+b"\0",valid[:-1],
            valid.replace(b'"node_id":',b'"node_id":"duplicate","node_id":',1),
            valid.replace(b'"node_id":',b'"node_id":"duplicate","\\u006eode_id":',1),
            valid.replace(b'"queue_depth":8',b'"queue_depth":08'),
            valid.replace(b'"queue_depth":8',b'"queue_depth":8.0'),
            valid.replace(b'"queue_depth":8',b'"queue_depth":8e0'),
            valid.replace(b'"queue_depth":8',b'"queue_depth":-0'),
            valid.replace(b'"queue_depth":8',b'"queue_depth":NaN'),
            valid.replace(b'"queue_depth":8',b'"queue_depth":4294967296'),
            valid.replace(b'"node_id":"',b'"node_id":"\\u0000',1),
            valid.replace(b'"node_id":"',b'"node_id":"\xff',1),
            b'{"a":'+b'['*9+b'0'+b']'*9+b'}',
            b'{"a":['+b'0,'*3000+b'0]}',
            b" " * 8193,
        ]
        documents=[]
        for case in cases:
            self.assertNotEqual(case,valid)
            documents += [case,valid]
        results=self.run_documents(documents)
        for index,row in enumerate(results):
            self.assertEqual(row[0]==0,index%2==1,(index,documents[index]))
        escaped=valid.replace(b'"node_id"',b'"\\u006eode_id"')
        self.assertEqual(self.run_documents([escaped])[0][0],0)

    def test_bounded_random_input_never_poison_next_parse(self):
        rng=random.Random(204)
        noise=[rng.randbytes(rng.randrange(1,1000)) for _ in range(50)]
        results=self.run_documents(noise+[self.base])
        self.assertTrue(all(row[0]!=0 for row in results[:-1]))
        self.assertEqual(results[-1][0],0)

    def test_restricted_target_limits_are_explicit(self):
        board=json.loads((ROOT/"catalogue/boards/esp32-c3-supermini.json").read_bytes())
        self.assertEqual(board["resources"]["max_device_tasks"],4)
        self.assertEqual(board["resources"]["device_stack_budget_bytes"],16384)
        self.assertEqual(board["resources"]["max_queues"],4)
        self.assertEqual(set(self.catalogue.descriptors),
                         {(name,"1.0.0") for name in ("bmp388","ssd1306_spi","hlk_ld2450","hc_sr04","pwm_output","sen0390")})

    def test_rule_profile_and_alternate_signed_environment_metric(self):
        rule = json.loads((ROOT / "configs/nodes/distance-controlled-output.json").read_bytes())
        alternate = json.loads((ROOT / "configs/nodes/two-environment-sensors.json").read_bytes())
        alternate["schema_version"] = "2.1.0"
        alternate["devices"] = [alternate["devices"][0], deepcopy(rule["devices"][1])]
        alternate["devices"][1]["interface"]["output_gpio"] = 6
        alternate["rules"] = [dict(rule["rules"][0], source="room_1",metric="temperature_centi_c",threshold=-1000)]
        disabled = deepcopy(rule)
        disabled["devices"][1]["enabled"] = False
        disabled.pop("rules")
        for document,row in zip([rule,alternate,disabled],self.run_documents([rule,alternate,disabled])):
            plan = build_plan(document,self.manifest,self.catalogue)
            self.assertEqual(row[0],0,document)
            self.assertEqual(row[2],plan["resources"]["active_tasks"])
        self.assertEqual(build_plan(alternate,self.manifest,self.catalogue)["rules"][0]["threshold"],-1000)

    def test_rule_semantic_rejections_and_parser_reuse(self):
        base = json.loads((ROOT / "configs/nodes/distance-controlled-output.json").read_bytes())
        bad = []
        for key,value in [("source","missing"),("source","output.main"),("target","front_range"),
                          ("metric","pressure_pa"),("comparison","equal"),("hysteresis",-1),
                          ("threshold",1000000001),("threshold",-1000000001),
                          ("max_age_ms",0),("cooldown_ms",60001),("duty_permille",1001)]:
            doc = deepcopy(base);doc["rules"][0][key] = value;bad.append(doc)
        for field,value in [("frequency_hz",99),("frequency_hz",20001),("interval_ms",19)]:
            doc=deepcopy(base);doc["devices"][1]["behaviour"][field]=value;bad.append(doc)
        doc=deepcopy(base);doc["devices"][0]["enabled"]=False;bad.append(doc)
        doc=deepcopy(base);doc["devices"][1]["interface"]["output_gpio"]=4;bad.append(doc)
        doc=deepcopy(base);doc["schema_version"]="2.0.0";bad.append(doc)
        doc=deepcopy(base);doc["rules"]*=2;bad.append(doc)
        doc=deepcopy(base);doc.pop("rules");bad.append(doc)
        doc=deepcopy(base);doc["rules"][0]["unexpected"]=1;bad.append(doc)
        for doc in bad:
            with self.assertRaises(ConfigurationError):
                build_plan(doc,self.manifest,self.catalogue)
        rows=self.run_documents(bad+[base])
        self.assertTrue(all(row[0]!=0 for row in rows[:-1]))
        self.assertEqual(rows[-1][0],0)
        raw=json.dumps(base,separators=(",",":")).encode()
        key=b'"threshold":400'
        self.assertIn(key,raw)
        malformed=[raw.replace(key,b'"threshold":'+value) for value in
                   (b'-0',b'-01',b'-2147483649',b'-1.0',b'-1e2')]
        self.assertTrue(all(row[0]!=0 for row in self.run_documents(malformed)))
