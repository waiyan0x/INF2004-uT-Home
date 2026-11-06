"""Bounded recipe admission and additive draft preparation without bus access."""
from copy import deepcopy
from dataclasses import replace
import json
import unittest

from tools.configurator.recipes import prepare_recipe, recipe_catalogue
from tools.configurator.workflow import Workflow
from tools.config_validator.planner import Catalogue, ROOT, build_plan, load_document
from test_configurator import FakeClient, Port


def recipe(**changes):
    return dict(device_id="bmp_id", bus_ref="i2c0", address="0x77", register=0,
                read_length=1, read_pause_ms=0, select_offset=0, select_length=1,
                interval_ms=1000, timeout_ms=100) | changes


def empty_i2c_profile():
    document = load_document(ROOT / "configs/nodes/c3-protocol-demo.json")
    document["devices"] = []
    document["queues"] = []
    document["interfaces"] = document["interfaces"][:1]
    return document


class RecipeTests(unittest.TestCase):
    def setUp(self):
        self.catalogue = Catalogue()
        self.manifest = load_document(ROOT / "configs/builds/host-reference.json")
        self.descriptor = self.catalogue.descriptors[("i2c_recipe", "1.0.0")]

    def plan(self, document):
        return build_plan(document, self.manifest, self.catalogue)

    def test_builder_copies_input_and_reuses_existing_bus_without_changing_wiring(self):
        document = empty_i2c_profile()
        original = deepcopy(document)
        result = prepare_recipe(document, recipe(), self.descriptor)
        plan = self.plan(result)
        self.assertEqual(result["interfaces"], original["interfaces"])
        self.assertEqual(plan["resources"]["active_tasks"], 1)
        self.assertEqual(plan["resources"]["device_stack_bytes"], 3072)
        self.assertEqual(result["queues"][0]["item_type"], "raw_sample")
        self.assertEqual(result["devices"][0]["behaviour"]["operation"], "read_register")
        result["devices"][0]["behaviour"]["register"] = 3
        self.assertEqual(document, original)

    def test_builder_rejects_missing_bus_extra_fields_and_existing_address(self):
        document = empty_i2c_profile()
        for invalid in (recipe(bus_ref="absent"), recipe(register=True),
                        recipe(command_bytes=[0]), recipe(address="0x7F")):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                prepare_recipe(document, invalid, self.descriptor)
        populated = load_document(ROOT / "configs/nodes/c3-protocol-demo.json")
        original = deepcopy(populated)
        for enabled in (True, False):
            populated["devices"][0]["enabled"] = enabled
            with self.subTest(enabled=enabled), self.assertRaisesRegex(ValueError, "Remove.*environment_sensor"):
                prepare_recipe(populated, recipe(), self.descriptor)
        populated["devices"][0]["enabled"] = original["devices"][0]["enabled"]
        self.assertEqual(populated, original)

    def test_planner_rejects_ranges_relations_and_unrecognized_behaviour(self):
        document = prepare_recipe(empty_i2c_profile(), recipe(), self.descriptor)
        changes = [dict(register=-1), dict(register=256), dict(register=True),
                   dict(read_length=0), dict(read_length=17), dict(read_pause_ms=11),
                   dict(select_offset=1), dict(select_length=2),
                   dict(read_pause_ms=10, timeout_ms=10), dict(interval_ms=199),
                   dict(timeout_ms=101), dict(extra=0), dict(operation="write")]
        for change in changes:
            candidate = deepcopy(document)
            candidate["devices"][0]["behaviour"].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.plan(candidate)
        for key in document["devices"][0]["behaviour"]:
            candidate = deepcopy(document)
            del candidate["devices"][0]["behaviour"][key]
            with self.subTest(missing=key), self.assertRaises(ValueError):
                self.plan(candidate)

    def test_boundary_selection_and_two_generic_instances_share_one_bus(self):
        document = prepare_recipe(empty_i2c_profile(),
            recipe(device_id="first", address="0x76", register=255, read_length=16,
                   select_offset=15, select_length=1, read_pause_ms=10,
                   interval_ms=60000, timeout_ms=100), self.descriptor)
        document = prepare_recipe(document, recipe(device_id="second"), self.descriptor)
        self.assertEqual(self.plan(document)["resources"]["active_tasks"], 2)
        over_limit = prepare_recipe(document, recipe(device_id="third", address="0x48"), self.descriptor)
        over_limit["devices"][-1]["enabled"] = False
        with self.assertRaisesRegex(ValueError, "instance capacity"):
            self.plan(over_limit)

    def test_all_i2c_drivers_reserve_addresses_even_when_disabled(self):
        document = prepare_recipe(empty_i2c_profile(), recipe(address="0x76"), self.descriptor)
        bmp = load_document(ROOT / "configs/nodes/c3-protocol-demo.json")["devices"][0]
        bmp["interface"]["address"] = "0x76"
        document["devices"].append(bmp)
        document["queues"].append(dict(id="telemetry.environment", item_type="environment_sample",
                                        depth=8, overflow_policy="drop_oldest"))
        for enabled in (True, False):
            bmp["enabled"] = enabled
            with self.subTest(enabled=enabled), self.assertRaisesRegex(ValueError, "Duplicate I2C"):
                self.plan(document)

    def test_explicit_recipe_edit_preserves_tasks_routes_and_other_devices(self):
        document = load_document(ROOT / "configs/nodes/i2c-recipe-demo.json")
        original = deepcopy(document)
        device = document["devices"][0]
        updated = recipe(device_id=device["device_id"], register=3, interval_ms=2000)
        result = prepare_recipe(document, updated, self.descriptor,
                                replace_device_id=device["device_id"])
        self.plan(result)
        self.assertEqual(result["devices"][0]["behaviour"]["register"], 3)
        self.assertEqual(result["devices"][0]["task"], device["task"])
        self.assertEqual(result["devices"][0]["behaviour"]["destination"], device["behaviour"]["destination"])
        self.assertEqual(result["devices"][1:], document["devices"][1:])
        self.assertEqual(result["queues"], document["queues"])
        self.assertEqual(result["interfaces"], document["interfaces"])
        self.assertEqual(document, original)

    def test_explicit_edit_refuses_missing_recipe_other_driver_or_rename(self):
        document = load_document(ROOT / "configs/nodes/i2c-recipe-demo.json")
        for target, name in (("absent", "absent"), ("oled_status", "oled_status"),
                             ("bmp_register", "renamed")):
            with self.subTest(target=target, name=name), self.assertRaises(ValueError):
                prepare_recipe(document, recipe(device_id=name), self.descriptor, replace_device_id=target)

    def test_example_preserves_oled_radar_wiring_and_uses_raw_queue(self):
        demo = load_document(ROOT / "configs/nodes/i2c-recipe-demo.json")
        original = load_document(ROOT / "configs/nodes/c3-protocol-demo.json")
        self.plan(demo)
        self.assertEqual(demo["interfaces"], original["interfaces"])
        self.assertEqual([device["driver"] for device in demo["devices"]],
                         ["i2c_recipe", "ssd1306_spi", "hlk_ld2450"])
        self.assertNotIn("telemetry.raw", demo["devices"][1]["behaviour"]["sources"])
        metadata = recipe_catalogue(self.descriptor)
        self.assertEqual(metadata["recipe_limits"]["read_length"], {"min": 1, "max": 16})
        self.assertEqual(metadata["recipe_examples"][0]["recipe"]["register"], 0)
        metadata["recipe_limits"]["read_length"]["max"] = 99
        self.assertEqual(recipe_catalogue(self.descriptor)["recipe_limits"]["read_length"]["max"], 16)


