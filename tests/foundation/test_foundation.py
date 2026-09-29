"""Foundation admission, extensibility and C host runtime integration."""
from copy import deepcopy
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from tools.config_validator import Catalogue, ConfigurationError, build_plan, load_document

ROOT = Path(__file__).resolve().parents[2]


def setUpModule():
    """Reject nonconforming C before the suite can compile project sources."""
    subprocess.run(
        [sys.executable, str(ROOT / "tools/c_standard/check.py"), "--root", str(ROOT)],
        cwd=ROOT, check=True,
    )


def example(name="first-prototype"):
    return load_document(ROOT / f"configs/nodes/{name}.json")


class PlannerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalogue = Catalogue(ROOT)
        cls.manifest = load_document(ROOT / "configs/builds/host-reference.json")

    def plan(self, document):
        return build_plan(document, self.manifest, self.catalogue)

    def test_alternative_deployments_and_independent_instances(self):
        expected = {"first-prototype":4,"two-environment-sensors":2,"distance-only":1}
        for name, count in expected.items():
            with self.subTest(name=name):
                plan = self.plan(example(name))
                self.assertEqual(plan["resources"]["active_tasks"], count)
        plan = self.plan(example("two-environment-sensors"))
        self.assertEqual([d["period_ms"] for d in plan["devices"]], [500,5000])
        self.assertEqual([d["priority"] for d in plan["devices"]], [11,12])
        self.assertEqual(plan["devices"][0]["driver_id"], plan["devices"][1]["driver_id"])

    def test_plan_does_not_alias_or_mutate_input(self):
        document = example()
        original = deepcopy(document)
        plan = self.plan(document)
        self.assertEqual(document, original)
        document["devices"][0]["behaviour"]["interval_ms"] = 555
        self.assertEqual(plan["devices"][0]["configuration"]["behaviour"]["interval_ms"], 2000)

    def test_board_bounds_cannot_be_widened_by_node(self):
        for field, value in (("max_device_tasks",5),("queue_depth",9)):
            candidate=example(); candidate["runtime"][field]=value
            with self.subTest(field=field), self.assertRaises(ConfigurationError): self.plan(candidate)

    def test_invalid_examples_are_rejected(self):
        for path in (ROOT/"configs/invalid").glob("*.json"):
            with self.subTest(path=path.name), self.assertRaises(ConfigurationError): self.plan(load_document(path))

    def test_unknown_descriptor_version_driver_and_legacy_schema(self):
        for change in ("descriptor","version","driver","schema"):
            candidate=example()
            if change=="descriptor": candidate["devices"][0]["descriptor"]["id"]="missing"
            elif change=="version": candidate["devices"][0]["descriptor"]["version"]="9.0.0"
            elif change=="driver": candidate["devices"][0]["driver"]="missing"
            else: candidate["schema_version"]="1.0.0"
            with self.subTest(change=change), self.assertRaises(ConfigurationError): self.plan(candidate)

    def test_catalogue_entry_does_not_mean_target_driver_is_available(self):
        manifest=load_document(ROOT/"configs/builds/target-pending.json")
        with self.assertRaisesRegex(ConfigurationError,"Driver unavailable"):
            build_plan(example(),manifest,self.catalogue)

    def test_duplicate_instance_names_and_addresses(self):
        for field in ("id","task","address"):
            candidate=example("two-environment-sensors")
            first,second=candidate["devices"]
            if field=="id": second["device_id"]=first["device_id"]
            elif field=="task": second["task"]["name"]=first["task"]["name"]
            else: second["interface"]["address"]=first["interface"]["address"]
            with self.subTest(field=field), self.assertRaises(ConfigurationError): self.plan(candidate)

    def test_stack_and_service_priority_reservations(self):
        for field,value in (("priority",4),("priority",20),("stack_size_bytes",1024),("stack_size_bytes",3073)):
            candidate=example();candidate["devices"][0]["task"][field]=value
            with self.subTest(field=field,value=value), self.assertRaises(ConfigurationError):self.plan(candidate)
        candidate=example()
        for device in candidate["devices"]:device["task"]["stack_size_bytes"]=8192
        with self.assertRaisesRegex(ConfigurationError,"stacks exceed"):self.plan(candidate)

    def test_pin_bus_and_boot_requirements(self):
        changes=[("interfaces",0,"sda_gpio",18),("interfaces",2,"rx_gpio",10)]
        for _,index,field,value in changes:
            candidate=example();candidate["interfaces"][index][field]=value
            with self.subTest(field=field), self.assertRaises(ConfigurationError):self.plan(candidate)
        candidate=example();del candidate["devices"][1]["interface"]["cs_pullup_ohms"]
        with self.assertRaisesRegex(ConfigurationError,"boot condition"):self.plan(candidate)
        candidate=example();candidate["interfaces"].append(deepcopy(candidate["interfaces"][0]));candidate["interfaces"][-1]["id"]="duplicate_bus"
        with self.assertRaisesRegex(ConfigurationError,"one shared interface"):self.plan(candidate)

    def test_routing_and_descriptor_limits(self):
        for change in ("route","type","interval","unknown"):
            candidate=example()
            if change=="route": candidate["devices"][0]["behaviour"]["destination"]="missing"
            elif change=="type":candidate["queues"][0]["item_type"]="distance_sample"
            elif change=="interval":candidate["devices"][0]["behaviour"]["interval_ms"]=1
            else:candidate["devices"][0]["behaviour"]["invented_setting"]=True
            with self.subTest(change=change), self.assertRaises(ConfigurationError):self.plan(candidate)

    def test_disabling_releases_task_and_stack_but_preserves_wiring_validation(self):
        candidate=example();candidate["devices"][0]["enabled"]=False
        plan=self.plan(candidate)
        self.assertEqual(plan["resources"]["active_tasks"],3)
        self.assertEqual(plan["resources"]["device_stack_bytes"],10240)
        candidate["devices"][0]["interface"]["address"]="0x01"
        with self.assertRaises(ConfigurationError):self.plan(candidate)

    def test_zero_device_configuration_is_valid(self):
        candidate=example();candidate.update(devices=[],interfaces=[],queues=[])
        self.assertEqual(self.plan(candidate)["resources"]["active_tasks"],0)

    def test_queues_have_one_active_consumer(self):
        candidate = example()
        first = candidate["devices"][1]
        second = deepcopy(first)
        second["device_id"] = "second_display"
        second["task"]["name"] = "second_renderer"
        second["interface"].update(cs_gpio=0, dc_gpio=1, reset_gpio=4)
        candidate["interfaces"] = [candidate["interfaces"][1]]
        candidate["devices"] = [first, second]
        with self.assertRaisesRegex(ConfigurationError, "more than one enabled consumer"):
            self.plan(candidate)
        second["enabled"] = False
        self.assertEqual(self.plan(candidate)["resources"]["active_tasks"], 1)
        second["enabled"] = True
        first["behaviour"]["sources"] = ["telemetry.environment"]
        second["behaviour"]["sources"] = ["distance.front"]
        self.assertEqual(self.plan(candidate)["resources"]["active_tasks"], 2)

    def test_duplicate_consumer_sources_rejected(self):
        candidate = example()
        sources = candidate["devices"][1]["behaviour"]["sources"]
        sources[1] = sources[0]
        with self.assertRaises(ConfigurationError):
            self.plan(candidate)

    def test_shared_producer_queue_is_supported(self):
        candidate = example("two-environment-sensors")
        candidate["devices"][1]["behaviour"]["destination"] = "zone.a"
        candidate["queues"] = candidate["queues"][:1]
        plan = self.plan(candidate)
        self.assertEqual(plan["resources"]["active_tasks"], 2)
        self.assertEqual(len(plan["queues"]), 1)

    def test_new_reviewed_descriptor_needs_no_planner_or_node_schema_edits(self):
        with tempfile.TemporaryDirectory(prefix="uthome-extension-") as folder:
            root=Path(folder)
            for area in ("schemas","catalogue"):
                shutil.copytree(ROOT/area,root/area)
            descriptor=load_document(root/"catalogue/devices/bmp388.json")
            descriptor.update(descriptor_id="test_counter",driver_id="test_counter",description="Test-only simulated device")
            props=descriptor["instance_schema"]["properties"]
            props["driver"]={"const":"test_counter"}
            props["descriptor"]["properties"]["id"]={"const":"test_counter"}
            descriptor["routing"]["output_type"]="counter_sample"
            (root/"catalogue/devices/test_counter.json").write_text(json.dumps(descriptor),encoding="utf-8")
            candidate=example("two-environment-sensors")
            for device in candidate["devices"]:
                device["driver"]="test_counter";device["descriptor"]["id"]="test_counter"
            for queue in candidate["queues"]:queue["item_type"]="counter_sample"
            manifest=deepcopy(self.manifest)
            with self.assertRaisesRegex(ConfigurationError,"Driver unavailable"):
                build_plan(candidate,manifest,Catalogue(root))
            manifest["drivers"].append("test_counter")
            plan=build_plan(candidate,manifest,Catalogue(root))
            self.assertEqual(plan["devices"][0]["driver_id"],"test_counter")
            self.assertEqual(plan["resources"]["active_tasks"],2)

    def test_cli_returns_plan_and_rejects_unavailable_target(self):
        command=[sys.executable,"-B","-m","tools.config_validator",
                 "configs/nodes/two-environment-sensors.json","--build-manifest"]
        result=subprocess.run(command+["configs/builds/host-reference.json"],cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout)["resources"]["active_tasks"],2)
        result=subprocess.run(command+["configs/builds/target-pending.json"],cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(result.returncode,1)
        self.assertIn("Driver unavailable",result.stderr)
        self.assertEqual(result.stdout,"")

    def test_json_duplicate_nonfinite_and_size_rejection(self):
        with tempfile.TemporaryDirectory(prefix="uthome-json-") as folder:
            path=Path(folder)/"bad.json"
            for text in ('{"a":1,"a":2}','{"a":NaN}','[]'):
                path.write_text(text,encoding="utf-8")
                with self.subTest(text=text),self.assertRaises(ConfigurationError):load_document(path)
            path.write_text('{"a":1}',encoding="utf-8")
            with self.assertRaises(ConfigurationError):load_document(path,3)


class Device(ctypes.Structure):
    _fields_=[("id",ctypes.c_char*32),("period_ms",ctypes.c_uint32),("priority",ctypes.c_uint8),
              ("stack_size_bytes",ctypes.c_size_t),("enabled",ctypes.c_bool)]


class HostRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=shutil.which("gcc")
        if not compiler:raise RuntimeError("GCC is required for the foundation acceptance tests")
        cls.temporary=tempfile.TemporaryDirectory(prefix="uthome-native-")
        cls.build=Path(cls.temporary.name)
        flags=[compiler,"-std=c99","-Wall","-Wextra","-Werror","-pedantic","-I",str(ROOT/"firmware/include")]
        library=cls.build/("runtime.dll" if os.name=="nt" else "runtime.so")
        subprocess.run(flags+["-shared", "-fPIC",str(ROOT/"firmware/src/runtime/runtime.c"),str(ROOT/"firmware/src/runtime/hostport.c"),"-o",str(library)],check=True,capture_output=True,text=True)
        cls.lib=ctypes.CDLL(str(library))
        cls.lib.runtime_apply_devices.argtypes=[ctypes.POINTER(Device),ctypes.c_size_t]
        cls.lib.runtime_get_device.argtypes=[ctypes.c_char_p,ctypes.POINTER(Device)]
        cls.lib.runtime_device_count.restype=ctypes.c_size_t
        cls.lib.runtime_start_device.argtypes=[ctypes.c_char_p]
        cls.lib.runtime_update_device.argtypes=[ctypes.c_char_p,ctypes.POINTER(Device)]
        cls.manifest=load_document(ROOT/"configs/builds/host-reference.json")
        cls.catalogue=Catalogue(ROOT)
        cls.binaries=[]
        for name,sources in {
            "runtime":["tests/unit/runtime_main.c","firmware/src/runtime/runtime.c","firmware/src/runtime/hostport.c"],
            "component":["tests/unit/cmp_main.c","firmware/src/runtime/component.c"],
            "registry":["tests/unit/reg_main.c","firmware/src/runtime/component.c","firmware/src/config/drvreg.c"]
        }.items():
            binary=cls.build/(name+".exe")
            subprocess.run(flags+[str(ROOT/s) for s in sources]+["-o",str(binary)],check=True,capture_output=True,text=True)
            cls.binaries.append(binary)

        cls.plan_fixture=cls.build/"runtime_plan_fixture.exe"
        sources=["tests/foundation/plan_main.c", "firmware/src/runtime/runtime.c",
                 "firmware/src/runtime/hostport.c", "firmware/src/runtime/component.c",
                 "firmware/src/config/drvreg.c"]
        subprocess.run(flags+[str(ROOT/s) for s in sources]+["-o",str(cls.plan_fixture)],
                       check=True,capture_output=True,text=True)

    @classmethod
    def tearDownClass(cls):
        if os.name=="nt":
            import _ctypes
            _ctypes.FreeLibrary(cls.lib._handle)
        cls.lib=None
        cls.temporary.cleanup()

    def setUp(self):self.lib.runtime_init()

    def apply(self,document):
        plan=build_plan(document,self.manifest,self.catalogue)
        array=(Device*len(plan["devices"]))()
        for output,device in zip(array,plan["devices"]):
            output.id=device["device_id"].encode("ascii")
            for key in ("period_ms","priority","stack_size_bytes","enabled"):setattr(output,key,device[key])
        return self.lib.runtime_apply_devices(array,len(array))

    def get(self,name):
        result=Device()
        self.assertEqual(self.lib.runtime_get_device(name.encode(),ctypes.byref(result)),0)
        return result

    def test_same_compiled_runtime_accepts_different_deployments(self):
        for name,count in (("first-prototype",4),("two-environment-sensors",2),("distance-only",1)):
            self.assertEqual(self.apply(example(name)),0)
            self.assertEqual(self.lib.runtime_device_count(),count)

    def test_instances_reconfigure_and_disable_independently(self):
        document=example("two-environment-sensors")
        self.assertEqual(self.apply(document),0)
        document["devices"][0]["behaviour"]["interval_ms"]=700
        document["devices"][1]["enabled"]=False
        self.assertEqual(self.apply(document),0)
        self.assertEqual(self.get("room_1").period_ms,700)
        self.assertEqual(self.get("room_2").period_ms,5000)
        self.assertEqual(self.lib.runtime_start_device(b"room_2"),-2)
        document["devices"][1]["enabled"]=True
        self.assertEqual(self.apply(document),0)
        self.assertTrue(self.get("room_2").enabled)

    def test_failed_validation_preserves_active_configuration(self):
        self.assertEqual(self.apply(example("two-environment-sensors")),0)
        original=bytes(self.get("room_1"))
        invalid=example();invalid["devices"][0]["behaviour"]["interval_ms"]=1
        with self.assertRaises(ConfigurationError):self.apply(invalid)
        self.assertEqual(bytes(self.get("room_1")),original)
        self.assertEqual(self.lib.runtime_device_count(),2)

    def test_c_admission_independently_rejects_partial_and_over_capacity_plans(self):
        self.assertEqual(self.apply(example("two-environment-sensors")),0)
        original=bytes(self.get("room_1"))
        for failure in ("duplicate","tasks","stack","invalid"):
            count=5 if failure=="tasks" else 3
            candidate=(Device*count)()
            for i,item in enumerate(candidate):
                item.id=f"candidate_{i}".encode();item.period_ms=200;item.priority=10;item.stack_size_bytes=3072;item.enabled=True
            if failure=="duplicate":candidate[2].id=candidate[0].id
            elif failure=="stack":
                for item in candidate:item.stack_size_bytes=8192
            elif failure=="invalid":candidate[2].priority=0
            with self.subTest(failure=failure):
                self.assertNotEqual(self.lib.runtime_apply_devices(candidate,count),0)
                self.assertEqual(bytes(self.get("room_1")),original)
                self.assertEqual(self.lib.runtime_device_count(),2)

    def test_low_level_update_cannot_bypass_stack_budget(self):
        self.assertEqual(self.apply(example()),0)
        original=self.get("environment_sensor")
        candidate=Device.from_buffer_copy(original)
        candidate.stack_size_bytes=8192
        self.assertNotEqual(self.lib.runtime_update_device(b"environment_sensor",ctypes.byref(candidate)),0)
        self.assertEqual(bytes(self.get("environment_sensor")),bytes(original))

    def test_empty_plan_stops_all_instances(self):
        self.assertEqual(self.apply(example()),0)
        self.assertEqual(self.lib.runtime_apply_devices(None,0),0)
        self.assertEqual(self.lib.runtime_device_count(),0)

    def test_normalized_plan_resolves_registry_and_dispatches_independent_instances(self):
        revised=example("two-environment-sensors")
        revised["devices"][0]["behaviour"]["interval_ms"]=700
        revised["devices"][1]["enabled"]=False
        for document in (example("two-environment-sensors"),example("distance-only"),revised):
            plan=build_plan(document,self.manifest,self.catalogue)
            arguments=[]
            expected={}
            for device in plan["devices"]:
                arguments.extend([device["driver_id"],device["device_id"],str(device["period_ms"]),
                                  str(device["priority"]),str(device["stack_size_bytes"]),str(int(device["enabled"]))])
                expected[device["device_id"]]=10000//device["period_ms"] if device["enabled"] else 0
            with self.subTest(node=document["node_id"],expected=expected):
                result=subprocess.run([str(self.plan_fixture)]+arguments,capture_output=True,text=True,timeout=20)
                self.assertEqual(result.returncode,0,result.stderr)
                actual={line.split()[0]:int(line.split()[1]) for line in result.stdout.splitlines()}
                self.assertEqual(actual,expected)
        # Even a host manifest entry cannot fabricate a missing compiled driver.
        arguments[0]="not_compiled"
        result=subprocess.run([str(self.plan_fixture)]+arguments,capture_output=True,text=True,timeout=20)
        self.assertEqual(result.returncode,3)
        self.assertEqual(result.stdout,"")

    def test_existing_c_runtime_component_and_registry_suites(self):
        for binary in self.binaries:
            with self.subTest(binary=binary.name):
                subprocess.run([str(binary)],check=True,capture_output=True,text=True)


if __name__=="__main__":unittest.main()
