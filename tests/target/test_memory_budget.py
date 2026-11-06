"""Fixed-budget failures must not be mistaken for whole-runtime qualification."""
from copy import deepcopy
import unittest
from tools.target.memory_budget import analyze, build_is_supported, evidence_matches_budget


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

    def test_distinct_owned_task_group_is_counted_once(self):
        self.files["esp-idf/uthome_task/libuthome_task.a:task.c.obj"] = {
            "memory_types": {"DRAM": {"sections": {".dram0.bss": {"size": 7}}}}}
        self.budget.update(
            archive_groups={"/uthome_task/libuthome_task.a:": "task_pool_fixed"},
            module_groups={"task.c.obj": "task_pool_fixed"},
            module_limits={"worker.c.obj": 16, "task.c.obj": 8},
            task_pool_fixed_limit=9,
            total_dram_limit=70,
            require_all_module_limits=True)
        result = analyze(self.files, self.summary, self.budget)
        self.assertTrue(result["passed"])
        self.assertEqual(result["groups"], dict(application_fixed=10, task_pool_fixed=7,
                                               kernel_sdk_fixed=13, ram_code=10,
                                               total_dram=40, rtc=0))
        self.assertEqual(sum(result["groups"][name] for name in
                             ("application_fixed", "task_pool_fixed", "kernel_sdk_fixed")), 30)
        self.assertEqual(result["modules"]["task.c.obj"]["group"], "task_pool_fixed")
        exceeded = deepcopy(self.budget)
        exceeded["task_pool_fixed_limit"] = 6
        self.assertIn("group_budget_exceeded: task_pool_fixed",
                      analyze(self.files, self.summary, exceeded)["failures"])
        overreserved = deepcopy(self.budget)
        overreserved["module_limits"]["task.c.obj"] = 10
        self.assertIn("module_reservations_exceed_group_limit: task_pool_fixed",
                      analyze(self.files, self.summary, overreserved)["failures"])

    def test_grouped_policy_rejects_ambiguous_or_mismatched_attribution(self):
        self.budget.update(archive_groups={"/main/": "task_pool_fixed"},
                           task_pool_fixed_limit=20)
        with self.assertRaises(ValueError):
            analyze(self.files, self.summary, self.budget)
        self.budget["archive_groups"] = {}
        self.budget["module_groups"] = {"worker.c.obj": "task_pool_fixed"}
        self.assertIn("module_group_mismatch: worker.c.obj",
                      analyze(self.files, self.summary, self.budget)["failures"])

    def test_grouped_policy_rejects_unused_limits_and_every_reservation_overrun(self):
        self.summary["layout"].append({"name": "RTC SLOW", "used": 1})
        self.budget["rtc_limit"] = 1
        self.budget["require_all_module_limits"] = True
        self.budget["module_limits"]["unused.c.obj"] = 1
        self.assertIn("reserved_module_not_linked: unused.c.obj",
                      analyze(self.files, self.summary, self.budget)["failures"])
        del self.budget["module_limits"]["unused.c.obj"]
        changes = (("module", lambda b: b["module_limits"].update({"worker.c.obj": 9})),
                   ("application", lambda b: b.update(application_fixed_limit=9)),
                   ("kernel", lambda b: b.update(kernel_sdk_fixed_limit=19)),
                   ("code", lambda b: b.update(ram_code_limit=9)),
                   ("total", lambda b: b.update(total_dram_limit=39)),
                   ("rtc", lambda b: b.update(rtc_limit=0)))
        for name, change in changes:
            with self.subTest(name=name):
                budget = deepcopy(self.budget)
                change(budget)
                self.assertFalse(analyze(self.files, self.summary, budget)["passed"])

    def test_supported_build_modes_require_a_passed_evidence_record(self):
        for mode in ("configuration_application", "wifi_probe", "ble_probe"):
            self.assertTrue(build_is_supported({"build_status": "passed", mode: True}))
        self.assertFalse(build_is_supported({"build_status": "failed", "wifi_probe": True}))
        self.assertFalse(build_is_supported({"build_status": "passed", "model_probe": True}))

    def test_station_policy_requires_exact_build_evidence(self):
        budget = {"required_evidence": {"configuration_application": True,
                                         "network_services": "wifi",
                                         "diagnostic_only": False}}
        evidence = {"configuration_application": True, "network_services": "wifi",
                    "diagnostic_only": False}
        self.assertTrue(evidence_matches_budget(evidence, budget))
        evidence["network_services"] = "ble"
        self.assertFalse(evidence_matches_budget(evidence, budget))