class RecipeWorkflowTests(unittest.TestCase):
    def setUp(self):
        self.flow = Workflow(opener=lambda _: Port(), client_factory=FakeClient)
        self.addCleanup(self.flow.close)
        self.flow.connect(port="fake")
        self.body = dict(profile=json.dumps(empty_i2c_profile()), recipe=recipe(), wiring_confirmed=True)

    def allow_recipe(self):
        self.flow.caps = self.flow.client.caps = replace(self.flow.caps,
            drivers=self.flow.caps.drivers + ("i2c_recipe",))

    def test_prepare_requires_compiled_driver_and_confirmation(self):
        with self.assertRaisesRegex(ValueError, "compatible.*recipe|recipe.*driver"):
            self.flow.dispatch("recipe-prepare", self.body)
        self.allow_recipe()
        for body in (self.body | {"wiring_confirmed": False}, self.body | {"unexpected": 1}):
            with self.subTest(body=body), self.assertRaises(ValueError):
                self.flow.dispatch("recipe-prepare", body)
        self.assertEqual(self.flow.client.calls, [])

    def test_prepare_returns_draft_and_invalidates_previous_review_without_mutation(self):
        self.allow_recipe()
        self.flow.review(profile=self.body["profile"])
        result = self.flow.dispatch("recipe-prepare", self.body)
        self.assertEqual(result["profile"]["devices"][0]["driver"], "i2c_recipe")
        self.assertIsNone(self.flow.pending)
        self.assertEqual(self.flow.client.calls, [])
        self.flow.review(profile=json.dumps(result["profile"]))

    def test_prepare_rechecks_capabilities_and_rejects_offline(self):
        self.allow_recipe()
        self.flow.client.caps = replace(self.flow.caps, drivers=("bmp388",))
        with self.assertRaisesRegex(ValueError, "capabilities changed"):
            self.flow.dispatch("recipe-prepare", self.body)
        self.flow.close()
        with self.assertRaisesRegex(ValueError, "Connect"):
            self.flow.dispatch("recipe-prepare", self.body)

    def test_workflow_accepts_only_an_explicit_existing_recipe_edit(self):
        self.allow_recipe()
        document = load_document(ROOT / "configs/nodes/i2c-recipe-demo.json")
        request = self.body | dict(profile=json.dumps(document), replace_device_id="bmp_register",
                                  recipe=recipe(device_id="bmp_register", register=3))
        result = self.flow.dispatch("recipe-prepare", request)
        self.assertEqual(result["profile"]["devices"][0]["behaviour"]["register"], 3)
        self.assertEqual(result["profile"]["queues"], document["queues"])
        with self.assertRaises(ValueError):
            self.flow.dispatch("recipe-prepare", request | {"replace_device_id": None})
        self.assertEqual(self.flow.client.calls, [])


if __name__ == "__main__":
    unittest.main()
