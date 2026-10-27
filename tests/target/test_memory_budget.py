"""Fixed-budget failures must not be mistaken for whole-runtime qualification."""
from copy import deepcopy
import unittest
from tools.target.memory_budget import analyze


class MemoryBudgetTests(unittest.TestCase):
    def setUp(self):
        self.files = {"esp-idf/main/libmain.a:worker.c.obj": {
            "memory_types": {"DRAM": {"sections": {".dram0.bss": {"size": 10}}}}}}
        self.summary = {"layout": [{"name": "DRAM", "total": 100, "used": 40, "free": 60,
                                    "parts": {".bss": {"size": 20}, ".data": {"size": 5}, ".noinit": {"size": 5}}}]}
        self.budget = dict(scope="test", module_limits={"worker.c.obj": 16}, application_fixed_limit=20,
                           kernel_sdk_fixed_limit=25, ram_code_limit=15, total_dram_limit=60, rtc_limit=0)

    def test_alias_code_and_pool_ram_are_counted_once(self):
        result = analyze(self.files, self.summary, self.budget)
        self.assertTrue(result["passed"])
        self.assertEqual(result["groups"], dict(application_fixed=10, kernel_sdk_fixed=20,
                                               ram_code=10, total_dram=40, rtc=0))
        self.assertFalse(result["whole_system_static_qualified"])
        self.assertFalse(result["runtime_heap_measured"])

    def test_missing_module_overruns_and_overcommitted_reservations_fail(self):
        for change in (lambda b: b.update(module_limits={}),
                       lambda b: b.update(module_limits={"worker.c.obj": 9}),
                       lambda b: b.update(module_limits={"worker.c.obj": 21}),
                       lambda b: b.update(kernel_sdk_fixed_limit=19),
                       lambda b: b.update(total_dram_limit=39),
                       lambda b: b.update(total_dram_limit=101)):
            budget = deepcopy(self.budget)
            change(budget)
            self.assertFalse(analyze(self.files, self.summary, budget)["passed"])

    def test_missing_and_duplicate_application_attribution_fail(self):
        with self.assertRaises(ValueError):
            analyze({}, self.summary, self.budget)
        self.files["other/main/libmain.a:worker.c.obj"] = deepcopy(next(iter(self.files.values())))
        with self.assertRaises(ValueError):
            analyze(self.files, self.summary, self.budget)

    def test_owned_radio_is_application_memory_and_requires_its_own_budget(self):
        self.files['esp-idf/uthome_radio/libuthome_radio.a:c3ble.c.obj'] = {
            'memory_types': {'DRAM': {'sections': {'.dram0.bss': {'size': 2}}}}}
        self.assertFalse(analyze(self.files, self.summary, self.budget)['passed'])
        self.budget['module_limits']['c3ble.c.obj'] = 3
        result = analyze(self.files, self.summary, self.budget)
        self.assertTrue(result['passed'])
        self.assertEqual(result['groups']['application_fixed'], 12)
        self.assertEqual(result['groups']['kernel_sdk_fixed'], 18)
